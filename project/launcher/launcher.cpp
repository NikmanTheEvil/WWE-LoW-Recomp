// LoWRecomp launcher
//
// Window shown before the game: language, display, graphics, controls,
// loading, SvR 2009 import. Writes everything to lowrecomp.toml (next to
// lowrecomp.exe) and then starts the game. lowrecomp.exe reads the same file
// without the launcher as well.
//
// UI: Dear ImGui (Win32 + Direct3D 11), font Segoe UI. Language follows the
// Windows display language where a translation exists (German, see kGerman /
// Tr()), otherwise English; [low] launcher_language = "auto" | "en" | "de".
// Holding Shift at start-up shows the launcher even if "start directly next
// time" is enabled.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include "glyph_patch.h"
#include "svr_import.h"
#include "toml_edit.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

using low::launcher::TomlEdit;

// ---------------------------------------------------------------------------
// UI language: English texts in the code, German translations here.
// ---------------------------------------------------------------------------
bool g_german = false;

struct TrEntry {
  const char* en;
  const char* de;
};
// clang-format off
constexpr TrEntry kGerman[] = {
  // Lists
  {"Fullscreen (borderless)", "Vollbild (randlos)"},
  {"Fullscreen (exclusive)", "Vollbild (exklusiv)"},
  {"Window", "Fenster"},
  {"1x  (1280 x 720, original)", "1x  (1280 x 720, Original)"},
  {"2x  (2560 x 1440) - unstable", "2x  (2560 x 1440) - instabil"},
  {"3x  (3840 x 2160) - unstable", "3x  (3840 x 2160) - instabil"},
  {"4x  (5120 x 2880) - unstable", "4x  (5120 x 2880) - instabil"},
  {"Off", "Aus"},
  {"FXAA (strong)", "FXAA (stark)"},
  {"Default", "Standard"},
  {"Xenos interim renderer (playable)", "Xenos-Übergangsrenderer (spielbar)"},
  {"Native (in development, no image yet)", "Nativ (in Entwicklung, noch kein Bild)"},
  {"Keyboard", "Tastatur"},
  {"Automatic (by controller)", "Automatisch (nach Controller)"},
  {"Detailed (debug)", "Ausführlich (debug)"},
  {"Everything (trace, slow!)", "Alles (trace, langsam!)"},
  // Header, tabs, footer
  {"native PC port (static recompilation, no emulation of the game code)",
   "nativer PC-Port (statische Recompilation, keine Emulation des Spielcodes)"},
  {"Game", "Spiel"},
  {"Display", "Anzeige"},
  {"Graphics", "Grafik"},
  {"Controls", "Steuerung"},
  {"Advanced", "Erweitert"},
  {"Quit", "Beenden"},
  {"Save", "Speichern"},
  {"PLAY", "SPIELEN"},
  {"Saved.", "Gespeichert."},
  {"Could not save lowrecomp.toml.", "Konnte lowrecomp.toml nicht speichern."},
  // Game tab
  {"Game language", "Sprache des Spiels"},
  {"Applies to the game's texts, menus and tutorials.", "Gilt für Texte, Menüs und Tutorials des Spiels."},
  {"Launcher language", "Sprache des Launchers"},
  {"Automatic (Windows language)", "Automatisch (Windows-Sprache)"},
  {"Game data", "Spieldaten"},
  {"(not found)", "(nicht gefunden)"},
  {"default.xex found", "default.xex gefunden"},
  {"No game found \xE2\x80\x93 please choose the folder with default.xex.",
   "Kein Spiel gefunden \xE2\x80\x93 bitte den Ordner mit default.xex wählen."},
  {"Choose folder ...", "Ordner wählen ..."},
  {"Choose the folder with default.xex (extracted game)", "Ordner mit default.xex wählen (entpacktes Spiel)"},
  {"Loading", "Laden"},
  {"Turbo loading", "Turbo-Laden"},
  {"The game loads one file every 8 frames (like on the console). Turbo loading runs loading "
   "phases at a much higher frame rate and switches back to 60 Hz afterwards.",
   "Das Spiel lädt pro Datei im Takt von 8 Bildern (wie auf der Konsole). Turbo-Laden "
   "lässt Ladephasen mit vielfachem Bildtakt laufen und schaltet danach auf 60 Hz zurück."},
  {"Skip company logos at start-up (experimental)", "Firmenlogos beim Start überspringen (experimentell)"},
  {"Moved an earlier import to the right place - try again in the game.",
   "Früheren Import an den richtigen Ort verschoben - im Spiel erneut versuchen."},
  // Display tab
  {"Display mode", "Anzeigemodus"},
  {"Window size", "Fenstergröße"},
  {"Borderless fullscreen uses your monitor's desktop resolution; the image is scaled with the "
   "correct aspect ratio (16:9).",
   "Randloses Vollbild nutzt die Desktop-Auflösung deines Monitors; das Bild wird mit "
   "korrektem Seitenverhältnis (16:9) skaliert."},
  // Graphics tab
  {"Internal resolution", "Interne Auflösung"},
  {"The game renders internally at 720p. Higher levels render every frame in more detail - but in "
   "the current interim renderer they crash the graphics driver when a match loads (\"device "
   "removed\"). Please use 1x for now.",
   "Das Spiel rendert intern in 720p. Höhere Stufen rechnen jedes Bild feiner - im aktuellen "
   "Übergangsrenderer bringen sie den Grafiktreiber aber beim Laden eines Matches zum Absturz "
   "(\"device removed\"). Vorerst bitte 1x."},
  {"Warning: with this level the game crashes when loading matches.",
   "Achtung: Mit dieser Stufe stürzt das Spiel beim Laden von Matches ab."},
  {"60 fps mode (experimental)", "60-fps-Modus (experimentell)"},
  {"The game can switch from 60 down to 30 frames per second in some modes. The 60 fps mode "
   "prevents that. If matches seem too fast/slow or animations hang, turn it off.",
   "Das Spiel kann in manchen Modi von 60 auf 30 Bilder/s herunterschalten. Der 60-fps-Modus "
   "verhindert das. Falls Matches zu schnell/langsam wirken oder Animationen hängen, bitte abschalten."},
  {"Anti-aliasing", "Kantenglättung"},
  {"Anisotropic filtering", "Anisotrope Filterung"},
  {"These options apply to the current interim renderer.",
   "Diese Optionen gelten für den aktuellen Übergangsrenderer."},
  // Controls tab
  {"Keyboard & mouse (WWE 2K style)", "Tastatur & Maus (WWE-2K-Stil)"},
  {"Older layout (run on RT)", "Frühere Belegung (Rennen auf RT)"},
  {"All devices control player 1", "Alle Geräte steuern Spieler 1"},
  {"Button symbols", "Tastensymbole"},
  {"PlayStation symbols are not installed (run the installer again and select the PS3 version).",
   "PlayStation-Symbole sind nicht installiert (Installer erneut ausführen und die PS3-Fassung wählen)."},
  {"PlayStation 3: original symbols of the PS3 version. Keyboard: keycaps of the default layout "
   "(arrow keys, WASD, Shift, Ctrl ...). Automatic: at game start DualShock/DualSense -> "
   "PlayStation, other controller -> Xbox, none -> keyboard.",
   "PlayStation 3: Originalsymbole der PS3-Version. Tastatur: Tastenkappen der Standardbelegung "
   "(Pfeiltasten, WASD, Shift, Strg ...). Automatisch: beim Spielstart DualShock/DualSense -> "
   "PlayStation, anderer Controller -> Xbox, keiner -> Tastatur."},
  {"Turn on \"All devices control player 1\" if DS4Windows is running and a PlayStation "
   "controller would otherwise control two players at once. DS4 and DualSense also work "
   "without DS4Windows.",
   "\"Alle Geräte steuern Spieler 1\" einschalten, wenn DS4Windows läuft und ein "
   "PlayStation-Controller sonst zwei Spieler gleichzeitig steuert. DS4 und DualSense "
   "funktionieren auch ohne DS4Windows."},
  {"Keyboard & mouse", "Tastatur & Maus"},
  {"Move / run", "Bewegen / Rennen"},
  {"W A S D  /  hold Shift", "W A S D  /  Shift halten"},
  {"Arrow left  or left click", "Pfeil links  oder Linksklick"},
  {"Grapple  (away from opponent = Irish whip)", "Grapple  (weg vom Gegner = Irish Whip)"},
  {"Arrow down  or right click", "Pfeil runter  oder Rechtsklick"},
  {"Reversal / block", "Reversal / Block"},
  {"Arrow up  or middle click", "Pfeil hoch  oder Mittelklick"},
  {"Pin, climb, enter/leave ring, pick up", "Pin, Klettern, Ring rein/raus, aufheben"},
  {"Arrow right  or mouse button 4", "Pfeil rechts  oder Maustaste 4"},
  {"Finisher / signature", "Finisher / Signature"},
  {"Arrow left + down  or mouse button 5", "Pfeil links + runter  oder Maustaste 5"},
  {"Modifier (move theft: + arrow up)", "Modifikator (Move Theft: + Pfeil hoch)"},
  {"hold Ctrl", "Strg halten"},
  {"Quick grapple  /  strong grapple", "Quick Grapple  /  Strong Grapple"},
  {"Space + arrow  /  Ctrl + Space + arrow", "Leertaste + Pfeil  /  Strg + Leertaste + Pfeil"},
  {"Dodge", "Ausweichen"},
  {"Q / E + direction", "Q / E + Richtung"},
  {"Menu: move / confirm / back", "Menü: bewegen / bestätigen / zurück"},
  {"W A S D  /  Enter or arrow down  /  Backspace", "W A S D  /  Enter oder Pfeil runter  /  Backspace"},
  {"Edit bindings ...", "Belegung bearbeiten ..."},
  {"Section [low], lines bind_... ", "Abschnitt [low], Zeilen bind_... "},
  // SvR 2009 tab
  {"Import the roster from SmackDown vs. RAW 2009", "Roster aus SmackDown vs. RAW 2009 importieren"},
  {"Legends of WrestleMania can take over Superstars from a SmackDown vs. RAW 2009 save (Xbox 360). "
   "Choose the save; it is stored so that the game finds it like on the console. Then use the "
   "import option in the game.",
   "Legends of WrestleMania kann Superstars aus einem Spielstand von SmackDown vs. RAW 2009 "
   "(Xbox 360) übernehmen. Wähle dazu den Spielstand aus; er wird so abgelegt, dass das Spiel ihn "
   "wie auf der Konsole findet. Danach im Spiel die Import-Option benutzen."},
  {"Choose save file ...", "Spielstand-Datei wählen ..."},
  {"Choose the SvR 2009 save (Xbox 360 package or SaveData.dat)",
   "SvR-2009-Spielstand wählen (Xbox-360-Paket oder SaveData.dat)"},
  {"Choose a folder with a SvR 2009 save or a USB stick", "Ordner mit SvR-2009-Spielstand oder USB-Stick wählen"},
  {"File: Xbox 360 save package (CON, e.g. from a USB stick under Content\\...\\54510826\\00000001\\) "
   "or an extracted SaveData.dat. Folder: extracted save or a whole USB stick/Content folder - "
   "SvR 2009 saves inside are found automatically.",
   "Datei: Xbox-360-Spielstandpaket (CON, z. B. vom USB-Stick unter Content\\...\\54510826\\00000001\\) "
   "oder eine entpackte SaveData.dat. Ordner: entpackter Spielstand oder ein ganzer "
   "USB-Stick/Content-Ordner - SvR-2009-Spielstände darin werden automatisch gefunden."},
  {"Imported save", "Importierter Spielstand"},
  {"The game reads exactly one SvR 2009 save; a new import replaces the old one.",
   "Das Spiel liest genau einen SvR-2009-Spielstand; ein neuer Import ersetzt den alten."},
  {"None yet.", "Noch keiner."},
  {"Remove", "Entfernen"},
  {"Removed: ", "Entfernt: "},
  {"Could not remove: ", "Konnte nicht entfernen: "},
  {"Open save folder", "Spielstand-Ordner öffnen"},
  // Advanced tab
  {"The game code always runs natively. The interim renderer still translates the game's graphics "
   "commands through Xenia's GPU layer.",
   "Der Spielcode läuft immer nativ. Der Übergangsrenderer setzt die Grafikbefehle des Spiels noch "
   "über Xenias GPU-Schicht um."},
  {"Start directly next time (hold Shift to show the launcher)",
   "Beim nächsten Mal direkt starten (Shift halten zeigt den Launcher)"},
  {"Folders", "Ordner"},
  {"Save games", "Spielstände"},
  {"Program folder", "Programmordner"},
  // Messages
  {"lowrecomp.exe not found.", "lowrecomp.exe nicht gefunden."},
  {"PlayStation symbols are not installed. Run the installer again and select the PS3 version "
   "- or choose other button symbols.",
   "PlayStation-Symbole sind nicht installiert. Den Installer erneut ausführen und die PS3-Fassung "
   "wählen - oder andere Tastensymbole wählen."},
  {"Could not start the game (error ", "Spiel konnte nicht gestartet werden (Fehler "},
  {"All files", "Alle Dateien"},
};
// clang-format on

