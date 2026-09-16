#!/usr/bin/env python3
"""
Compiler-free source contract and synthetic behavioral regression test for
Sphaira shutdown lifecycle safety and install session publication synchronization.

NOTE ON SCOPE AND LIMITATIONS:
This test validates static source contracts and a Python behavioral simulation model
of the shutdown ordering, cancellation flow, install session admission serialization,
MTP port handback vs final stop, and synchronous diagnostic breadcrumbs.
It explicitly does NOT execute compiled C++ binaries, libnx IPC calls, hardware
interrupts, or Nintendo Switch hardware primitives.
"""

import os
import re
import sys

def check(condition, msg):
    if not condition:
        print(f"FAIL: {msg}")
        sys.exit(1)

# Paths relative to repo root
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CMAKELISTS_PATH = os.path.join(REPO_ROOT, "sphaira", "CMakeLists.txt")
APP_HEADER_PATH = os.path.join(REPO_ROOT, "sphaira", "include", "app.hpp")
HAZE_HEADER_PATH = os.path.join(REPO_ROOT, "sphaira", "include", "haze_helper.hpp")
HAZE_SOURCE_PATH = os.path.join(REPO_ROOT, "sphaira", "source", "haze_helper.cpp")
APP_SOURCE_PATH = os.path.join(REPO_ROOT, "sphaira", "source", "app.cpp")
MAIN_SOURCE_PATH = os.path.join(REPO_ROOT, "sphaira", "source", "main.cpp")
WEB_SOURCE_PATH = os.path.join(REPO_ROOT, "sphaira", "source", "web.cpp")
DBI_SESSION_PATH = os.path.join(REPO_ROOT, "sphaira", "source", "ui", "menus", "dbi", "dbi_session.cpp")

# ---------------------------------------------------------------------------
# 1. Source Contract Tests
# ---------------------------------------------------------------------------

def test_cmakelists_version():
    print("[1] Checking CMakeLists.txt version contract...")
    with open(CMAKELISTS_PATH, "r", encoding="utf-8") as f:
        content = f.read()
    m = re.search(r"set\(sphaira_VERSION\s+(\d+\.\d+\.\d+)\)", content)
    check(m is not None, "CMakeLists.txt must define a valid sphaira_VERSION")
    app_version = m.group(1)
    print(f"    OK: version {app_version} found.")
    return app_version

def test_haze_helper_contracts():
    print("[2] Checking haze_helper header and source contracts...")
    with open(HAZE_HEADER_PATH, "r", encoding="utf-8") as f:
        hdr = f.read()
    check("void Exit(bool reinit_usb_host = true);" in hdr,
          "haze_helper.hpp must declare Exit(bool reinit_usb_host = true)")

    with open(HAZE_SOURCE_PATH, "r", encoding="utf-8") as f:
        src = f.read()

    exit_start = src.find("void Exit(bool reinit_usb_host)")
    check(exit_start != -1, "haze_helper.cpp must implement Exit(bool reinit_usb_host)")
    exit_end = src.find("bool IsRunning()", exit_start)
    exit_body = src[exit_start:exit_end]

    check("if (reinit_usb_host)" in exit_body,
          "haze_helper.cpp must condition usbHsFsInitialize on reinit_usb_host")
    check("g_should_exit = true;" in exit_body,
          "haze_helper.cpp must set g_should_exit before stopping libhaze")
    check("ueventSignal(&g_mtp_done_event);" in exit_body,
          "haze_helper.cpp must signal done event on exit")
    check("::haze::Exit();" in exit_body,
          "haze_helper.cpp must call ::haze::Exit()")
    check("g_fs_entries.clear();" in exit_body,
          "haze_helper.cpp must clear proxies after ::haze::Exit()")

    idx_haze_exit = exit_body.find("::haze::Exit();")
    idx_clear = exit_body.find("g_fs_entries.clear();")
    check(idx_haze_exit < idx_clear,
          "::haze::Exit() must be called before g_fs_entries.clear() to join before destruction")

    print("    OK: haze_helper contracts verified.")

