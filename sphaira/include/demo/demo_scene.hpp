#pragma once

// DOCS_DEMO builds only: frozen demo screens for docs screenshots (list in source/demo/demo_scene.cpp).

#include <string>

namespace sphaira::demo {

// after the main menu is up: opens the screen named by `[demo] scene` in config.ini, if any.
void StartScene();

// install scenes (demo_install.cpp).
void SceneInstallReview();
void SceneInstallUsbQueue();
void SceneInstallProgress();
void SceneInstallMinimized();
void SceneInstallScreensaver();
void SceneInstallSummary();
void SceneInstallStream(bool mtp);

// dbi_local.cpp: a deferred demo package named "(broken copy)" reports a failed analysis.
bool IsBrokenPackage(const std::string& file_name);

} // namespace sphaira::demo
