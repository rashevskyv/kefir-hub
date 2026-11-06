// The one place Kefir Hub calls the hacBrewPackLZ engine. Built inside the hbp library with exceptions on:
// every engine call is wrapped here, and only strings and plain values go back to the app.
// Mirrors what the engine's own app does (switch/app/app_work.cpp: loadGame, estimateJob, compressInstalled).

#include "hbp_bridge.hpp"
#include "log.hpp"

#include "core/console.h"
#include "nx/nx_console.h"

#include <algorithm>
#include <mutex>
#include <set>

namespace sphaira::hbp {
namespace {

using namespace ::hbp;

const std::string Folder = std::string("sdmc:") + DataFolder;
const std::string JournalFolder = Folder + "/journal";
const char* const KeyPaths[] = {"sdmc:/switch/prod.keys", "sdmc:/prod.keys"};

// storages this run already showed can read a compressed test copy; the check writes and mounts one, so once is enough.
std::mutex g_checked_mutex;
std::set<Storage> g_checked;

struct Sink final : JobSink {
    explicit Sink(const ProgressFn& fn) : m_fn{fn} { setSink(this); }
    ~Sink() override { setSink(nullptr); }

    void progress(const char* label, i64 done, i64 total, i64) override {
        if (m_fn && !m_fn(label, done, total)) {
            stop = true;
        }
    }
    void counting(const char* label, i64 sofar) override { progress(label, sofar, 0, 0); }
    void message(const std::string& text) override { log_write("[hbp] %s\n", text.c_str()); }
    void warning(const std::string& text) override { log_write("[hbp] warning: %s\n", text.c_str()); }

private:
    const ProgressFn& m_fn;
};

Keyset LoadKeys() {
    for (auto path : KeyPaths) {
        if (sys::exists(path)) {
            Keyset keyset;
            keyset.load(path);
            return keyset;
        }
    }
    throw ToolError("prod.keys not found in /switch. Dump it with Lockpick_RCM");
}

std::unique_ptr<ConsoleApi> OpenConsole() {
    std::string problem;
    auto console = openConsole(problem);
    if (!console) {
        throw ToolError(problem);
    }
    return console;
}

ConvertOptions Options() {
    ConvertOptions options;
    options.threads = sys::workerCount();
    options.tempDir = Folder + "/temp";
    // ponytail: the engine's spareMemory() assumes it owns the heap; Kefir Hub holds its UI too, so cap it.
    // Raise the cap once a big game is seen to run fine with more.
    options.holdInMemory = std::min<i64>(sys::spareMemory(), 512LL << 20);
    options.keepSmallSections = std::min<i64>(options.holdInMemory, 256LL << 20);
    // the engine's own default: the firmware still reads game data back through the new copy before the swap.
    options.checkResult = false;
    return options;
}

struct Loaded {
    InstalledGame game;
    std::shared_ptr<InstalledArchive> archive;
    std::unique_ptr<TitleSet> set;
};

std::unique_ptr<Loaded> LoadGame(ConsoleApi& console, const Keyset& keyset, u64 app_id) {
    auto loaded = std::make_unique<Loaded>();
    loaded->game = console.describe(app_id);
    if (!loaded->game.blocked.empty()) {
        throw ToolError(loaded->game.blocked);
    }
    loaded->archive = std::make_shared<InstalledArchive>(console, loaded->game);
    loaded->set = std::make_unique<TitleSet>(keyset);
    console.addTitleKeys(loaded->set->tickets(), keyset);
    loaded->set->add(loaded->archive);
    loaded->set->group();
    for (auto& problem : loaded->set->problems) {
        log_write("[hbp] %s\n", problem.message.c_str());
    }
    if (loaded->set->jobs.empty()) {
        throw ToolError(loaded->set->problems.empty() ? std::string("This game holds nothing that can be rebuilt") : loaded->set->problems[0].message);
    }
    const bool has_base = std::any_of(loaded->game.metas.begin(), loaded->game.metas.end(), [](const InstalledMeta& meta) { return meta.key.type == Cnmt::MetaApplication; });
    const bool base_loaded = std::any_of(loaded->set->jobs.begin(), loaded->set->jobs.end(), [](const TitleJob& job) { return job.isBaseGame(); });
    if (has_base && !base_loaded) {
        throw ToolError(loaded->set->problems.empty() ? std::string("Its base game can't be read") : loaded->set->problems[0].message);
    }
    return loaded;
}

Storage StorageOf(const InstalledGame& game) {
    for (auto& meta : game.metas) {
        if (meta.key.type == Cnmt::MetaApplication) {
            return meta.storage;
        }
    }
    return game.metas.empty() ? Storage::Sd : game.metas.front().storage;
}

template <typename F>
std::string Guard(const char* what, F&& work) {
    try {
        work();
        return {};
    } catch (const Cancelled&) {
        return Stopped;
    } catch (const std::exception& e) {
        log_write_error("[hbp] %s: %s\n", what, e.what());
        return e.what();
    } catch (...) {
        log_write_error("[hbp] %s: unknown error\n", what);
        return "Unknown error";
    }
}

} // namespace

std::string RecoverIfNeeded() {
    return Guard("recover", [] {
        if (!sys::isDirectory(JournalFolder) || sys::list(JournalFolder, false).empty()) {
            return;
        }
        auto console = OpenConsole();
        for (auto& line : Replacer(*console, JournalFolder).recover()) {
            log_write("[hbp] recover: %s\n", line.c_str());
        }
    });
}

std::string EstimateGame(u64 app_id, Estimate& out, const ProgressFn& progress) {
    return Guard("estimate", [&] {
        Sink sink{progress};
        const auto keyset = LoadKeys();
        auto console = OpenConsole();
        auto loaded = LoadGame(*console, keyset, app_id);
        const auto options = Options();

        out = {};
        out.already_compressed = true;
        for (auto& job : loaded->set->jobs) {
            out.has_update |= job.update != nullptr;
            if (isAlreadyCompressed(job, options)) {
                out.now += job.inputBytes;
                out.after += job.inputBytes;
                continue;
            }
            out.already_compressed = false;
            const auto one = estimateTitle(job, options);
            out.now += one.inputBytes;
            out.after += one.outputBytes;
            out.margin += one.marginBytes;
        }
    });
}

std::string CompressGame(u64 app_id, s64& before, s64& after, const ProgressFn& progress) {
    return Guard("compress", [&] {
        Sink sink{progress};
        const auto keyset = LoadKeys();
        auto console = OpenConsole();
        const auto options = Options();
        sys::makeDirectories(JournalFolder);
        Replacer replacer(*console, JournalFolder);

        before = console->describe(app_id).totalBytes();
        // a title at a time, reading the console afresh each time, as the last one changed what is there.
        std::set<u64> done;
        while (true) {
            auto loaded = LoadGame(*console, keyset, app_id);
            const TitleJob* job{};
            for (auto& candidate : loaded->set->jobs) {
                if (!done.count(candidate.main->meta.titleId)) {
                    job = &candidate;
                    break;
                }
            }
            if (!job) {
                break;
            }
            done.insert(job->main->meta.titleId);
            if (isAlreadyCompressed(*job, options)) {
                continue;
            }

            const auto storage = StorageOf(loaded->game);
            {
                std::scoped_lock lock{g_checked_mutex};
                if (!g_checked.count(storage)) {
                    replacer.checkConsole(storage, options, keyset);
                    g_checked.insert(storage);
                }
            }
            replacer.run(loaded->game, *job, options, keyset);
        }
        after = console->describe(app_id).totalBytes();
    });
}

} // namespace sphaira::hbp