def test_install_session_admission_contracts():
    print("[3] Checking install session admission synchronization contracts...")
    with open(APP_HEADER_PATH, "r", encoding="utf-8") as f:
        hdr = f.read()
    check("static auto CloseInstallAdmissionAndGetSession() -> std::shared_ptr<ui::menu::dbi::InstallSession>;" in hdr,
          "app.hpp must declare CloseInstallAdmissionAndGetSession()")
    check("bool m_install_sessions_closed{false};" in hdr,
          "app.hpp must declare m_install_sessions_closed flag")
    check("Mutex m_install_session_mutex{};" in hdr,
          "app.hpp must declare m_install_session_mutex")

    with open(APP_SOURCE_PATH, "r", encoding="utf-8") as f:
        src = f.read()

    # Verify PushInstallSession checks m_install_sessions_closed under mutex
    push_start = src.find("auto App::PushInstallSession(")
    check(push_start != -1, "App::PushInstallSession not found in app.cpp")
    push_end = src.find("}", push_start)
    push_body = src[push_start:push_end]

    check("SCOPED_MUTEX(&g_app->m_install_session_mutex);" in push_body,
          "PushInstallSession must acquire m_install_session_mutex")
    check("g_app->m_install_sessions_closed" in push_body,
          "PushInstallSession must check m_install_sessions_closed")

    # Verify CloseInstallAdmissionAndGetSession sets m_install_sessions_closed under mutex
    close_start = src.find("auto App::CloseInstallAdmissionAndGetSession(")
    check(close_start != -1, "App::CloseInstallAdmissionAndGetSession not found in app.cpp")
    close_end = src.find("}", close_start)
    close_body = src[close_start:close_end]

    check("SCOPED_MUTEX(&g_app->m_install_session_mutex);" in close_body,
          "CloseInstallAdmissionAndGetSession must acquire m_install_session_mutex")
    check("g_app->m_install_sessions_closed = true;" in close_body,
          "CloseInstallAdmissionAndGetSession must set m_install_sessions_closed = true")
    check("return g_app->m_active_install_session;" in close_body,
          "CloseInstallAdmissionAndGetSession must return snapshot of active session")

    # Verify App::~App() calls CloseInstallAdmissionAndGetSession at top
    destr_start = src.find("App::~App() {")
    check(destr_start != -1, "App::~App() not found in app.cpp")
    destr_end = src.find("auto App::GetVersionFromString", destr_start)
    destr_body = src[destr_start:destr_end]

    idx_close_adm = destr_body.find("CloseInstallAdmissionAndGetSession();")
    idx_bg_start = destr_body.find("[SHUTDOWN] begin background services")
    idx_web_stop = destr_body.find("sphaira::WebShareStop();")

    check(idx_close_adm != -1, "App::~App() must call CloseInstallAdmissionAndGetSession()")
    check(idx_close_adm < idx_bg_start,
          "CloseInstallAdmissionAndGetSession() must be called at start of App::~App() before background teardown")
    check(idx_close_adm < idx_web_stop,
          "CloseInstallAdmissionAndGetSession() must be called before WebShareStop()")

    print("    OK: install session admission contracts verified.")