const char* Tr(const char* en) {
  if (!g_german) return en;
  for (const auto& e : kGerman)
    if (std::strcmp(e.en, en) == 0) return e.de;
  return en;
}

// Translated copy of a list of texts (for combo boxes)
template <size_t N>
std::array<const char*, N> TrList(const char* const (&items)[N]) {
  std::array<const char*, N> out{};
  for (size_t i = 0; i < N; ++i) out[i] = Tr(items[i]);
  return out;
}

// ---------------------------------------------------------------------------
// Pfade & Zeichensaetze
// ---------------------------------------------------------------------------
std::string ToUtf8(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}
std::wstring ToWide(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring w(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
  return w;
}
std::wstring ExeDir() {
  wchar_t buf[MAX_PATH * 2] = {};
  GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
  std::wstring p(buf);
  return p.substr(0, p.find_last_of(L"\\/"));
}
bool FileExists(const std::wstring& p) {
  const DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool DirExists(const std::wstring& p) {
  const DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
std::wstring Normalize(const std::wstring& p) {
  wchar_t buf[MAX_PATH * 2] = {};
  const DWORD n = GetFullPathNameW(p.c_str(), DWORD(std::size(buf)), buf, nullptr);
  return n ? std::wstring(buf, n) : p;
}
bool IsGameDir(const std::wstring& dir) { return FileExists(dir + L"\\default.xex"); }

#ifndef LOWRECOMP_VERSION
#define LOWRECOMP_VERSION "dev"
#endif

// PS3-Symbole sind optional: der Installer erzeugt sie nur, wenn die PS3-Fassung
// (Spielordner, z. B. aus RPCS3) angegeben wurde.
bool Ps3SymbolsInstalled(const std::wstring& exe_dir) {
  return FileExists(exe_dir + L"\\glyphs\\ps3_hd.bin") && FileExists(exe_dir + L"\\glyphs\\ps3_sd.bin");
}

std::wstring DocumentsDir() {
  PWSTR path = nullptr;
  std::wstring r;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &path))) r = path;
  CoTaskMemFree(path);
  return r;
}

