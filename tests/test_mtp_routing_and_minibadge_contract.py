#!/usr/bin/env python3
"""
Regression source contracts for:
1. MTP package routing (SD root/subfolders preserve files physically,
   virtual Install folder remains the sole target for streaming installs).
2. Clean dismissal of residual InstallSession on subsequent normal MTP transfers.
3. Minimized badge progress filling during active installation with deferred plan,
   guarding against fake 100%/percentages when size is truly unknown.
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INTERNAL_CPP = ROOT / "sphaira/source/haze/haze_internal.cpp"
HELPER_CPP = ROOT / "sphaira/source/haze_helper.cpp"
APP_FRAME_CPP = ROOT / "sphaira/source/app_frame.cpp"
DBI_DRAW_CPP = ROOT / "sphaira/source/ui/menus/dbi/dbi_draw.cpp"
README_MD = ROOT / "README.md"


PATCH_PTP = ROOT / "sphaira/cmake/patch_libhaze_ptp.cmake"
PATCH_MTP = ROOT / "sphaira/cmake/patch_libhaze_mtp.cmake"


def check(condition: bool, msg: str) -> None:
    if not condition:
        print(f"FAIL: {msg}")
        sys.exit(1)


def test_device_root_vs_storage_routing() -> None:
    # The patch application test exercises the actual libhaze sources; this
    # contract catches accidental removal of routing from either PTP entry point.
    for name, path in [("SendObjectInfo", PATCH_PTP), ("SendObjectPropList", PATCH_MTP)]:
        src = path.read_text(encoding="utf-8")
        check("const bool is_device_root" in src, f"{name} must distinguish the device root")
        check("PtpObjectFormatCode_Association" in src, f"{name} must keep folders on microSD")
        check(all(ext in src for ext in (".nsp", ".nsz", ".xci", ".xcz")),
              f"{name} must recognize supported packages")
        check("install" in src and "GetName()[0]" in src,
              f"{name} must select Install and microSD storages")
        check("ResultInvalidStorageId()" in src, f"{name} must fail if the selected storage is disabled")


def test_routing_contracts() -> None:
    cpp_src = INTERNAL_CPP.read_text(encoding="utf-8")
    helper_src = HELPER_CPP.read_text(encoding="utf-8")

    # 1. ROOT_DROP_RULES in haze_internal.cpp must NOT intercept NSP/NSZ/XCI/XCZ for install.
    check("RootDropAction::Install" not in cpp_src,
          "ROOT_DROP_RULES must not contain RootDropAction::Install (SD root must not intercept packages)")
    check("NRO_EXT, RootDropAction::RedirectDir, \"/switch\"" in cpp_src,
          "ROOT_DROP_RULES must still redirect .nro to /switch")

    # 2. Virtual Install folder remains configured via MakeFsInstallProxy
    check("MakeFsInstallProxy(\"install\", display_name)" in helper_src,
          "haze_helper must retain MakeFsInstallProxy as the dedicated Install storage target")


def test_residual_session_contracts() -> None:
    helper_src = HELPER_CPP.read_text(encoding="utf-8")
    app_frame_src = APP_FRAME_CPP.read_text(encoding="utf-8")

    # 1. haze_callback WriteBegin/ReadBegin must request exit for terminal/summary install sessions
    check("session->RequestExit();" in helper_src,
          "haze_helper must signal RequestExit() for residual install sessions on normal MTP transfer")
    check("session->AllPackagesTerminal()" in helper_src,
          "haze_helper must inspect AllPackagesTerminal() when checking for residual install sessions")
    check("session->GetOrigin() == ui::menu::dbi::TransportOrigin::Mtp" in helper_src,
          "normal MTP copy must not dismiss a session owned by another transport")

    # 2. app_frame.cpp must prioritize active MTP transfer ProgressBox over terminal/residual sessions
    check("!m_active_transfer_pbox || (session->GetState() == ui::menu::dbi::State::Installing && !session->AllPackagesTerminal())" in app_frame_src,
          "app_frame.cpp Draw must not allow residual install sessions to mask m_active_transfer_pbox")
    check("if (session && active_install)" in app_frame_src,
          "app_frame.cpp Update must not send input to a residual install session")


def test_minibadge_contracts() -> None:
    dbi_draw_src = DBI_DRAW_CPP.read_text(encoding="utf-8")

    # Verify presence of package-level ratio calculation when plan is deferred
    check("const bool has_known_overall = !has_deferred_plan && m_plan_total_bytes > 0;" in dbi_draw_src,
          "DrawMiniBadge must use overall progress only when the plan is known")
    check("const bool has_known_package = (m_progress_size > 0);" in dbi_draw_src,
          "DrawMiniBadge must use package progress only when its size is known")
    check("m_progress_offset / (double)m_progress_size" in dbi_draw_src,
          "DrawMiniBadge must use m_progress_offset / m_progress_size when package size is known")
    check("has_known_overall || has_known_package" in dbi_draw_src,
          "DrawMiniBadge must gate percentage display and bar fill behind known overall or package size")


def test_readme_docs() -> None:
    readme = README_MD.read_text(encoding="utf-8")
    check("automatically intercept supported formats (NSP, NSZ, XCI, XCZ) copied to the root of the microSD card" not in readme,
          "README.md must not contain outdated claim of intercepting root microSD copies")
    check("dedicated virtual `Install` folder" in readme or "virtual `Install` folder" in readme,
          "README.md must describe the virtual Install folder as the installation target")


def main() -> None:
    print("Running MTP routing, residual cleanup, and MiniBadge source contracts...")
    test_device_root_vs_storage_routing()
    test_routing_contracts()
    test_residual_session_contracts()
    test_minibadge_contracts()
    test_readme_docs()
    print("PASS: all MTP routing and MiniBadge contracts verified successfully.")


if __name__ == "__main__":
    main()
