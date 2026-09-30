#!/usr/bin/env python3
"""Source contract for modal input and MTP install cancellation.

Hardware USB behavior still requires a Switch and PC.
"""

from pathlib import Path


root = Path(__file__).resolve().parents[1] / "sphaira"


def source(path: str) -> str:
    return (root / path).read_text(encoding="utf-8")


frame = source("source/app_frame.cpp")
update = frame.split("void App::Update() {", 1)[1].split("void App::Draw()", 1)[0]
assert "m_widgets.back()->IsModal() && !m_widgets.back()->IsMinimized()" in update
assert "if (!active_install && !has_modal)" in update
assert "else if (!has_modal && !session->IsMinimized())" in update
assert "session->Update(nullptr, nullptr);" in update
assert "m_widgets.back()->Update(&m_controller, &m_touch_info);" in update

session = source("source/ui/menus/dbi/dbi_session.cpp")
cancel = session.split("void InstallSession::CancelSession() {", 1)[1].split("void Menu::CancelSession()", 1)[0]
assert "m_stream->Disable();" in cancel
assert "if (m_origin == TransportOrigin::Mtp)" in cancel
assert "sphaira::haze::CancelTransfer();" in cancel

mtp = source("source/haze/haze_internal.cpp")
cancel = mtp.split("void CancelTransfer() {", 1)[1].split("std::vector<PinnedMount>", 1)[0]
assert "if (g_mtp_transfer_active)" in cancel
assert "if (HasActiveTransfer())" in cancel
assert "::haze::CancelTransfer();" in cancel

for path in ("source/haze/haze_install_proxy.cpp", "source/haze/haze_fs_proxy.cpp"):
    write = source(path).split("Result WriteFile(", 1)[1].split("void CloseFile(", 1)[0]
    assert "R_THROW(::haze::ResultCancelled());" in write, path

app = source("source/app.cpp")
shutdown = app.split('"[SHUTDOWN] begin mtp"', 1)[1].split('"[SHUTDOWN] end mtp', 1)[0]
assert shutdown.index("BackgroundInstaller::TeardownWorker();") < shutdown.index("haze::Exit(false);")

restart = source("source/ui/menus/install_stream_menu_base.cpp")
assert "needs_mtp_restart && !App::IsExiting()" in restart
assert restart.count("if (App::IsExiting())") >= 2
assert "haze::Exit(false);" in restart
assert "ui::menu::dbi::ShouldRestartMtp(" in restart
assert "c->origin, source_interrupted, true" in restart
assert "R_FAILED(rc) && !user_cancelled" in restart
assert "if (needs_mtp_restart && !App::IsExiting()) {\n                    c->session->AddLog" in restart
assert "c->session->RequestExit();\n                    ScheduleMtpRestart();" in restart

print("modal input and MTP cancellation contracts: PASS")
