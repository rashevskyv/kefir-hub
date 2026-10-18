// Firmware from the store (github.com/kefircfw/nx-firmware): /firmware is filled NCA by NCA. What the console already
// holds in its system storage is copied from there (checked by SHA-256), only the rest is downloaded (also checked).
// The result is the same folder the zip used to give, so validation and the install prompt stay as they are.
#include "ui/menus/kefir/kefir_firmware.hpp"
#include "fw_store.hpp"
#include "app.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/progress_box.hpp"

#include <yyjson.h>
#include <switch.h>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::kefir::detail {
namespace {

constexpr const char* STORE_INDEX_URL = "https://raw.githubusercontent.com/kefircfw/nx-firmware/main/index.json";
constexpr const char* DEST = "/firmware";
constexpr size_t CHUNK = 1 << 20;

struct Store {
    std::string download_tmpl;
    std::string manifest_tmpl;
    std::vector<std::string> versions;
};

auto ReadJson(const std::string& url, std::vector<u8>& out) -> bool {
    const auto result = curl::Api().ToMemory(curl::Url{url});
    if (!result.success || result.data.empty()) {
        return false;
    }
    out = result.data;
    return true;
}

auto FetchIndex(Store& store) -> bool {
    std::vector<u8> data;
    if (!ReadJson(STORE_INDEX_URL, data)) {
        return false;
    }
    auto* doc = yyjson_read(reinterpret_cast<const char*>(data.data()), data.size(), 0);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));
    auto* root = yyjson_doc_get_root(doc);
    const auto str = [&](const char* key) -> std::string {
        auto* v = yyjson_obj_get(root, key);
        return v && yyjson_is_str(v) ? yyjson_get_str(v) : std::string{};
    };
    store.download_tmpl = str("download");
    store.manifest_tmpl = str("manifest");
    auto* versions = yyjson_obj_get(root, "versions");
    size_t i, max;
    yyjson_val* it;
    yyjson_arr_foreach(versions, i, max, it) {
        if (auto* v = yyjson_obj_get(it, "version"); v && yyjson_is_str(v)) {
            store.versions.emplace_back(yyjson_get_str(v));
        }
    }
    return !store.download_tmpl.empty() && !store.manifest_tmpl.empty() && !store.versions.empty();
}

auto FetchManifest(const Store& store, const std::string& version, std::vector<fwstore::Nca>& ncas) -> bool {
    auto url = store.manifest_tmpl;
    if (const auto pos = url.find("{version}"); pos != std::string::npos) {
        url.replace(pos, 9, version);
    }
    std::vector<u8> data;
    if (!ReadJson(url, data)) {
        return false;
    }
    auto* doc = yyjson_read(reinterpret_cast<const char*>(data.data()), data.size(), 0);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));
    auto* list = yyjson_obj_get(yyjson_doc_get_root(doc), "ncas");
    size_t i, max;
    yyjson_val* it;
    yyjson_arr_foreach(list, i, max, it) {
        const auto str = [&](const char* key) -> std::string {
            auto* v = yyjson_obj_get(it, key);
            return v && yyjson_is_str(v) ? yyjson_get_str(v) : std::string{};
        };
        fwstore::Nca nca{str("name"), 0, str("sha256"), str("release")};
        if (auto* v = yyjson_obj_get(it, "size"); v && yyjson_is_int(v)) {
            nca.size = yyjson_get_sint(v);
        }
        if (nca.name.empty() || nca.sha256.size() != 64 || nca.release.empty() || nca.size <= 0 || !fwstore::ContentId(nca.name)) {
            return false; // a manifest we do not understand: do not build half a firmware from it
        }
        ncas.push_back(std::move(nca));
    }
    return !ncas.empty();
}

auto HexOf(const u8* hash) -> std::string {
    static constexpr char DIGITS[] = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < SHA256_HASH_SIZE; i++) {
        out += DIGITS[hash[i] >> 4];
        out += DIGITS[hash[i] & 15];
    }
    return out;
}

auto Sha256OfFile(const std::string& path) -> std::string {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        return {};
    }
    ON_SCOPE_EXIT(std::fclose(f));
    Sha256Context ctx;
    sha256ContextCreate(&ctx);
    auto buf = std::make_unique<u8[]>(CHUNK);
    while (const auto n = std::fread(buf.get(), 1, CHUNK, f)) {
        sha256ContextUpdate(&ctx, buf.get(), n);
    }
    u8 hash[SHA256_HASH_SIZE];
    sha256ContextGetHash(&ctx, hash);
    return HexOf(hash);
}

