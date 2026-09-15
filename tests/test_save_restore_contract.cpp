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
    // 3. Preflight ordering and commit contracts
    const std::string save_menu_code = read_file_to_string("sphaira/source/ui/menus/save/save_menu_ops.cpp");
    CHECK(!save_menu_code.empty());

    const auto rsi_pos = save_menu_code.find("RestoreSaveInternal");
    CHECK(rsi_pos != std::string::npos);

    const auto preflight_pos = save_menu_code.find("TransferUnzipPreflight", rsi_pos);
    CHECK(preflight_pos != std::string::npos);

    const auto create_pos = save_menu_code.find("fsCreateSaveDataFileSystem", preflight_pos);
    CHECK(create_pos != std::string::npos);
    CHECK(preflight_pos < create_pos);

    const auto extend_pos = save_menu_code.find("fsExtendSaveDataFileSystem", preflight_pos);
    CHECK(extend_pos != std::string::npos);
    CHECK(preflight_pos < extend_pos);

    const auto delete_coll_pos = save_menu_code.find("DeleteAllCollections", preflight_pos);
    CHECK(delete_coll_pos != std::string::npos);
    CHECK(preflight_pos < delete_coll_pos);

    const auto unzip_pos = save_menu_code.find("TransferUnzipAll", delete_coll_pos);
    CHECK(unzip_pos != std::string::npos);

    const auto commit_pos = save_menu_code.find("save_fs.Commit()", unzip_pos);
    CHECK(commit_pos != std::string::npos);

    // Verify File Browser restore ordering and commit
    const std::string fb_code = read_file_to_string("sphaira/source/ui/menus/filebrowser/filebrowser_ops.cpp");
    CHECK(!fb_code.empty());

    const auto fb_restore_pos = fb_code.find("RestoreSaveFile");
    CHECK(fb_restore_pos != std::string::npos);

    const auto fb_preflight_pos = fb_code.find("TransferUnzipPreflight", fb_restore_pos);
    CHECK(fb_preflight_pos != std::string::npos);

    const auto fb_del_pos = fb_code.find("DeleteAllCollections", fb_preflight_pos);
    CHECK(fb_del_pos != std::string::npos);
    CHECK(fb_preflight_pos < fb_del_pos);

    const auto fb_unzip_pos = fb_code.find("TransferUnzipAll", fb_del_pos);
    CHECK(fb_unzip_pos != std::string::npos);

    const auto fb_commit_pos = fb_code.find("R_TRY(save_fs.Commit())", fb_unzip_pos);
    CHECK(fb_commit_pos != std::string::npos);

    // Verify fs.cpp CRUD wrappers return fsFsCommit
    const std::string fs_code = read_file_to_string("sphaira/source/fs.cpp");
    CHECK(!fs_code.empty());
    CHECK(fs_code.find("return fsFsCommit(fs);") != std::string::npos);

    // Verify threaded_file_transfer.cpp uses IsSafeExtractionDestination and ResolveArchiveEntryName
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
