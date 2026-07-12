#include "ui/menus/cheats/cheats_dmnt.hpp"
#include "ui/menus/cheats/cheats_lookup.hpp"
#include "defines.hpp"
#include "log.hpp"

#include <switch.h>
#include <string>

namespace sphaira::ui::menu::hats {

// Get Build ID from dmnt:cht service (when game is running/suspended)
auto GetBuildIdFromDmnt(u64 title_id) -> std::string {
    Result rc = pmdmntInitialize();
    if (R_FAILED(rc)) {
        log_write("[Cheats] Failed to initialize pmdmnt: %x\n", rc);
        return "";
    }
    ON_SCOPE_EXIT(pmdmntExit());

    u64 application_pid = 0;
    bool found_application = false;

    // Mirror EdiZon's attach flow as closely as possible: wait for the
    // suspended application PID, then let dmnt attach and trust the metadata.
    for (int i = 0; i < 10; i++) {
        if (R_SUCCEEDED(pmdmntGetApplicationProcessId(&application_pid)) && application_pid != 0) {
            found_application = true;
            break;
        }
        svcSleepThread(100'000'000ULL);
    }

    if (!found_application) {
        log_write("[Cheats] No application PID available from pmdmnt\n");
        return "";
    }

    u64 application_title_id = 0;
    rc = pmdmntGetProgramId(&application_title_id, application_pid);
    if (R_SUCCEEDED(rc)) {
        log_write("[Cheats] Active application PID %016lx reports title %016lx\n",
                  application_pid, application_title_id);
    } else {
        log_write("[Cheats] Failed to get program ID for active application PID %016lx: %x\n",
                  application_pid, rc);
    }

    Service dmntchtSrv;
    rc = smGetService(&dmntchtSrv, "dmnt:cht");
    if (R_FAILED(rc)) {
        log_write("[Cheats] Failed to get dmnt:cht service: %x\n", rc);
        return "";
    }

    ON_SCOPE_EXIT(serviceClose(&dmntchtSrv));

    u8 has_cheat_process = 0;
    bool attached = false;
    for (int attempt = 0; attempt < 10; attempt++) {
        rc = serviceDispatch(&dmntchtSrv, 65003);
        if (R_FAILED(rc)) {
            log_write("[Cheats] Force-open cheat process failed on attempt %d: %x\n", attempt + 1, rc);
            svcSleepThread(100'000'000ULL);
            continue;
        }

        rc = serviceDispatchOut(&dmntchtSrv, 65000, has_cheat_process);
        if (R_FAILED(rc)) {
            log_write("[Cheats] Failed to query cheat-process state on attempt %d: %x\n", attempt + 1, rc);
            svcSleepThread(100'000'000ULL);
            continue;
        }

        if ((has_cheat_process & 1) != 0) {
            attached = true;
            break;
        }

        svcSleepThread(100'000'000ULL);
    }

    if (!attached) {
        log_write("[Cheats] dmnt:cht never reported an attached cheat process\n");
        return "";
    }

    // Get process metadata (command 65002)
    DmntCheatProcessMetadata metadata{};
    rc = serviceDispatchOut(&dmntchtSrv, 65002, metadata);
    if (R_FAILED(rc)) {
        log_write("[Cheats] Failed to get cheat metadata: %x\n", rc);
        return "";
    }

    log_write("[Cheats] dmnt:cht metadata - title_id: %016lx, process_id: %016lx, app_pid: %016lx\n",
              metadata.title_id, metadata.process_id, application_pid);

    if (metadata.process_id == 0) {
        log_write("[Cheats] dmnt:cht returned metadata with no process ID\n");
        return "";
    }

    if (metadata.title_id != title_id) {
        log_write("[Cheats] Running title %016lx doesn't match target %016lx\n",
                  metadata.title_id, title_id);
        return "";
    }

    if (metadata.process_id != application_pid) {
        log_write("[Cheats] dmnt:cht process %016lx differs from pmdmnt PID %016lx, using metadata title match\n",
                  metadata.process_id, application_pid);
    }

    std::string build_id = detail::NormalizeBuildId(detail::BytesToBuildId(metadata.main_nso_build_id, 8));
    if (!detail::IsValidBuildId(build_id)) {
        log_write("[Cheats] dmnt:cht returned an invalid Build ID: %s\n", build_id.c_str());
        return "";
    }

    log_write("[Cheats] Got Build ID from dmnt:cht: %s\n", build_id.c_str());

    return build_id;
}

} // namespace sphaira::ui::menu::hats