auto ToContentId(const fwstore::Nca& nca) -> NcmContentId {
    NcmContentId id{};
    if (const auto raw = fwstore::ContentId(nca.name)) {
        std::copy(raw->begin(), raw->end(), id.c);
    }
    return id;
}

// the NCA from the console's system storage into /firmware; true when the file there matches the manifest
auto CopyFromConsole(NcmContentStorage* cs, const fwstore::Nca& nca, const std::string& dst, ProgressBox* pbox, s64& done, s64 total) -> bool {
    const auto id = ToContentId(nca);
    FILE* out = std::fopen(dst.c_str(), "wb");
    if (!out) {
        return false;
    }
    Sha256Context ctx;
    sha256ContextCreate(&ctx);
    auto buf = std::make_unique<u8[]>(CHUNK);
    bool ok = true;
    for (s64 offset = 0; offset < nca.size && ok; offset += CHUNK) {
        const size_t want = static_cast<size_t>(std::min<s64>(CHUNK, nca.size - offset));
        ok = R_SUCCEEDED(ncmContentStorageReadContentIdFile(cs, buf.get(), want, &id, offset)) &&
             std::fwrite(buf.get(), 1, want, out) == want;
        if (ok) {
            sha256ContextUpdate(&ctx, buf.get(), want);
        }
        if (pbox->ShouldExit()) {
            ok = false;
        }
    }
    std::fclose(out);
    u8 hash[SHA256_HASH_SIZE];
    sha256ContextGetHash(&ctx, hash);
    if (!ok || HexOf(hash) != nca.sha256) {
        std::remove(dst.c_str());
        return false;
    }
    done += nca.size;
    pbox->UpdateTransfer(done, total);
    return true;
}

auto DownloadNca(const Store& store, const fwstore::Nca& nca, const std::string& dst, ProgressBox* pbox, s64& done, s64 total) -> Result {
    const auto url = fwstore::AssetUrl(store.download_tmpl, nca.release, nca.name);
    for (int attempt = 0; attempt < 2; attempt++) {
        std::remove(dst.c_str());
        const s64 base = done;
        const auto result = curl::Api().ToFile(
            curl::Url{url},
            curl::Path{dst},
            curl::OnProgress{[&](s64 dltotal, s64 dlnow, s64, s64) {
                if (pbox->ShouldExit()) {
                    return false;
                }
                pbox->UpdateTransfer(base + dlnow, total);
                return true;
            }}
        );
        if (pbox->ShouldExit()) {
            R_THROW(Result_TransferCancelled);
        }
        if (result.success && Sha256OfFile(dst) == nca.sha256) {
            done += nca.size;
            R_SUCCEED();
        }
        log_write("[fwstore] %s: attempt %d failed (http %ld)\n", nca.name.c_str(), attempt + 1, result.code);
    }
    std::remove(dst.c_str());
    R_THROW(Result_AppstoreFailedZipDownload);
}

} // namespace

// "22.5.0" from an updater entry name; empty for names that are not a plain version (and for Rebootless packs, which
// the store does not hold)
auto StoreVersionOf(const UpdaterEntry& entry) -> std::string {
    std::string v;
    for (const auto c : entry.name) {
        if ((c >= '0' && c <= '9') || c == '.') {
            v += c;
        } else if (!v.empty()) {
            break;
        }
    }
    if (std::count(v.begin(), v.end(), '.') != 2 || v.back() == '.') {
        return {};
    }
    std::string lower = entry.url + entry.name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    if (lower.find("rebootless") != std::string::npos) {
        return {};
    }
    return v;
}