std::wstring PickFolder(HWND owner, const std::wstring& start, const char* title = nullptr) {
  std::wstring result;
  IFileOpenDialog* dlg = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&dlg))))
    return result;
  DWORD opts = 0;
  dlg->GetOptions(&opts);
  dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  dlg->SetTitle(ToWide(Tr(title ? title : "Choose the folder with default.xex (extracted game)")).c_str());
  if (!start.empty()) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
      dlg->SetFolder(item);
      item->Release();
    }
  }
  if (SUCCEEDED(dlg->Show(owner))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dlg->GetResult(&item))) {
      PWSTR p = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
        result = p;
        CoTaskMemFree(p);
      }
      item->Release();
    }
  }
  dlg->Release();
  return result;
}

std::wstring PickFile(HWND owner, const char* title) {
  std::wstring result;
  IFileOpenDialog* dlg = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&dlg))))
    return result;
  DWORD opts = 0;
  dlg->GetOptions(&opts);
  dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
  dlg->SetTitle(ToWide(Tr(title)).c_str());
  const std::wstring all_files = ToWide(Tr("All files"));
  const COMDLG_FILTERSPEC filters[] = {{all_files.c_str(), L"*.*"}};
  dlg->SetFileTypes(1, filters);
  if (SUCCEEDED(dlg->Show(owner))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dlg->GetResult(&item))) {
      PWSTR p = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
        result = p;
        CoTaskMemFree(p);
      }
      item->Release();
    }
  }
  dlg->Release();
  return result;
}

