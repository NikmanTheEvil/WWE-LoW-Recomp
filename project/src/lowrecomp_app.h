// lowrecomp - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <filesystem>
#include <string>

#include <rex/cvar.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>

#include "low_input.h"

class LowrecompApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<LowrecompApp>(new LowrecompApp(ctx, "lowrecomp",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // Eigene Eingabe: SDL-Controller + Tastatur/Maus im WWE-2K-Stil (low_input.cpp)
  void OnPreSetup(rex::RuntimeConfig& config) override {
    config.input_factory = [](bool tool_mode) { return low::CreateInputSystem(tool_mode); };
  }
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}

  // ReXGlue liest game_data_root & Co. aus den CVars, BEVOR es lowrecomp.toml
  // laedt - Pfade aus der Datei (vom Launcher) kaemen sonst zu spaet an.
  // Deshalb die Datei hier schon einmal laden und die Pfade daraus fuellen.
  // Die Kommandozeile hat weiterhin Vorrang (hoehere CVar-Quelle).
  void OnConfigurePaths(rex::PathConfig& paths) override {
    std::error_code ec;
    if (!std::filesystem::exists(paths.config_path, ec)) return;
    rex::cvar::LoadConfig(paths.config_path);
    auto from_cvar = [](const char* name) -> std::filesystem::path {
      const std::string v = rex::cvar::GetFlagByName(name);  // UTF-8
      return std::filesystem::path(std::u8string(v.begin(), v.end()));
    };
    if (paths.game_data_root.empty()) paths.game_data_root = from_cvar("game_data_root");
    if (auto p = from_cvar("user_data_root"); !p.empty()) paths.user_data_root = p;
    if (auto p = from_cvar("cache_root"); !p.empty()) paths.cache_root = p;
    if (paths.update_data_root.empty()) paths.update_data_root = from_cvar("update_data_root");
  }
};