// Result_FsEmpty = the store does not hold this version (or cannot be reached): the caller uses the zip instead.
auto DownloadFirmwareFromStore(ProgressBox* pbox, const std::string& version) -> Result {
    Store store;
    pbox->NewTransfer("Checking the firmware store"_i18n);
    if (!FetchIndex(store) || std::find(store.versions.begin(), store.versions.end(), version) == store.versions.end()) {
        log_write("[fwstore] %s is not in the store\n", version.c_str());
        R_THROW(Result_FsEmpty);
    }
    std::vector<fwstore::Nca> ncas;
    if (!FetchManifest(store, version, ncas)) {
        log_write("[fwstore] manifest of %s unreadable\n", version.c_str());
        R_THROW(Result_FsEmpty);
    }

    NcmContentStorage cs{};
    const bool have_cs = R_SUCCEEDED(ncmOpenContentStorage(&cs, NcmStorageId_BuiltInSystem));
    ON_SCOPE_EXIT(if (have_cs) ncmContentStorageClose(&cs));
    const auto plan = fwstore::MakePlan(ncas, [&](const fwstore::Nca& nca) -> std::optional<s64> {
        if (!have_cs) {
            return std::nullopt;
        }
        const auto id = ToContentId(nca);
        bool has{};
        s64 size{};
        if (R_SUCCEEDED(ncmContentStorageHas(&cs, &has, &id)) && has && R_SUCCEEDED(ncmContentStorageGetSizeFromContentId(&cs, &size, &id))) {
            return size;
        }
        return std::nullopt;
    });
    log_write("[fwstore] %s: %zu NCAs, %zu from the console, %zu to download (%lld of %lld bytes)\n", version.c_str(),
        ncas.size(), plan.copy.size(), plan.download.size(), (long long)plan.download_bytes, (long long)plan.total_bytes);

    fs::FsNativeSd sd;
    R_TRY(sd.GetFsOpenResult());
    s64 free_bytes{};
    if (R_SUCCEEDED(sd.GetFreeSpace("/", &free_bytes)) && free_bytes > 0 && free_bytes < fwstore::NeededSdBytes(plan)) {
        R_THROW(Result_GameMoveNotEnoughSpace);
    }

    if (sd.DirExists(DEST)) {
        R_TRY(sd.DeleteDirectoryRecursively(DEST));
    }
    R_TRY(sd.CreateDirectoryRecursively(DEST));

    s64 done = 0;
    std::vector<size_t> to_download = plan.download;
    pbox->NewTransfer("Copying from the console"_i18n);
    for (const auto index : plan.copy) {
        R_TRY(pbox->ShouldExitResult());
        const auto& nca = ncas[index];
        if (!CopyFromConsole(&cs, nca, std::string{DEST} + "/" + nca.name, pbox, done, plan.total_bytes)) {
            if (pbox->ShouldExit()) {
                R_THROW(Result_TransferCancelled);
            }
            log_write("[fwstore] %s could not be copied from the console: downloading it\n", nca.name.c_str());
            to_download.push_back(index);
        }
    }
    pbox->NewTransfer("Downloading"_i18n + " " + version);
    for (const auto index : to_download) {
        const auto& nca = ncas[index];
        R_TRY(DownloadNca(store, nca, std::string{DEST} + "/" + nca.name, pbox, done, plan.total_bytes));
    }
    R_TRY(sd.Commit());
    log_write("[fwstore] %s ready in %s\n", version.c_str(), DEST);
    R_SUCCEED();
}

// Adds the versions named in the store's featured.json ("firmware": ["23.0.1"]) to the updater's firmware list, next
// to the ones nx-links has, newest first. The owner edits that file on the server; the store holds many more versions
// than users should see. The entry's url is the zip of the same version, used when the store cannot be reached at
// install time.
void AppendStoreFirmwares(const fs::FsPath& featured_path, std::vector<UpdaterEntry>& entries) {
    auto* doc = yyjson_read_file(featured_path, YYJSON_READ_NOFLAG, nullptr, nullptr);
    if (!doc) {
        return;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));
    auto* versions = yyjson_obj_get(yyjson_doc_get_root(doc), "firmware");
    size_t i, max;
    yyjson_val* it;
    yyjson_arr_foreach(versions, i, max, it) {
        if (!yyjson_is_str(it)) {
            continue;
        }
        const std::string version = yyjson_get_str(it);
        if (StoreVersionOf({UpdaterEntryType::Firmware, version, {}, false}).empty()) {
            continue; // not a plain version
        }
        const bool known = std::any_of(entries.begin(), entries.end(), [&](const UpdaterEntry& e) {
            return e.type == UpdaterEntryType::Firmware && e.name == version;
        });
        if (!known) {
            entries.push_back({UpdaterEntryType::Firmware, version,
                "https://github.com/THZoria/NX_Firmware/releases/download/" + version + "/Firmware." + version + ".zip", false});
        }
    }
    // the firmware entries newest first, the Kefir ones stay where they are
    const auto key = [](const std::string& name) {
        unsigned a{}, b{}, c{};
        std::sscanf(name.c_str(), "%u.%u.%u", &a, &b, &c);
        return (static_cast<unsigned long long>(a) << 40) | (static_cast<unsigned long long>(b) << 20) | c;
    };
    std::stable_sort(entries.begin(), entries.end(), [&](const UpdaterEntry& x, const UpdaterEntry& y) {
        if (x.type != UpdaterEntryType::Firmware || y.type != UpdaterEntryType::Firmware) {
            return false;
        }
        return key(x.name) > key(y.name);
    });
}

} // namespace sphaira::ui::menu::kefir::detail