void OpenInExplorer(const std::wstring& path) {
  CreateDirectoryW(path.c_str(), nullptr);
  ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------------------
// Einstellungen
// ---------------------------------------------------------------------------
struct Language {
  const char* name;
  int xbox_id;  // XLanguage
};
constexpr Language kLanguages[] = {
    {"English", 1}, {"Deutsch", 3}, {"Fran\xC3\xA7" "ais", 4}, {"Italiano", 6}, {"Espa\xC3\xB1ol", 5},
};
constexpr const char* kUiLanguages[] = {"Automatic (Windows language)", "English", "Deutsch"};
constexpr const char* kUiLanguageValues[] = {"auto", "en", "de"};

bool WindowsIsGerman() { return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_GERMAN; }
bool UiIsGerman(int ui_language) { return ui_language == 2 || (ui_language == 0 && WindowsIsGerman()); }

struct WindowSize {
  const char* name;
  int w, h;
};
constexpr WindowSize kWindowSizes[] = {
    {"1280 x 720", 1280, 720},   {"1600 x 900", 1600, 900},   {"1920 x 1080", 1920, 1080},
    {"2560 x 1440", 2560, 1440}, {"3840 x 2160", 3840, 2160},
};

constexpr const char* kDisplayModes[] = {"Fullscreen (borderless)", "Fullscreen (exclusive)", "Window"};
constexpr const char* kInternalRes[] = {"1x  (1280 x 720, original)", "2x  (2560 x 1440) - unstable",
                                        "3x  (3840 x 2160) - unstable", "4x  (5120 x 2880) - unstable"};
constexpr const char* kAntiAlias[] = {"Off", "FXAA", "FXAA (strong)"};
constexpr const char* kAntiAliasValues[] = {"none", "fxaa", "fxaa_extreme"};
constexpr const char* kAniso[] = {"Off", "2x", "4x", "8x", "16x"};
constexpr int kAnisoValues[] = {0, 2, 3, 4, 5};
constexpr const char* kMonitors[] = {"Default", "Monitor 1", "Monitor 2", "Monitor 3", "Monitor 4"};
constexpr const char* kRenderers[] = {"Xenos interim renderer (playable)",
                                      "Native (in development, no image yet)"};
constexpr const char* kGlyphModes[] = {"Xbox 360", "PlayStation 3", "Keyboard",
                                       "Automatic (by controller)"};
constexpr const char* kGlyphModeValues[] = {"xbox", "ps3", "keyboard", "auto"};
constexpr const char* kLogLevels[] = {"Normal", "Detailed (debug)", "Everything (trace, slow!)"};
constexpr const char* kLogLevelValues[] = {"info", "debug", "trace"};

struct Settings {
  int language = 0;     // kLanguages (game)
  int ui_language = 0;  // kUiLanguages (launcher), 0 = automatic
  std::wstring game_dir;
  int display_mode = 0;
  int monitor = 0;
  int window_size = 2;
  int internal_res = 0;
  int anti_alias = 0;
  int aniso = 2;
  bool kbm = true;
  bool swap_triggers = false;
  bool single_player_input = false;
  int glyphs = 0;  // kGlyphModes
  bool turbo_loading = true;
  bool skip_logos = false;
  bool fps60 = false;
  int renderer = 0;
  int log_level = 0;
  bool launcher_skip = false;
};

template <class T, size_t N, class Pred>
int IndexOf(const T (&arr)[N], Pred pred, int fallback) {
  for (size_t i = 0; i < N; ++i)
    if (pred(arr[i])) return int(i);
  return fallback;
}

std::wstring GuessGameDir(const std::wstring& exe_dir) {
  for (const wchar_t* rel :
       {L"\\game", L"\\..\\game", L"\\..\\..\\..\\..\\unpack", L"\\..\\..\\..\\unpack", L"\\unpack"}) {
    const std::wstring d = Normalize(exe_dir + rel);
    if (IsGameDir(d)) return d;
  }
  return {};
}

Settings FromToml(const TomlEdit& t, const std::wstring& exe_dir) {
  Settings s;
  const long lang = t.GetInt("", "user_language", 1);
  s.language = IndexOf(kLanguages, [&](const Language& l) { return l.xbox_id == lang; }, 0);
  s.game_dir = ToWide(t.GetString("", "game_data_root", ""));
  if (s.game_dir.empty() || !IsGameDir(s.game_dir)) {
    const std::wstring guess = GuessGameDir(exe_dir);
    if (!guess.empty()) s.game_dir = guess;
  }
  const bool fullscreen = t.GetBool("", "fullscreen", true);
  const bool exclusive = t.GetBool("", "fullscreen_exclusive", false);
  s.display_mode = !fullscreen ? 2 : exclusive ? 1 : 0;
  s.monitor = std::clamp(int(t.GetInt("", "monitor", 0)), 0, int(std::size(kMonitors)) - 1);
  const long ww = t.GetInt("", "window_width", 1920), wh = t.GetInt("", "window_height", 1080);
  s.window_size = IndexOf(kWindowSizes, [&](const WindowSize& w) { return w.w == ww && w.h == wh; }, 2);
  s.internal_res = std::clamp(int(t.GetInt("", "resolution_scale", 1)) - 1, 0, 3);
  const std::string aa = t.GetString("", "swap_post_effect", "none");
  s.anti_alias = IndexOf(kAntiAliasValues, [&](const char* v) { return aa == v; }, 0);
  const long an = t.GetInt("", "anisotropic_override", 3);
  s.aniso = IndexOf(kAnisoValues, [&](int v) { return v == an; }, 2);
  s.kbm = t.GetBool("low", "kbm", true);
  s.swap_triggers = t.GetBool("low", "swap_triggers", false);
  s.single_player_input = t.GetBool("low", "single_player_input", false);
  const std::string gl = t.GetString("low", "button_glyphs", "xbox");
  s.glyphs = IndexOf(kGlyphModeValues, [&](const char* v) { return gl == v; }, 0);
  s.turbo_loading = t.GetBool("low", "turbo_loading", true);
  s.skip_logos = t.GetBool("low", "skip_logos", false);
  s.fps60 = t.GetBool("low", "fps60", false);
  s.renderer = t.GetString("", "gpu_plugin", "xenos").empty() ? 1 : 0;
  const std::string ll = t.GetString("", "log_level", "info");
  s.log_level = IndexOf(kLogLevelValues, [&](const char* v) { return ll == v; }, 0);
  s.launcher_skip = t.GetBool("low", "launcher_skip", false);
  const std::string ui = t.GetString("low", "launcher_language", "auto");
  s.ui_language = IndexOf(kUiLanguageValues, [&](const char* v) { return ui == v; }, 0);
  return s;
}

void ToToml(const Settings& s, TomlEdit& t, const std::wstring& exe_dir) {
  t.SetInt("", "user_language", kLanguages[s.language].xbox_id);
  if (!s.game_dir.empty()) t.SetString("", "game_data_root", ToUtf8(s.game_dir));
  t.SetString("", "gpu_plugin", s.renderer == 0 ? "xenos" : "");
  t.SetBool("", "fullscreen", s.display_mode != 2);
  t.SetBool("", "fullscreen_exclusive", s.display_mode == 1);
  t.SetInt("", "monitor", s.monitor);
  t.SetInt("", "window_width", kWindowSizes[s.window_size].w);
  t.SetInt("", "window_height", kWindowSizes[s.window_size].h);
  // The game itself always renders at 720p; sharper via the internal
  // resolution (resolution_scale).
  t.SetInt("", "video_mode_width", 1280);
  t.SetInt("", "video_mode_height", 720);
  t.SetInt("", "resolution_scale", s.internal_res + 1);
  t.SetString("", "swap_post_effect", kAntiAliasValues[s.anti_alias]);
  t.SetInt("", "anisotropic_override", kAnisoValues[s.aniso]);
  // The game's frame pacing: must stay on, otherwise the game runs too fast.
  // (Turbo loading only switches it off briefly during loading phases.)
  t.SetBool("", "vsync", true);
  t.SetString("", "log_level", kLogLevelValues[s.log_level]);
  t.SetString("", "log_file", ToUtf8(exe_dir + L"\\logs\\lowrecomp.log"));
  t.SetBool("low", "kbm", s.kbm);
  t.SetBool("low", "swap_triggers", s.swap_triggers);
  t.SetBool("low", "single_player_input", s.single_player_input);
  t.SetString("low", "button_glyphs", kGlyphModeValues[s.glyphs]);
  t.SetBool("low", "turbo_loading", s.turbo_loading);
  t.SetBool("low", "skip_logos", s.skip_logos);
  t.SetBool("low", "fps60", s.fps60);
  t.SetBool("low", "launcher_skip", s.launcher_skip);
  t.SetString("low", "launcher_language", kUiLanguageValues[s.ui_language]);
}

std::string ReadFileUtf8(const std::wstring& path) {
  std::ifstream f(std::filesystem::path(path), std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
bool WriteFileUtf8(const std::wstring& path, const std::string& text) {
  std::ofstream f(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
  f.write(text.data(), std::streamsize(text.size()));
  return bool(f);
}

bool LaunchGame(const std::wstring& exe_dir, const std::wstring& game_dir, int glyphs,
                std::string& error) {
  const std::wstring exe = exe_dir + L"\\lowrecomp.exe";
  if (!FileExists(exe)) {
    error = Tr("lowrecomp.exe not found.");
    return false;
  }
  {
    // Button symbols: provide copies with PS3 / keyboard symbols (only the first
    // time or after changes, takes under a second). "Automatic" needs both.
    namespace lw = low::launcher;
    const std::filesystem::path gd(game_dir), ed(exe_dir);
    const std::string mode = kGlyphModeValues[glyphs];
    std::string glyph_error;
    bool ok = true;
    const bool ps3_ok = Ps3SymbolsInstalled(exe_dir);
    if (mode == "ps3" && !ps3_ok) {
      error = Tr("PlayStation symbols are not installed. Run the installer again and select the PS3 version "
                 "- or choose other button symbols.");
      return false;
    }
    if ((mode == "ps3" || mode == "auto") && ps3_ok)
      ok = ok && lw::EnsureGlyphVariant(gd, ed, lw::kPs3Variant, glyph_error);
    if (mode == "keyboard" || mode == "auto")
      ok = ok && lw::EnsureGlyphVariant(gd, ed, lw::kKeyboardVariant, glyph_error);
    if (!ok) {
      error = glyph_error;
      return false;
    }
  }

  CreateDirectoryW((exe_dir + L"\\logs").c_str(), nullptr);
  // Also pass the game folder directly (in case the configuration cannot be
  // read). A trailing backslash would escape the closing quote - remove it.
  std::wstring dir = game_dir;
  while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
  std::wstring cmd = L"\"" + exe + L"\"";
  if (!dir.empty()) cmd += L" --game_data_root \"" + dir + L"\"";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      exe_dir.c_str(), &si, &pi)) {
    error = Tr("Could not start the game (error ") + std::to_string(GetLastError()) + ").";
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

// ---------------------------------------------------------------------------
// Direct3D 11 (nach dem ImGui-Beispiel example_win32_directx11)
// ---------------------------------------------------------------------------
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapchain = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
bool g_occluded = false;

void CreateRenderTarget() {
  ID3D11Texture2D* back = nullptr;
  g_swapchain->GetBuffer(0, IID_PPV_ARGS(&back));
  if (back) {
    g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
  }
}
void CleanupRenderTarget() {
  if (g_rtv) {
    g_rtv->Release();
    g_rtv = nullptr;
  }
}
bool CreateDeviceD3D(HWND hwnd) {
  DXGI_SWAP_CHAIN_DESC sd{};
  sd.BufferCount = 2;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL got;
  HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                             D3D11_SDK_VERSION, &sd, &g_swapchain, &g_device, &got,
                                             &g_context);
  if (hr == DXGI_ERROR_UNSUPPORTED)
    hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
                                       D3D11_SDK_VERSION, &sd, &g_swapchain, &g_device, &got,
                                       &g_context);
  if (hr != S_OK) return false;
  CreateRenderTarget();
  return true;
}
void CleanupDeviceD3D() {
  CleanupRenderTarget();
  if (g_swapchain) g_swapchain->Release();
  if (g_context) g_context->Release();
  if (g_device) g_device->Release();
  g_swapchain = nullptr;
  g_context = nullptr;
  g_device = nullptr;
}

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return true;
  switch (msg) {
    case WM_SYSCOMMAND:
      if ((wparam & 0xfff0) == SC_KEYMENU) return 0;
      break;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

// ---------------------------------------------------------------------------
// Stil
// ---------------------------------------------------------------------------
void ApplyStyle(float scale) {
  ImGuiStyle& st = ImGui::GetStyle();
  ImGui::StyleColorsDark(&st);
  st.WindowRounding = 0.0f;
  st.FrameRounding = 4.0f;
  st.GrabRounding = 4.0f;
  st.TabRounding = 4.0f;
  st.FramePadding = ImVec2(10, 6);
  st.ItemSpacing = ImVec2(10, 8);
  st.WindowPadding = ImVec2(22, 18);
  ImVec4* c = st.Colors;
  const ImVec4 accent(0.78f, 0.12f, 0.14f, 1.0f);        // WWE-Rot
  const ImVec4 accent_hover(0.90f, 0.20f, 0.22f, 1.0f);
  const ImVec4 gold(0.86f, 0.70f, 0.30f, 1.0f);
  c[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.07f, 0.08f, 1.0f);
  c[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.14f, 0.16f, 1.0f);
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.23f, 1.0f);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.24f, 0.27f, 1.0f);
  c[ImGuiCol_Button] = ImVec4(0.18f, 0.18f, 0.20f, 1.0f);
  c[ImGuiCol_ButtonHovered] = accent_hover;
  c[ImGuiCol_ButtonActive] = accent;
  c[ImGuiCol_Header] = ImVec4(0.30f, 0.10f, 0.11f, 1.0f);
  c[ImGuiCol_HeaderHovered] = accent_hover;
  c[ImGuiCol_HeaderActive] = accent;
  c[ImGuiCol_Tab] = ImVec4(0.14f, 0.14f, 0.16f, 1.0f);
  c[ImGuiCol_TabHovered] = accent_hover;
  c[ImGuiCol_TabSelected] = accent;
  c[ImGuiCol_CheckMark] = gold;
  c[ImGuiCol_SliderGrab] = gold;
  c[ImGuiCol_SeparatorHovered] = accent_hover;
  c[ImGuiCol_TextLink] = gold;
  st.ScaleAllSizes(scale);
}

bool Combo(const char* label, int* value, const char* const* items, int count) {
  label = Tr(label);
  ImGui::TextUnformatted(label);
  ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.40f);
  ImGui::SetNextItemWidth(-1);
  ImGui::PushID(label);
  std::vector<const char*> translated(items, items + count);
  for (auto& t : translated) t = Tr(t);
  const bool r = ImGui::Combo("##c", value, translated.data(), count);
  ImGui::PopID();
  return r;
}