def test_app_shutdown_contracts():
    print("[4] Checking App::~App() shutdown ordering and breadcrumbs...")
    with open(APP_SOURCE_PATH, "r", encoding="utf-8") as f:
        src = f.read()

    # Find App::~App() body
    start = src.find("App::~App() {")
    check(start != -1, "App::~App() not found in app.cpp")
    end = src.find("auto App::GetVersionFromString", start)
    check(end != -1, "End of App::~App() not found")
    destructor_body = src[start:end]

    # Check breadcrumbs in App::~App()
    required_breadcrumbs = [
        '[SHUTDOWN] begin app exit v%s',
        '[SHUTDOWN] begin background services',
        '[SHUTDOWN] end background services (%zu ms)',
        '[SHUTDOWN] begin mtp',
        '[SHUTDOWN] end mtp (%zu ms)',
        '[SHUTDOWN] begin web',
        '[SHUTDOWN] end web (%zu ms)',
        '[SHUTDOWN] begin usb host',
        '[SHUTDOWN] end usb host (%zu ms)',
        '[SHUTDOWN] begin widgets/gpu',
        '[SHUTDOWN] end widgets/gpu (%zu ms)',
        '[SHUTDOWN] end app exit (%zu ms)',
    ]
    for bc in required_breadcrumbs:
        check(bc in destructor_body, f"App::~App() missing breadcrumb: {bc}")

    # Check ordering in destructor
    idx_bg = destructor_body.find("[SHUTDOWN] begin background services")
    idx_mtp = destructor_body.find("[SHUTDOWN] begin mtp")
    idx_web = destructor_body.find("[SHUTDOWN] begin web")
    idx_usb = destructor_body.find("[SHUTDOWN] begin usb host")
    idx_gpu = destructor_body.find("[SHUTDOWN] begin widgets/gpu")

    check(idx_bg < idx_mtp, "background services must precede mtp")
    check(idx_mtp < idx_web, "mtp must precede web")
    check(idx_web < idx_usb, "web must precede usb host")
    check(idx_usb < idx_gpu, "usb host must precede widgets/gpu")

    # Check MTP final stop uses false (no reinit)
    check("haze::Exit(false);" in destructor_body,
          "App::~App() must call haze::Exit(false) to avoid restarting USB host")

    # Check cancellation before WebShareStop
    idx_cancel_session = destructor_body.find("cancel_session->CancelSession();")
    idx_pbox_request_exit = destructor_body.find("pbox->RequestExit();")
    idx_web_stop = destructor_body.find("sphaira::WebShareStop();")
    check(idx_cancel_session != -1 and idx_cancel_session < idx_web_stop,
          "CancelSession() must be called before WebShareStop()")
    check(idx_pbox_request_exit != -1 and idx_pbox_request_exit < idx_web_stop,
          "ProgressBox RequestExit() must be called before WebShareStop()")

    # Check WebShareStop occurs before owner resets and GPU teardown
    idx_pbox_reset = destructor_body.find("m_active_transfer_pbox.reset();")
    idx_session_reset = destructor_body.find("m_active_install_session.reset();")
    idx_widgets_pop = destructor_body.find("m_widgets.pop_back();")
    idx_i18n_exit = destructor_body.find("i18n::exit();")
    idx_vg_delete = destructor_body.find("nvgDeleteDk(this->vg);")

    check(idx_web_stop < idx_pbox_reset, "WebShareStop must precede m_active_transfer_pbox.reset()")
    check(idx_web_stop < idx_session_reset, "WebShareStop must precede m_active_install_session.reset()")
    check(idx_web_stop < idx_widgets_pop, "WebShareStop must precede m_widgets.pop_back()")
    check(idx_web_stop < idx_i18n_exit, "WebShareStop must precede i18n::exit()")
    check(idx_web_stop < idx_vg_delete, "WebShareStop must precede nvgDeleteDk")

    print("    OK: App::~App() shutdown ordering and breadcrumbs verified.")

def test_main_exit_contracts():
    print("[5] Checking userAppExit() contracts in main.cpp...")
    with open(MAIN_SOURCE_PATH, "r", encoding="utf-8") as f:
        src = f.read()

    start = src.find("void userAppExit(void) {")
    check(start != -1, "userAppExit() not found in main.cpp")
    end = src.find("}", start)
    user_exit_body = src[start:end+1]

    check('[SHUTDOWN] begin userAppExit' in user_exit_body,
          "userAppExit must have '[SHUTDOWN] begin userAppExit' breadcrumb")
    check('[SHUTDOWN] end userAppExit (%zu ms)' in user_exit_body,
          "userAppExit must have '[SHUTDOWN] end userAppExit (%zu ms)' breadcrumb")
    check("sphaira::WebShareStop();" in user_exit_body,
          "userAppExit must preserve idempotent WebShareStop() fallback")
    check('fsdevCommitDevice("sdmc");' in user_exit_body,
          "userAppExit must commit sdmc device")

    print("    OK: userAppExit() contracts verified.")

