// Host regression and contract test for Emergency P0 safe save restore
//
//     g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_save_restore_contract.cpp -o /tmp/t && /tmp/t

#include "path_util.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)


static std::string read_file_to_string(const char* path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return {};
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static int test_preflight_ordering_contract() {
    // 1. Shared function declaration in save_menu.hpp
    const std::string save_menu_hpp = read_file_to_string("sphaira/include/ui/menus/save_menu.hpp");
    CHECK(!save_menu_hpp.empty());
    CHECK(save_menu_hpp.find("Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path") != std::string::npos);

    // 2. Shared function existence and ordering in save_menu_ops.cpp
    const std::string save_menu_code = read_file_to_string("sphaira/source/ui/menus/save/save_menu_ops.cpp");
    CHECK(!save_menu_code.empty());

    const auto rsz_pos = save_menu_code.find("Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path");
    CHECK(rsz_pos != std::string::npos);

    const auto rsi_pos = save_menu_code.find("Result Menu::RestoreSaveInternal(", rsz_pos);
    CHECK(rsi_pos != std::string::npos);

    const std::string rsz_body = save_menu_code.substr(rsz_pos, rsi_pos - rsz_pos);

    const auto preflight_pos = rsz_body.find("TransferUnzipPreflight");
    CHECK(preflight_pos != std::string::npos);

    const auto create_pos = rsz_body.find("fsCreateSaveDataFileSystem", preflight_pos);
    CHECK(create_pos != std::string::npos);
    CHECK(preflight_pos < create_pos);

    CHECK(rsz_body.find("fsExtendSaveDataFileSystem") == std::string::npos);

    const auto delete_coll_pos = rsz_body.find("DeleteAllCollections", preflight_pos);
    CHECK(delete_coll_pos != std::string::npos);
    CHECK(preflight_pos < delete_coll_pos);

    const auto unzip_pos = rsz_body.find("TransferUnzipAll", delete_coll_pos);
    CHECK(unzip_pos != std::string::npos);

    const auto commit_pos = rsz_body.find("save_fs.Commit()", unzip_pos);
    CHECK(commit_pos != std::string::npos);

    // 3. Passes save_dbi_compat=true to both preflight and extraction
    CHECK(rsz_body.find("TransferUnzipPreflight(pbox, zfile, \"/\", save_filter, true, &summary)") != std::string::npos);
    CHECK(rsz_body.find("TransferUnzipAll(pbox, zfile, &save_fs, \"/\", save_filter, thread::Mode::SingleThreadedIfSmaller, true)") != std::string::npos);

    // 4. Menu::RestoreSaveInternal delegates to RestoreSaveZip and has no ZIP lifecycle after RAW branch
    const auto bsi_pos = save_menu_code.find("Result Menu::BackupSaveInternal(", rsi_pos);
    CHECK(bsi_pos != std::string::npos);
    const std::string rsi_body = save_menu_code.substr(rsi_pos, bsi_pos - rsi_pos);

    CHECK(rsi_body.find("return RestoreSaveZip(pbox, e, path") != std::string::npos);

    const auto raw_end = rsi_body.find("log_write(\"finished raw save restore\\n\");");
    CHECK(raw_end != std::string::npos);
    const std::string rsi_after_raw = rsi_body.substr(raw_end);
    CHECK(rsi_after_raw.find("TransferUnzipPreflight") == std::string::npos);
    CHECK(rsi_after_raw.find("fsExtendSaveDataFileSystem") == std::string::npos);
    CHECK(rsi_after_raw.find("DeleteAllCollections") == std::string::npos);
    CHECK(rsi_after_raw.find("TransferUnzipAll") == std::string::npos);
    CHECK(rsi_after_raw.find("save_fs.Commit()") == std::string::npos);

    // 5. Verify File Browser restore calls the shared function and removes local ZIP restore
    const std::string fb_code = read_file_to_string("sphaira/source/ui/menus/filebrowser/filebrowser_ops.cpp");
    CHECK(!fb_code.empty());

    const auto fb_restore_pos = fb_code.find("void FsView::RestoreSaveFile(");
    CHECK(fb_restore_pos != std::string::npos);

    const auto fb_unzip_pos = fb_code.find("void FsView::UnzipFiles(", fb_restore_pos);
    CHECK(fb_unzip_pos != std::string::npos);

    const std::string fb_restore_body = fb_code.substr(fb_restore_pos, fb_unzip_pos - fb_restore_pos);

    CHECK(fb_restore_body.find("return save::RestoreSaveZip(pbox, se, file_path") != std::string::npos);
    CHECK(fb_restore_body.find("TransferUnzipPreflight") == std::string::npos);
    CHECK(fb_restore_body.find("FsNativeSave") == std::string::npos);
    CHECK(fb_restore_body.find("DeleteAllCollections") == std::string::npos);
    CHECK(fb_restore_body.find("TransferUnzipAll") == std::string::npos);
    CHECK(fb_restore_body.find("save_fs.Commit") == std::string::npos);

    // 6. Verify fs.cpp CRUD wrappers return fsFsCommit
    const std::string fs_code = read_file_to_string("sphaira/source/fs.cpp");
    CHECK(!fs_code.empty());
    CHECK(fs_code.find("return fsFsCommit(fs);") != std::string::npos);

    // 7. Verify threaded_file_transfer.cpp uses IsSafeExtractionDestination and ResolveArchiveEntryName
    const std::string tft_code = read_file_to_string("sphaira/source/threaded_file_transfer.cpp");
    CHECK(!tft_code.empty());
    CHECK(tft_code.find("IsSafeExtractionDestination") != std::string::npos);
    CHECK(tft_code.find("ResolveArchiveEntryName") != std::string::npos);

    return 0;
}

int main() {
    if (test_preflight_ordering_contract()) {
        return 1;
    }
    std::printf("ok  save_restore_contract: %d checks passed\n", g_checks);
    return 0;
}