void Hint(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.62f, 0.66f, 1.0f));
  ImGui::TextWrapped("%s", Tr(text));
  ImGui::PopStyleColor();
}

// Tab label with a stable ID (the selected tab stays when the language changes)
std::string Tab(const char* en, const char* id) {
  return std::string("  ") + Tr(en) + "  ###" + id;
}

}  // namespace

// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

  const std::wstring exe_dir = ExeDir();
  const std::wstring config_path = exe_dir + L"\\lowrecomp.toml";
  TomlEdit toml;
  toml.Load(ReadFileUtf8(config_path));
  Settings settings = FromToml(toml, exe_dir);
  g_german = UiIsGerman(settings.ui_language);
  // Save games (like ReXGlue: user_data_root, otherwise Documents\lowrecomp)
  std::wstring user_data_root = ToWide(toml.GetString("", "user_data_root", ""));
  if (user_data_root.empty()) user_data_root = DocumentsDir() + L"\\lowrecomp";
  std::string svr_status;
  bool svr_status_error = false;
  // Imports of an earlier launcher version were in the wrong place -> move them
  if (low::launcher::MigrateLegacyImports(std::filesystem::path(user_data_root)) > 0)
    svr_status = Tr("Moved an earlier import to the right place - try again in the game.");
  std::vector<low::launcher::ImportedSave> svr_saves =
      low::launcher::ListImported(std::filesystem::path(user_data_root));

  // Start directly if wanted (holding Shift still shows the launcher).
  if (settings.launcher_skip && !(GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
      IsGameDir(settings.game_dir)) {
    std::string err;
    if (LaunchGame(exe_dir, settings.game_dir, settings.glyphs, err)) return 0;
  }

  ImGui_ImplWin32_EnableDpiAwareness();
  const float scale =
      ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

  WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc, 0, 0, instance, [instance] {
                      // Program icon from the resources (resources/app.rc.in), else the Windows default
                      HICON icon = LoadIconW(instance, MAKEINTRESOURCEW(1));
                      return icon ? icon : LoadIconW(nullptr, MAKEINTRESOURCEW(32512) /* IDI_APPLICATION */);
                    }(),
                    LoadCursorW(nullptr, MAKEINTRESOURCEW(32512) /* IDC_ARROW */), nullptr, nullptr, L"LoWRecompLauncher", nullptr};
  RegisterClassExW(&wc);
  const int width = int(860 * scale), height = int(600 * scale);
  RECT rc{0, 0, width, height};
  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  AdjustWindowRect(&rc, style, FALSE);
  const int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
  HWND hwnd = CreateWindowW(wc.lpszClassName, L"LoWRecomp – WWE Legends of WrestleMania", style,
                            (sw - (rc.right - rc.left)) / 2, (sh - (rc.bottom - rc.top)) / 2,
                            rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, instance, nullptr);
  if (!CreateDeviceD3D(hwnd)) {
    CleanupDeviceD3D();
    MessageBoxW(nullptr, L"Direct3D 11 could not be started.", L"LoWRecomp", MB_ICONERROR);
    return 1;
  }
  ShowWindow(hwnd, SW_SHOWDEFAULT);
  UpdateWindow(hwnd);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // no imgui.ini next to the exe
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  ApplyStyle(scale);
  ImGui::GetStyle().FontScaleDpi = scale;

  ImFont* font = nullptr;
  ImFont* font_big = nullptr;
  wchar_t windir[MAX_PATH] = {};
  GetWindowsDirectoryW(windir, MAX_PATH);
  const std::string fonts = ToUtf8(std::wstring(windir) + L"\\Fonts\\");
  if (FileExists(ToWide(fonts + "segoeui.ttf"))) font = io.Fonts->AddFontFromFileTTF((fonts + "segoeui.ttf").c_str(), 17.0f);
  if (FileExists(ToWide(fonts + "segoeuib.ttf"))) font_big = io.Fonts->AddFontFromFileTTF((fonts + "segoeuib.ttf").c_str(), 17.0f);
  if (!font) font = io.Fonts->AddFontDefault();
  if (!font_big) font_big = font;

  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX11_Init(g_device, g_context);

  std::string status;
  bool status_error = false;
  bool done = false;
  bool launch_requested = false;

  auto save = [&]() -> bool {
    ToToml(settings, toml, exe_dir);
    if (!WriteFileUtf8(config_path, toml.Text())) {
      status = Tr("Could not save lowrecomp.toml.");
      status_error = true;
      return false;
    }
    status = Tr("Saved.");
    status_error = false;
    return true;
  };

  while (!done) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
      if (msg.message == WM_QUIT) done = true;
    }
    if (done) break;
    if (g_occluded && g_swapchain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
      Sleep(10);
      continue;
    }
    g_occluded = false;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##launcher", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Header
    ImGui::PushFont(font_big, 30.0f);
    ImGui::TextColored(ImVec4(0.90f, 0.20f, 0.22f, 1.0f), "WWE LEGENDS OF WRESTLEMANIA");
    ImGui::PopFont();
    Hint((std::string("LoWRecomp " LOWRECOMP_VERSION " \xE2\x80\x93 ") +
          Tr("native PC port (static recompilation, no emulation of the game code)")).c_str());
    ImGui::Spacing();

    const bool game_ok = IsGameDir(settings.game_dir);
    const float footer = ImGui::GetFrameHeightWithSpacing() * 2.2f;

    ImGui::BeginChild("##tabs", ImVec2(0, -footer), ImGuiChildFlags_None);
    if (ImGui::BeginTabBar("##tabbar")) {
      // --- Game ----------------------------------------------------------
      if (ImGui::BeginTabItem(Tab("Game", "game").c_str())) {
        ImGui::Spacing();
        const char* langs[std::size(kLanguages)];
        for (size_t i = 0; i < std::size(kLanguages); ++i) langs[i] = kLanguages[i].name;
        Combo("Game language", &settings.language, langs, int(std::size(langs)));
        Hint("Applies to the game's texts, menus and tutorials.");
        if (Combo("Launcher language", &settings.ui_language, kUiLanguages, int(std::size(kUiLanguages))))
          g_german = UiIsGerman(settings.ui_language);
        ImGui::Spacing();

        ImGui::SeparatorText(Tr("Game data"));
        const std::string dir = settings.game_dir.empty() ? Tr("(not found)") : ToUtf8(settings.game_dir);
        ImGui::TextWrapped("%s", dir.c_str());
        if (game_ok) {
          ImGui::TextColored(ImVec4(0.45f, 0.80f, 0.45f, 1.0f), "%s", Tr("default.xex found"));
        } else {
          ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), "%s",
                             Tr("No game found \xE2\x80\x93 please choose the folder with default.xex."));
        }
        if (ImGui::Button(Tr("Choose folder ..."))) {
          const std::wstring picked = PickFolder(hwnd, settings.game_dir);
          if (!picked.empty()) settings.game_dir = picked;
        }

        ImGui::SeparatorText(Tr("Loading"));
        ImGui::Checkbox(Tr("Turbo loading"), &settings.turbo_loading);
        Hint("The game loads one file every 8 frames (like on the console). Turbo loading runs loading "
             "phases at a much higher frame rate and switches back to 60 Hz afterwards.");
        ImGui::Checkbox(Tr("Skip company logos at start-up (experimental)"), &settings.skip_logos);
        ImGui::EndTabItem();
      }

      // --- Display -------------------------------------------------------
      if (ImGui::BeginTabItem(Tab("Display", "display").c_str())) {
        ImGui::Spacing();
        Combo("Display mode", &settings.display_mode, kDisplayModes, int(std::size(kDisplayModes)));
        Combo("Monitor", &settings.monitor, kMonitors, int(std::size(kMonitors)));
        ImGui::BeginDisabled(settings.display_mode != 2);
        const char* sizes[std::size(kWindowSizes)];
        for (size_t i = 0; i < std::size(kWindowSizes); ++i) sizes[i] = kWindowSizes[i].name;
        Combo("Window size", &settings.window_size, sizes, int(std::size(sizes)));
        ImGui::EndDisabled();
        Hint("Borderless fullscreen uses your monitor's desktop resolution; the image is scaled with the "
             "correct aspect ratio (16:9).");
        ImGui::EndTabItem();
      }

      // --- Graphics ------------------------------------------------------
      if (ImGui::BeginTabItem(Tab("Graphics", "graphics").c_str())) {
        ImGui::Spacing();
        Combo("Internal resolution", &settings.internal_res, kInternalRes, int(std::size(kInternalRes)));
        Hint("The game renders internally at 720p. Higher levels render every frame in more detail - but in "
             "the current interim renderer they crash the graphics driver when a match loads (\"device "
             "removed\"). Please use 1x for now.");
        if (settings.internal_res > 0)
          ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f), "%s",
                             Tr("Warning: with this level the game crashes when loading matches."));
        ImGui::Checkbox(Tr("60 fps mode (experimental)"), &settings.fps60);
        Hint("The game can switch from 60 down to 30 frames per second in some modes. The 60 fps mode "
             "prevents that. If matches seem too fast/slow or animations hang, turn it off.");
        Combo("Anti-aliasing", &settings.anti_alias, kAntiAlias, int(std::size(kAntiAlias)));
        Combo("Anisotropic filtering", &settings.aniso, kAniso, int(std::size(kAniso)));
        ImGui::Spacing();
        Hint("These options apply to the current interim renderer.");
        ImGui::EndTabItem();
      }

      // --- Controls ------------------------------------------------------
      if (ImGui::BeginTabItem(Tab("Controls", "controls").c_str())) {
        ImGui::Spacing();
        ImGui::Checkbox(Tr("Keyboard & mouse (WWE 2K style)"), &settings.kbm);
        ImGui::Checkbox(Tr("Older layout (run on RT)"), &settings.swap_triggers);
        ImGui::Checkbox(Tr("All devices control player 1"), &settings.single_player_input);
        Combo("Button symbols", &settings.glyphs, kGlyphModes, int(std::size(kGlyphModes)));
        if (!Ps3SymbolsInstalled(exe_dir) && (kGlyphModeValues[settings.glyphs] == std::string("ps3")))
          ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f), "%s",
                             Tr("PlayStation symbols are not installed (run the installer again and select the PS3 version)."));
        Hint("PlayStation 3: original symbols of the PS3 version. Keyboard: keycaps of the default layout "
             "(arrow keys, WASD, Shift, Ctrl ...). Automatic: at game start DualShock/DualSense -> "
             "PlayStation, other controller -> Xbox, none -> keyboard.");
        Hint("Turn on \"All devices control player 1\" if DS4Windows is running and a PlayStation "
             "controller would otherwise control two players at once. DS4 and DualSense also work "
             "without DS4Windows.");
        ImGui::SeparatorText(Tr("Keyboard & mouse"));
        if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
          const char* rows[][2] = {
              {"Move / run", "W A S D  /  hold Shift"},
              {"Strike", "Arrow left  or left click"},
              {"Grapple  (away from opponent = Irish whip)", "Arrow down  or right click"},
              {"Reversal / block", "Arrow up  or middle click"},
              {"Pin, climb, enter/leave ring, pick up", "Arrow right  or mouse button 4"},
              {"Finisher / signature", "Arrow left + down  or mouse button 5"},
              {"Taunt", "2"},
              {"Modifier (move theft: + arrow up)", "hold Ctrl"},
              {"Quick grapple  /  strong grapple", "Space + arrow  /  Ctrl + Space + arrow"},
              {"Dodge", "Q / E + direction"},
              {"Menu: move / confirm / back", "W A S D  /  Enter or arrow down  /  Backspace"},
              {"Pause", "Esc"},
          };
          for (auto& r : rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(Tr(r[0]));
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ImVec4(0.86f, 0.70f, 0.30f, 1.0f), "%s", Tr(r[1]));
          }
          ImGui::EndTable();
        }
        if (ImGui::Button(Tr("Edit bindings ..."))) {
          save();
          ShellExecuteW(hwnd, L"open", L"notepad.exe", (L"\"" + config_path + L"\"").c_str(), nullptr,
                        SW_SHOWNORMAL);
        }
        ImGui::SameLine();
        Hint("Section [low], lines bind_... ");
        ImGui::EndTabItem();
      }

      // --- SvR 2009 -------------------------------------------------------
      if (ImGui::BeginTabItem("  SvR 2009  ###svr")) {
        namespace lw = low::launcher;
        const std::filesystem::path udr(user_data_root);
        ImGui::Spacing();
        ImGui::TextWrapped("%s", Tr("Import the roster from SmackDown vs. RAW 2009"));
        Hint("Legends of WrestleMania can take over Superstars from a SmackDown vs. RAW 2009 save (Xbox 360). "
             "Choose the save; it is stored so that the game finds it like on the console. Then use the "
             "import option in the game.");
        ImGui::Spacing();
        auto do_import = [&](const std::wstring& src) {
          if (src.empty()) return;
          const auto r = lw::ImportSvr2009(std::filesystem::path(src), udr);
          svr_status = r.message;
          svr_status_error = !r.ok;
          svr_saves = lw::ListImported(udr);
        };
        if (ImGui::Button(Tr("Choose save file ...")))
          do_import(PickFile(hwnd, "Choose the SvR 2009 save (Xbox 360 package or SaveData.dat)"));
        ImGui::SameLine();
        if (ImGui::Button(Tr("Choose folder ...")))
          do_import(PickFolder(hwnd, L"", "Choose a folder with a SvR 2009 save or a USB stick"));
        Hint("File: Xbox 360 save package (CON, e.g. from a USB stick under Content\\...\\54510826\\00000001\\) "
             "or an extracted SaveData.dat. Folder: extracted save or a whole USB stick/Content folder - "
             "SvR 2009 saves inside are found automatically.");
        if (!svr_status.empty()) {
          ImGui::TextColored(svr_status_error ? ImVec4(0.95f, 0.45f, 0.35f, 1.0f)
                                              : ImVec4(0.45f, 0.80f, 0.45f, 1.0f),
                             "%s", svr_status.c_str());
        }
        ImGui::SeparatorText(Tr("Imported save"));
        Hint("The game reads exactly one SvR 2009 save; a new import replaces the old one.");
        if (svr_saves.empty()) {
          Hint("None yet.");
        } else if (ImGui::BeginTable("##svr", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
          std::string remove;
          for (const auto& sv : svr_saves) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(sv.display_name.empty() ? "SmackDown vs. RAW 2009" : sv.display_name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ImVec4(0.62f, 0.62f, 0.66f, 1.0f), "%s", sv.package_name.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::PushID(sv.package_name.c_str());
            if (ImGui::SmallButton(Tr("Remove"))) remove = sv.package_name;
            ImGui::PopID();
          }
          ImGui::EndTable();
          if (!remove.empty()) {
            const bool ok = lw::RemoveImported(udr, remove);
            svr_status = (ok ? Tr("Removed: ") : Tr("Could not remove: ")) + remove;
            svr_status_error = !ok;
            svr_saves = lw::ListImported(udr);
          }
        }
        if (ImGui::Button(Tr("Open save folder"))) {
          std::error_code ec;
          std::filesystem::create_directories(lw::SvrPackagesDir(udr), ec);
          OpenInExplorer(lw::SvrPackagesDir(udr).wstring());
        }
        ImGui::EndTabItem();
      }

      // --- Advanced ------------------------------------------------------
      if (ImGui::BeginTabItem(Tab("Advanced", "advanced").c_str())) {
        ImGui::Spacing();
        Combo("Renderer", &settings.renderer, kRenderers, int(std::size(kRenderers)));
        Hint("The game code always runs natively. The interim renderer still translates the game's graphics "
             "commands through Xenia's GPU layer.");
        Combo("Log", &settings.log_level, kLogLevels, int(std::size(kLogLevels)));
        ImGui::Checkbox(Tr("Start directly next time (hold Shift to show the launcher)"), &settings.launcher_skip);
        ImGui::SeparatorText(Tr("Folders"));
        if (ImGui::Button(Tr("Save games"))) OpenInExplorer(user_data_root);
        ImGui::SameLine();
        if (ImGui::Button("Logs")) OpenInExplorer(exe_dir + L"\\logs");
        ImGui::SameLine();
        if (ImGui::Button(Tr("Program folder"))) OpenInExplorer(exe_dir);
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    ImGui::EndChild();

    // Footer
    ImGui::Separator();
    if (!status.empty()) {
      ImGui::TextColored(status_error ? ImVec4(0.95f, 0.45f, 0.35f, 1.0f)
                                      : ImVec4(0.62f, 0.62f, 0.66f, 1.0f),
                         "%s", status.c_str());
      ImGui::SameLine();
    }
    const float bw = 150.0f * scale, bh = ImGui::GetFrameHeight() * 1.3f;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - bw * 2.6f -
                         ImGui::GetStyle().ItemSpacing.x * 2);
    if (ImGui::Button((std::string(Tr("Quit")) + "###quit").c_str(), ImVec2(bw * 0.8f, bh))) done = true;
    ImGui::SameLine();
    if (ImGui::Button((std::string(Tr("Save")) + "###save").c_str(), ImVec2(bw * 0.8f, bh))) save();
    ImGui::SameLine();
    ImGui::BeginDisabled(!game_ok);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.78f, 0.12f, 0.14f, 1.0f));
    ImGui::PushFont(font_big, 0.0f);
    if (ImGui::Button((std::string(Tr("PLAY")) + "###play").c_str(), ImVec2(bw, bh))) launch_requested = true;
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::EndDisabled();

    ImGui::End();

    if (launch_requested) {
      launch_requested = false;
      if (save()) {
        std::string err;
        if (LaunchGame(exe_dir, settings.game_dir, settings.glyphs, err)) {
          done = true;
        } else {
          status = err;
          status_error = true;
        }
      }
    }

    ImGui::Render();
    const float clear[4] = {0.07f, 0.07f, 0.08f, 1.0f};
    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_context->ClearRenderTargetView(g_rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    const HRESULT hr = g_swapchain->Present(1, 0);
    g_occluded = (hr == DXGI_STATUS_OCCLUDED);
  }

  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  CleanupDeviceD3D();
  DestroyWindow(hwnd);
  UnregisterClassW(wc.lpszClassName, wc.hInstance);
  CoUninitialize();
  return 0;
}