def test_web_prompt_shutdown_contracts():
    print("[6] Checking web.cpp prompt cancellation loops and fail-closed publication...")
    with open(WEB_SOURCE_PATH, "r", encoding="utf-8") as f:
        src = f.read()

    check("void WebShareStop() {" in src, "WebShareStop must be defined in web.cpp")
    idx_manifest = src.find("void HandleUploadManifest(")
    check(idx_manifest != -1, "HandleUploadManifest found in web.cpp")
    idx_manifest_end = src.find("void ReceiveUpload(", idx_manifest)
    manifest_body = src[idx_manifest:idx_manifest_end]
    check("!g_share_running.load()" in manifest_body,
          "HandleUploadManifest loop must check !g_share_running.load()")

    # Verify both manifest and direct install check PushInstallSession return value
    check("if (!App::PushInstallSession(session))" in manifest_body,
          "HandleUploadManifest must check return of PushInstallSession")

    recv_body = src[idx_manifest_end:]
    check("if (!App::PushInstallSession(session))" in recv_body,
          "ReceiveUpload must check return of PushInstallSession")

    print("    OK: web.cpp prompt cancellation loops and fail-closed publication verified.")

# ---------------------------------------------------------------------------
# 2. Behavioral Simulation Model (Interleaving Serialization & Safety)
# ---------------------------------------------------------------------------

class MockInstallSession:
    def __init__(self, name="Web install"):
        self.name = name
        self.cancelled = False
        self.choice = -1 # -1 = pending UI choice

    def cancel(self):
        self.cancelled = True

    def prompt_reinstall(self, ui_active=False):
        """Simulates InstallSession::PromptReinstall loop."""
        if self.cancelled:
            return False
        if ui_active:
            self.choice = 1
            return True
        # If UI loop is stopped and cancel is not requested, this would hang!
        if not self.cancelled:
            raise TimeoutError("Deadlock: PromptReinstall blocked forever waiting for dead UI loop!")
        return False

class MockApp:
    def __init__(self):
        self.mutex_locked = False
        self.active_session = None
        self.admission_closed = False

    def push_install_session(self, session):
        """Mirrors App::PushInstallSession under mutex."""
        assert not self.mutex_locked, "Reentrant mutex lock"
        self.mutex_locked = True
        try:
            if self.admission_closed or self.active_session is not None:
                return False
            self.active_session = session
            return True
        finally:
            self.mutex_locked = False

    def close_admission_and_get_session(self):
        """Mirrors App::CloseInstallAdmissionAndGetSession under mutex."""
        assert not self.mutex_locked, "Reentrant mutex lock"
        self.mutex_locked = True
        try:
            self.admission_closed = True
            return self.active_session
        finally:
            self.mutex_locked = False

class MockSystem:
    def __init__(self):
        self.app = MockApp()
        self.usb_host_running = True
        self.mtp_running = False
        self.mtp_callbacks_suppressed = False
        self.mtp_proxies_alive = False
        self.web_running = False
        self.web_producer_active = False
        self.transfer_pbox_alive = False
        self.transfer_pbox_exit_requested = False
        self.widgets_alive = True
        self.i18n_alive = True
        self.gpu_alive = True
        self.log_entries = []

    def log_error(self, fmt, *args):
        py_fmt = fmt.replace("%zu", "%d")
        msg = py_fmt % args if args else fmt
        for secret_token in ["/switch/", "/save/", "token", "password", "email", "@"]:
            if secret_token in msg:
                raise ValueError(f"Secret or full path leaked in breadcrumb: {msg}")
        self.log_entries.append(msg)

    def mtp_start(self):
        self.usb_host_running = False
        self.mtp_running = True
        self.mtp_callbacks_suppressed = False
        self.mtp_proxies_alive = True

    def mtp_exit(self, reinit_usb_host=True):
        if not self.mtp_running:
            return
        self.mtp_running = False
        self.mtp_callbacks_suppressed = True
        self.mtp_proxies_alive = False
        if reinit_usb_host:
            self.usb_host_running = True

    def web_start(self):
        self.web_running = True
        self.web_producer_active = True

    def web_stop(self):
        was_running = self.web_running
        self.web_running = False
        if was_running:
            self.web_producer_active = False

def test_behavioral_simulation(app_version):
    print("[7] Running behavioral shutdown lifecycle & interleaving simulation...")

    # Case A: Runtime MTP cycle (settings / disconnect / pinned mount)
    sys_a = MockSystem()
    sys_a.mtp_start()
    check(sys_a.mtp_running and not sys_a.usb_host_running, "MTP should be active, host inactive")
    sys_a.mtp_exit(reinit_usb_host=True)
    check(not sys_a.mtp_running and sys_a.usb_host_running, "Runtime MTP stop must restore USB host")
    print("    OK: runtime MTP port handback verified.")

    # Case B: Final shutdown with MTP active
    sys_b = MockSystem()
    sys_b.mtp_start()
    sys_b.log_error("[SHUTDOWN] begin mtp")
    sys_b.mtp_exit(reinit_usb_host=False)
    sys_b.log_error("[SHUTDOWN] end mtp (%zu ms)", 5)
    check(not sys_b.mtp_running and not sys_b.usb_host_running,
          "Final MTP stop must NOT reinitialize USB host")
    check(sys_b.mtp_callbacks_suppressed and not sys_b.mtp_proxies_alive,
          "MTP exit must suppress callbacks and clear proxies")
    print("    OK: final MTP stop without host reinit verified.")

    # Case C: Interleaving A (Publication BEFORE shutdown admission closure)
    # 1. In-flight Web request creates and pushes session
    sys_c = MockSystem()
    sys_c.web_start()
    worker_session = MockInstallSession("Web direct install")
    accepted = sys_c.app.push_install_session(worker_session)
    check(accepted, "Interleaving A: session pushed before admission closure must be accepted")

    # 2. App destructor begins: closes admission and gets snapshot
    sys_c.log_error("[SHUTDOWN] begin app exit v%s", app_version)
    cancel_session = sys_c.app.close_admission_and_get_session()
    check(cancel_session is worker_session,
          "Interleaving A: snapshot must capture pre-closure session")
    # Destructor cancels session without holding mutex
    cancel_session.cancel()

    # 3. Web worker reaches PromptReinstall while UI loop is already stopped
    # Because cancel was delivered, PromptReinstall unblocks cleanly (returns False)
    unblocked = not worker_session.prompt_reinstall(ui_active=False)
    check(unblocked, "Interleaving A: PromptReinstall must unblock on cancellation")

    # 4. Destructor joins Web producer cleanly
    sys_c.web_stop()
    check(not sys_c.web_producer_active, "Interleaving A: Web producer successfully joined")
    print("    OK: Interleaving A (publication before admission closure -> cancelled & joined) verified.")

    # Case D: Interleaving B (Shutdown admission closure BEFORE publication)
    # 1. App destructor begins: closes admission and gets snapshot
    sys_d = MockSystem()
    sys_d.web_start()
    sys_d.log_error("[SHUTDOWN] begin app exit v%s", app_version)
    cancel_session_d = sys_d.app.close_admission_and_get_session()
    check(cancel_session_d is None, "Interleaving B: no session active at admission closure")

    # 2. In-flight Web request attempts to push session
    late_session = MockInstallSession("Late web install")
    accepted_d = sys_d.app.push_install_session(late_session)
    check(not accepted_d, "Interleaving B: session publication after admission closure MUST be rejected")

    # 3. Caller handles rejection by aborting without starting install
    # Handlers in web.cpp return HTTP 409/500 immediately; no InstallFromSource/PromptReinstall
    sys_d.web_stop()
    check(not sys_d.web_producer_active, "Interleaving B: Web producer joined without entering install")
    print("    OK: Interleaving B (admission closed before publication -> rejected fail-closed) verified.")

    # Case E: Proof of Pre-Fix Vulnerability (Interleaving without admission closure -> deadlock)
    vuln_app = MockApp()
    vuln_session = None
    # Pre-fix App::~App() called GetActiveInstallSession() without closing admission:
    vuln_snapshot = vuln_app.active_session # None!
    # Meanwhile in-flight Web worker pushes session:
    worker_s = MockInstallSession("Unsynchronized install")
    pushed = vuln_app.push_install_session(worker_s) # Accepted!
    check(pushed, "Vulnerability proof: session was accepted without admission gate")
    # Destructor has vuln_snapshot == None, so it does NOT cancel worker_s
    # Destructor calls web_stop() and waits for worker
    # Worker executes and calls PromptReinstall with dead UI loop:
    deadlock_detected = False
    try:
        worker_s.prompt_reinstall(ui_active=False)
    except TimeoutError:
        deadlock_detected = True
    check(deadlock_detected, "Vulnerability proof: uncancelled session must deadlock PromptReinstall")
    print("    OK: pre-fix race/deadlock reproduction verified.")

    # Case F: Full shutdown sequence breadcrumb verification
    sys_f = MockSystem()
    sys_f.mtp_start()
    sys_f.web_start()
    sys_f.log_error("[SHUTDOWN] begin app exit v%s", app_version)
    sys_f.log_error("[SHUTDOWN] begin background services")
    sys_f.log_error("[SHUTDOWN] end background services (%zu ms)", 2)
    sys_f.log_error("[SHUTDOWN] begin mtp")
    sys_f.mtp_exit(reinit_usb_host=False)
    sys_f.log_error("[SHUTDOWN] end mtp (%zu ms)", 10)
    sys_f.log_error("[SHUTDOWN] begin web")
    sys_f.web_stop()
    sys_f.log_error("[SHUTDOWN] end web (%zu ms)", 3)
    sys_f.log_error("[SHUTDOWN] begin usb host")
    sys_f.log_error("[SHUTDOWN] end usb host (%zu ms)", 0)
    sys_f.log_error("[SHUTDOWN] begin widgets/gpu")
    sys_f.widgets_alive = False
    sys_f.i18n_alive = False
    sys_f.gpu_alive = False
    sys_f.log_error("[SHUTDOWN] end widgets/gpu (%zu ms)", 15)
    sys_f.log_error("[SHUTDOWN] end app exit (%zu ms)", 30)
    sys_f.log_error("[SHUTDOWN] begin userAppExit")
    sys_f.web_stop() # Idempotent fallback
    sys_f.log_error("[SHUTDOWN] end userAppExit (%zu ms)", 5)

    phases = ["app exit", "background services", "mtp", "web", "usb host", "widgets/gpu", "userAppExit"]
    for p in phases:
        has_begin = any(f"begin {p}" in e for e in sys_f.log_entries)
        has_end = any(f"end {p}" in e for e in sys_f.log_entries)
        check(has_begin and has_end, f"Phase '{p}' must have matched begin and end breadcrumbs")
    print("    OK: shutdown breadcrumb pairing verified.")

def main():
    print("=============================================================")
    print("Sphaira Shutdown Lifecycle & Session Admission Regression")
    print("=============================================================")
    app_version = test_cmakelists_version()
    test_haze_helper_contracts()
    test_install_session_admission_contracts()
    test_app_shutdown_contracts()
    test_main_exit_contracts()
    test_web_prompt_shutdown_contracts()
    test_behavioral_simulation(app_version)
    print("=============================================================")
    print("ALL SHUTDOWN LIFECYCLE CHECKS PASSED (7/7 groups).")
    print("NOTE: Model and source checks verify contracts; hardware/C++")
    print("execution must be verified by compiled testing and Switch HW.")
    print("=============================================================")

if __name__ == "__main__":
    main()
