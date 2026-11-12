// LoWRecomp - Systemanpassungen
//
// 1. Sprache: ReXGlues XGetLanguage meldet immer Englisch. Wir geben die
//    Sprache aus der CVar user_language zurueck (Launcher: Sprache).
//
// 2. Turbo-Laden: Die Analyse (Lauf 6) zeigte, dass das Spiel beim Laden pro
//    Datei einen festen Takt von 8 Bildern braucht (133 ms bei 60 fps) - die
//    Lesezugriffe selbst sind auf der SSD sofort fertig. Waehrend eine
//    Ladephase laeuft (.pac/.arc-Zugriffe), stellen wir den Bildtakt des
//    Spiels von 60 Hz auf 1000 Hz (CVar vsync=false im GPU-Teil) und danach
//    wieder zurueck, damit das Spieltempo stimmt.
//
// 3. Ladezeit-Messung: jede Ladephase wird mit Dateien, Datenmenge und Dauer
//    ins Log geschrieben ("LoW-Laden: ...").
//
// 4. Optional: Firmenlogos beim Start ueberspringen (movies\logo.bik).
//
// 5. Bildrate: Das Spiel kennt selbst 60 und 30 Bilder/s (Global 0x82B50D10,
//    Zeitkonstanten daneben). Menues laufen mit 60, fuer Matches schaltet es
//    per 0x8235EB90 auf 30 herunter (Praesentationsintervall 2). Der 60-fps-
//    Modus (CVar low_fps60) laesst diese Funktion leer zurueckkehren (Midasm-
//    Hook, config\default_codegen.toml). Zusaetzlich wird die tatsaechliche
//    Bildrate regelmaessig ins Log geschrieben ("LoW-FPS: ...").
//
// Technik: Die uebersetzten Spielaufrufe springen die Symbole __imp__Nt*
// direkt an. Eine eigene Definition hier hat beim Linken Vorrang vor der
// Import-Bibliothek von rexruntime.dll; das Original holen wir uns per
// GetProcAddress aus der DLL und rufen es weiter auf.

#include <cctype>
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>

#include "low_input.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#endif
#include <filesystem>
#include <unordered_map>

REXCVAR_DEFINE_BOOL(low_turbo_loading, true, "LoW/System",
                    "Ladephasen mit erhoehtem Bildtakt durchlaufen (Turbo-Laden)");
REXCVAR_DEFINE_INT32(low_turbo_hold_ms, 250, "LoW/System",
                     "So lange nach dem letzten Dateizugriff bleibt Turbo-Laden aktiv (ms)")
    .range(50, 5000);
REXCVAR_DEFINE_BOOL(low_skip_logos, false, "LoW/System",
                    "Firmenlogos beim Start ueberspringen (experimentell)");
REXCVAR_DEFINE_STRING(low_button_glyphs, "xbox", "LoW/System",
                      "Tastensymbole im Spiel: xbox, ps3, keyboard oder auto (nach Controller)");
REXCVAR_DEFINE_BOOL(low_fps60, false, "LoW/System",
                    "60-fps-Modus: Matches nicht auf 30 Bilder/s herunterschalten (experimentell)");
REXCVAR_DEFINE_STRING(low_launcher_language, "auto", "LoW/System",
                      "Language of the launcher (auto, en, de) - only read by LoWLauncher.exe");
REXCVAR_DEFINE_BOOL(low_auto_storage, true, "LoW/System",
                    "Beim Start nicht nach dem Speichergeraet fragen, sondern es direkt waehlen");
REXCVAR_DEFINE_BOOL(low_launcher_skip, false, "LoW/System",
                    "Launcher ueberspringen und direkt starten (wird nur vom Launcher gelesen)");

namespace low {
namespace {

using PPCFn = void (*)(PPCContext&, uint8_t*);

// Gast-Speicher (fuer Midasm-Hooks, die nur Register bekommen). Wird von den
// ueberschriebenen Systemfunktionen gesetzt - die laufen lange vor den Hooks.
std::atomic<uint8_t*> g_guest_base{nullptr};

// ---------------------------------------------------------------------------
// Hilfen
// ---------------------------------------------------------------------------
uint32_t LoadBE32(const uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, 4);
  return std::byteswap(v);
}
uint16_t LoadBE16(const uint8_t* base, uint32_t addr) {
  uint16_t v;
  std::memcpy(&v, base + addr, 2);
  return std::byteswap(v);
}
void StoreBE32(uint8_t* base, uint32_t addr, uint32_t value) {
  value = std::byteswap(value);
  std::memcpy(base + addr, &value, 4);
}

// Pfad aus X_OBJECT_ATTRIBUTES (Gast-Adresse) lesen, klein geschrieben.
std::string GuestPath(const uint8_t* base, uint32_t object_attributes) {
  if (!object_attributes) return {};
  const uint32_t name_ptr = LoadBE32(base, object_attributes + 4);  // PANSI_STRING
  if (!name_ptr) return {};
  const uint16_t length = LoadBE16(base, name_ptr);
  const uint32_t buffer = LoadBE32(base, name_ptr + 4);
  if (!buffer || !length) return {};
  std::string path(reinterpret_cast<const char*>(base + buffer), std::min<uint16_t>(length, 512));
  std::transform(path.begin(), path.end(), path.begin(),
                 [](char c) { return char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
  std::replace(path.begin(), path.end(), '/', '\\');
  return path;
}

bool EndsWith(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

// Ladedateien: Archive des Spiels. Musik (.yaf) und Videos (.bik) werden auch
// waehrend des Spielens gestreamt und zaehlen deshalb nicht.
bool IsLoadFile(std::string_view path) {
  return EndsWith(path, ".pac") || EndsWith(path, ".arc");
}

// ---------------------------------------------------------------------------
// Weiterleitung an die Originale in rexruntime.dll
// ---------------------------------------------------------------------------
PPCFn Original(const char* name) {
#if defined(_WIN32)
  static HMODULE runtime = [] {
    for (const wchar_t* candidate : {L"rexruntime.dll", L"rexruntimerd.dll", L"rexruntimed.dll"}) {
      if (HMODULE h = GetModuleHandleW(candidate)) return h;
    }
    return HMODULE(nullptr);
  }();
  if (!runtime) return nullptr;
  return reinterpret_cast<PPCFn>(reinterpret_cast<void*>(GetProcAddress(runtime, name)));
#else
  (void)name;
  return nullptr;
#endif
}

struct Originals {
  PPCFn create_file = nullptr;
  PPCFn open_file = nullptr;
  PPCFn read_file = nullptr;
  PPCFn read_file_scatter = nullptr;
  PPCFn close = nullptr;
  PPCFn write_file = nullptr;
  bool ok = false;
};

const Originals& Orig() {
  static const Originals o = [] {
    Originals r;
    r.create_file = Original("__imp__NtCreateFile");
    r.open_file = Original("__imp__NtOpenFile");
    r.read_file = Original("__imp__NtReadFile");
    r.read_file_scatter = Original("__imp__NtReadFileScatter");
    r.close = Original("__imp__NtClose");
    r.write_file = Original("__imp__NtWriteFile");
    r.ok = r.create_file && r.open_file && r.read_file && r.read_file_scatter && r.close &&
           r.write_file;
    if (r.ok) {
      REXLOG_INFO("LoW-System: Datei-Hooks aktiv (Weiterleitung an rexruntime.dll)");
    } else {
      REXLOG_ERROR("LoW-System: Originalfunktionen in rexruntime.dll nicht gefunden - "
                   "Dateizugriffe werden fehlschlagen!");
    }
    return r;
  }();
  return o;
}

constexpr uint32_t kStatusUnsuccessful = 0xC0000001;
constexpr uint32_t kStatusObjectNameNotFound = 0xC0000034;

void Forward(PPCFn fn, PPCContext& ctx, uint8_t* base) {
  if (fn) {
    fn(ctx, base);
  } else {
    ctx.r3.u64 = kStatusUnsuccessful;
  }
}

// ---------------------------------------------------------------------------
// Ladephasen-Erkennung + Turbo
// ---------------------------------------------------------------------------
class LoadMonitor {
 public:
  static LoadMonitor& Get() {
    static LoadMonitor m;
    return m;
  }

  void OnLoadFileOpened(uint32_t handle) {
    {
      std::lock_guard lock(mutex_);
      handles_.insert(handle);
      ++phase_files_;
    }
    Touch();
  }

  void OnRead(uint32_t handle, uint32_t bytes) {
    {
      std::lock_guard lock(mutex_);
      if (!handles_.count(handle)) return;
      phase_bytes_ += bytes;
    }
    Touch();
  }

  void OnClose(uint32_t handle) {
    std::lock_guard lock(mutex_);
    handles_.erase(handle);
  }

 private:
  using Clock = std::chrono::steady_clock;

  static int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               Clock::now().time_since_epoch())
        .count();
  }

  void Touch() {
    last_activity_ms_.store(NowMs(), std::memory_order_relaxed);
    std::call_once(thread_once_, [this] { std::thread([this] { Run(); }).detach(); });
    bool expected = false;
    if (active_.compare_exchange_strong(expected, true)) {
      std::lock_guard lock(phase_mutex_);
      BeginPhase();
    }
  }

  void BeginPhase() {
    phase_start_ms_ = NowMs();
    turbo_ = false;
    if (REXCVAR_GET(low_turbo_loading) && turbo_available_) {
      saved_vsync_ = rex::cvar::GetFlagByName("vsync");
      if (saved_vsync_.empty()) {
        // Kein GPU-Teil geladen (nativer Modus) - es gibt keinen Bildtakt zu aendern.
        turbo_available_ = false;
      } else if (saved_vsync_ == "true" && rex::cvar::SetFlagByName("vsync", "false")) {
        turbo_ = true;
      }
    }
  }

  void EndPhase() {
    if (turbo_) {
      rex::cvar::SetFlagByName("vsync", saved_vsync_.empty() ? "true" : saved_vsync_);
    }
    uint64_t files, bytes;
    {
      std::lock_guard lock(mutex_);
      files = phase_files_;
      bytes = phase_bytes_;
      phase_files_ = 0;
      phase_bytes_ = 0;
    }
    const int64_t hold = REXCVAR_GET(low_turbo_hold_ms);
    const double seconds = double(std::max<int64_t>(0, NowMs() - phase_start_ms_ - hold)) / 1000.0;
    if (files > 0 || bytes > (1u << 20)) {
      REXLOG_INFO("LoW-Laden: {} Dateien, {:.1f} MB in {:.2f} s (Turbo {})", files,
                  double(bytes) / (1024.0 * 1024.0), seconds, turbo_ ? "an" : "aus");
    }
    turbo_ = false;
  }

  void Run() {
    for (;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      if (!active_.load()) continue;
      const int64_t idle = NowMs() - last_activity_ms_.load(std::memory_order_relaxed);
      if (idle >= REXCVAR_GET(low_turbo_hold_ms)) {
        std::lock_guard lock(phase_mutex_);
        EndPhase();
        active_.store(false);
      }
    }
  }

  std::mutex mutex_;
  std::unordered_set<uint32_t> handles_;
  uint64_t phase_files_ = 0;
  uint64_t phase_bytes_ = 0;
  std::atomic<int64_t> last_activity_ms_{0};
  std::atomic<bool> active_{false};
  std::once_flag thread_once_;
  // Phasenzustand: BeginPhase (Gast-Thread) und EndPhase (Monitor-Thread).
  std::mutex phase_mutex_;
  int64_t phase_start_ms_ = 0;
  bool turbo_ = false;
  bool turbo_available_ = true;
  std::string saved_vsync_;
};

// ---------------------------------------------------------------------------
// Spielstand-Diagnose: Content-Aufrufe (XamContent*), Dateizugriffe auf
// SaveData.Dat und der Inhalt des Spielstand-Ordners auf der Festplatte.
// Alles mit "LoW-Spielstand:" im Log.
// ---------------------------------------------------------------------------
class SaveTrace {
 public:
  static SaveTrace& Get() {
    static SaveTrace t;
    return t;
  }
  void AddFile(uint32_t handle, const std::string& path) {
    std::lock_guard lock(mutex_);
    files_[handle] = path;
  }
  bool IsFile(uint32_t handle, std::string* path = nullptr) {
    std::lock_guard lock(mutex_);
    auto it = files_.find(handle);
    if (it == files_.end()) return false;
    if (path) *path = it->second;
    return true;
  }
  void RemoveFile(uint32_t handle) {
    std::lock_guard lock(mutex_);
    files_.erase(handle);
  }
  void AddEnum(uint32_t handle) {
    std::lock_guard lock(mutex_);
    enums_.insert(handle);
  }
  bool IsEnum(uint32_t handle) {
    std::lock_guard lock(mutex_);
    return enums_.count(handle) != 0;
  }

 private:
  std::mutex mutex_;
  std::unordered_map<uint32_t, std::string> files_;
  std::unordered_set<uint32_t> enums_;
};

std::string GuestCString(const uint8_t* base, uint32_t addr, size_t max = 128) {
  if (!addr) return "(null)";
  const char* p = reinterpret_cast<const char*>(base + addr);
  size_t n = 0;
  while (n < max && p[n]) ++n;
  return std::string(p, n);
}

// XCONTENT_DATA (308 Bytes): device_id, content_type, display_name[128] (UTF-16 BE), file_name[42]
std::string DescribeContentData(const uint8_t* base, uint32_t addr) {
  if (!addr) return "(null)";
  std::string display;
  for (int i = 0; i < 128; ++i) {
    const uint16_t ch = LoadBE16(base, addr + 8 + i * 2);
    if (!ch) break;
    display += ch < 0x80 ? char(ch) : '?';
  }
  return fmt::format("Geraet {} Typ {} Datei '{}' Name '{}'", LoadBE32(base, addr),
                     LoadBE32(base, addr + 4), GuestCString(base, addr + 264, 42), display);
}

std::filesystem::path UserDataRoot() {
  const std::string v = rex::cvar::GetFlagByName("user_data_root");
  if (!v.empty()) return std::filesystem::path(std::u8string(v.begin(), v.end()));
#if defined(_WIN32)
  PWSTR docs = nullptr;
  std::filesystem::path r;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &docs))) {
    r = std::filesystem::path(docs) / "lowrecomp";
  }
  CoTaskMemFree(docs);
  return r;
#else
  return {};
#endif
}

std::string U8Path(const std::filesystem::path& p) {
  const auto u = p.u8string();
  return std::string(u.begin(), u.end());
}

// Inhalt des Spielstand-Ordners (eigener Titel + SvR 2009) ins Log.
void LogSaveFolders(const char* when) {
  namespace fs = std::filesystem;
  const fs::path root = UserDataRoot() / "B13EBABEBABEBABE";
  REXLOG_INFO("LoW-Spielstand: Ordner ({}): {}", when, U8Path(root));
  int count = 0;
  for (const char* title : {"54510838", "54510826"}) {
    std::error_code ec;
    const fs::path dir = root / title;
    if (!fs::exists(dir, ec)) {
      REXLOG_INFO("LoW-Spielstand:   {} fehlt", title);
      continue;
    }
    for (auto it = fs::recursive_directory_iterator(dir, ec);
         !ec && it != fs::recursive_directory_iterator() && count < 60; it.increment(ec), ++count) {
      std::error_code e2;
      const auto rel = U8Path(fs::relative(it->path(), root, e2));
      if (it->is_directory(e2)) {
        REXLOG_INFO("LoW-Spielstand:   {}\\", rel);
      } else {
        const auto size = it->file_size(e2);
        const auto age = std::chrono::duration_cast<std::chrono::minutes>(
                             fs::file_time_type::clock::now() - it->last_write_time(e2))
                             .count();
        REXLOG_INFO("LoW-Spielstand:   {}  {} Bytes, vor {} min geaendert", rel, size, age);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Tastensymbole: fontHD.pac -> fontHP.pac (PlayStation) / fontHK.pac (Tastatur),
// ebenso game2dHD/SD und LoadHD/SD.
// Die Kopien legt der Launcher an (glyph_patch.h). Gleich
// lange Namen: Es wird nur ein Zeichen im Gast-Pfad getauscht und danach
// zurueckgesetzt, ohne Gast-Speicher anzulegen.
// ---------------------------------------------------------------------------
// 0 = Xbox (Original), 'P' = PlayStation, 'K' = Tastatur. Einmal je Spielstart.
char GlyphVariant() {
  static const char tag = [] {
    std::string mode = REXCVAR_GET(low_button_glyphs);
    for (auto& c : mode) c = char(std::tolower(static_cast<unsigned char>(c)));
    char t = 0;
    if (mode == "ps3" || mode == "playstation") {
      t = 'P';
    } else if (mode == "keyboard" || mode == "tastatur") {
      t = 'K';
    } else if (mode == "auto") {
      switch (ConnectedPadKind(/*wait_ms=*/500)) {
        case PadKind::kPlayStation: t = 'P'; break;
        case PadKind::kXbox: t = 0; break;
        case PadKind::kNone: t = 'K'; break;
      }
    }
    REXLOG_INFO("LoW-Tastensymbole: {} (Einstellung '{}')",
                t == 'P' ? "PlayStation" : t == 'K' ? "Tastatur" : "Xbox", mode);
    return t;
  }();
  return tag;
}

// Gast-Adresse des 'D' in "...\fontHD.pac", "...\game2dSD.pac" usw., sonst 0.
uint32_t GlyphFontSwapAddress(const uint8_t* base, uint32_t object_attributes,
                              std::string_view path) {
  bool match = false;
  for (const char* name : {"\\fonthd.pac", "\\fontsd.pac", "\\game2dhd.pac", "\\game2dsd.pac",
                           "\\loadhd.pac", "\\loadsd.pac"}) {
    if (EndsWith(path, name)) match = true;
  }
  if (!match) return 0;
  const uint32_t name_ptr = LoadBE32(base, object_attributes + 4);
  const uint16_t length = LoadBE16(base, name_ptr);
  const uint32_t buffer = LoadBE32(base, name_ptr + 4);
  if (!buffer || length < 5 || length > 512) return 0;
  const uint32_t addr = buffer + length - 5;  // ...?D.pac
  const char c = char(base[addr]);
  return (c == 'd' || c == 'D') ? addr : 0;
}

// Gemeinsamer Teil von NtCreateFile/NtOpenFile: r3 = Handle-Ziel, r5 = Attribute.
void OpenCommon(PPCFn fn, PPCContext& ctx, uint8_t* base, bool is_create) {
  g_guest_base.store(base, std::memory_order_relaxed);
  const uint32_t handle_out = ctx.r3.u32;
  const uint32_t saved_access = ctx.r4.u32;
  // NtCreateFile: r10 = CreateDisposition (1 open, 2 create, 3 open_if, 5 overwrite_if)
  const uint32_t saved_disposition = is_create ? ctx.r10.u32 : 1;
  const std::string path = GuestPath(base, ctx.r5.u32);

  if (REXCVAR_GET(low_skip_logos) && EndsWith(path, "movies\\logo.bik")) {
    REXLOG_INFO("LoW-System: Firmenlogos uebersprungen");
    if (handle_out) StoreBE32(base, handle_out, 0);
    ctx.r3.u64 = kStatusObjectNameNotFound;
    return;
  }

  if (const uint32_t swap = GlyphFontSwapAddress(base, ctx.r5.u32, path); swap && GlyphVariant()) {
    // Argumente sichern: schlaegt die Kopie fehl, mit dem Original erneut versuchen.
    const uint64_t args[8] = {ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64,
                              ctx.r7.u64, ctx.r8.u64, ctx.r9.u64, ctx.r10.u64};
    uint8_t* p = base + swap;
    const uint8_t original = *p;
    *p = static_cast<uint8_t>(GlyphVariant());
    Forward(fn, ctx, base);
    *p = original;
    if (ctx.r3.u32 == 0) {
      REXLOG_INFO("LoW-Tastensymbole: {} -> {}-Fassung geladen", path,
                  GlyphVariant() == 'P' ? "PlayStation" : "Tastatur");
      if (handle_out) LoadMonitor::Get().OnLoadFileOpened(LoadBE32(base, handle_out));
      return;
    }
    REXLOG_WARN("LoW-Tastensymbole: Fassung '{}' von {} fehlt (Status {:08X}) - "
                "im Launcher neu erzeugen lassen; nutze Xbox-Symbole",
                GlyphVariant(), path, ctx.r3.u32);
    ctx.r3.u64 = args[0]; ctx.r4.u64 = args[1]; ctx.r5.u64 = args[2]; ctx.r6.u64 = args[3];
    ctx.r7.u64 = args[4]; ctx.r8.u64 = args[5]; ctx.r9.u64 = args[6]; ctx.r10.u64 = args[7];
  }

  Forward(fn, ctx, base);

  // Diagnose fuer Spielstaende (auch SvR-2009-Import: "<Laufwerk>:\SaveData.Dat")
  {
    std::string lower = path;
    for (auto& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (lower.find("savedata") != std::string::npos) {
      REXLOG_INFO("LoW-Spielstand: Datei {} (Zugriff {:08X}, Modus {}) -> Status {:08X}", path,
                  saved_access, saved_disposition, ctx.r3.u32);
      if (ctx.r3.u32 == 0 && handle_out) SaveTrace::Get().AddFile(LoadBE32(base, handle_out), path);
    }
  }

  if (ctx.r3.u32 == 0 && handle_out && IsLoadFile(path)) {
    LoadMonitor::Get().OnLoadFileOpened(LoadBE32(base, handle_out));
  }
}

}  // namespace
}  // namespace low

// ---------------------------------------------------------------------------
// Ueberschriebene Systemfunktionen (Namen wie im generierten Code)
// ---------------------------------------------------------------------------
#if defined(_WIN32)

extern "C" REX_FUNC(__imp__NtCreateFile) {
  low::OpenCommon(low::Orig().create_file, ctx, base, /*is_create=*/true);
}

extern "C" REX_FUNC(__imp__NtOpenFile) {
  low::OpenCommon(low::Orig().open_file, ctx, base, /*is_create=*/false);
}

// NtReadFile(handle r3, event r4, apc r5, apc_ctx r6, iosb r7, buffer r8, length r9, offset r10)
extern "C" REX_FUNC(__imp__NtReadFile) {
  const uint32_t handle = ctx.r3.u32;
  const uint32_t length = ctx.r9.u32;
  const uint32_t iosb = ctx.r7.u32;
  low::Forward(low::Orig().read_file, ctx, base);
  low::LoadMonitor::Get().OnRead(handle, length);
  if (std::string p; low::SaveTrace::Get().IsFile(handle, &p)) {
    REXLOG_INFO("LoW-Spielstand: Lesen {} - {} Bytes angefordert, {} gelesen, Status {:08X}", p,
                length, iosb ? low::LoadBE32(base, iosb + 4) : 0, ctx.r3.u32);
  }
}

// NtWriteFile(handle r3, event r4, apc r5, apc_ctx r6, iosb r7, buffer r8, length r9, offset r10)
extern "C" REX_FUNC(__imp__NtWriteFile) {
  const uint32_t handle = ctx.r3.u32;
  const uint32_t length = ctx.r9.u32;
  const uint32_t iosb = ctx.r7.u32;
  low::Forward(low::Orig().write_file, ctx, base);
  if (std::string p; low::SaveTrace::Get().IsFile(handle, &p)) {
    REXLOG_INFO("LoW-Spielstand: Schreiben {} - {} Bytes, {} geschrieben, Status {:08X}", p, length,
                iosb ? low::LoadBE32(base, iosb + 4) : 0, ctx.r3.u32);
  }
}

extern "C" REX_FUNC(__imp__NtReadFileScatter) {
  const uint32_t handle = ctx.r3.u32;
  const uint32_t length = ctx.r9.u32;  // (handle, event, apc, apc_ctx, iosb, segments, length, offset)
  low::Forward(low::Orig().read_file_scatter, ctx, base);
  low::LoadMonitor::Get().OnRead(handle, length);
}

extern "C" REX_FUNC(__imp__NtClose) {
  const uint32_t handle = ctx.r3.u32;
  low::Forward(low::Orig().close, ctx, base);
  low::LoadMonitor::Get().OnClose(handle);
  if (std::string p; low::SaveTrace::Get().IsFile(handle, &p)) {
    REXLOG_INFO("LoW-Spielstand: Datei geschlossen {}", p);
    low::SaveTrace::Get().RemoveFile(handle);
  }
}

// --- Content-Aufrufe (nur Diagnose, Verhalten unveraendert) ----------------
namespace low {
namespace {
PPCFn XamOrig(const char* name) {
  PPCFn fn = Original(name);
  if (!fn) REXLOG_ERROR("LoW-Spielstand: {} in rexruntime.dll nicht gefunden", name);
  return fn;
}
}  // namespace
}  // namespace low

// XamContentCreateEx(user r3, root r4, content_data r5, flags r6, disposition* r7, license* r8, ...)
extern "C" REX_FUNC(__imp__XamContentCreateEx) {
  static const low::PPCFn fn = low::XamOrig("__imp__XamContentCreateEx");
  static std::once_flag listed;
  std::call_once(listed, [] { low::LogSaveFolders("erster Zugriff"); });
  const uint32_t user = ctx.r3.u32, flags = ctx.r6.u32, disp_ptr = ctx.r7.u32;
  const std::string root = low::GuestCString(base, ctx.r4.u32);
  const std::string data = low::DescribeContentData(base, ctx.r5.u32);
  low::Forward(fn, ctx, base);
  REXLOG_INFO("LoW-Spielstand: XamContentCreateEx(Spieler {}, '{}', {}, Modus {:X}) -> {:08X}, "
              "Disposition {}",
              user, root, data, flags, ctx.r3.u32, disp_ptr ? low::LoadBE32(base, disp_ptr) : 0);
}

// XamContentCreateEnumerator(user r3, device r4, type r5, flags r6, items r7, size* r8, handle* r9)
extern "C" REX_FUNC(__imp__XamContentCreateEnumerator) {
  static const low::PPCFn fn = low::XamOrig("__imp__XamContentCreateEnumerator");
  const uint32_t user = ctx.r3.u32, device = ctx.r4.u32, type = ctx.r5.u32, handle_ptr = ctx.r9.u32;
  low::LogSaveFolders("Suche");
  low::Forward(fn, ctx, base);
  const uint32_t handle = handle_ptr ? low::LoadBE32(base, handle_ptr) : 0;
  if (ctx.r3.u32 == 0 && handle) low::SaveTrace::Get().AddEnum(handle);
  REXLOG_INFO("LoW-Spielstand: XamContentCreateEnumerator(Spieler {}, Geraet {}, Typ {}) -> {:08X}",
              user, device, type, ctx.r3.u32);
}

// XamEnumerate(handle r3, flags r4, buffer r5, size r6, items_returned* r7, overlapped r8)
extern "C" REX_FUNC(__imp__XamEnumerate) {
  static const low::PPCFn fn = low::XamOrig("__imp__XamEnumerate");
  const uint32_t handle = ctx.r3.u32, buffer = ctx.r5.u32, items_ptr = ctx.r7.u32;
  low::Forward(fn, ctx, base);
  if (!low::SaveTrace::Get().IsEnum(handle)) return;
  const uint32_t items = items_ptr ? low::LoadBE32(base, items_ptr) : 0;
  REXLOG_INFO("LoW-Spielstand: XamEnumerate -> {:08X}, {} Eintraege", ctx.r3.u32, items);
  for (uint32_t i = 0; ctx.r3.u32 == 0 && i < items && i < 16; ++i) {
    REXLOG_INFO("LoW-Spielstand:   [{}] {}", i, low::DescribeContentData(base, buffer + i * 308));
  }
}

// XamContentClose(root r3, overlapped r4)
extern "C" REX_FUNC(__imp__XamContentClose) {
  static const low::PPCFn fn = low::XamOrig("__imp__XamContentClose");
  const std::string root = low::GuestCString(base, ctx.r3.u32);
  low::Forward(fn, ctx, base);
  REXLOG_INFO("LoW-Spielstand: XamContentClose('{}') -> {:08X}", root, ctx.r3.u32);
  low::LogSaveFolders("nach dem Schliessen");
}

// XamShowDeviceSelectorUI(user r3, type r4, flags r5, total r6, device_id* r7, overlapped r8)
extern "C" REX_FUNC(__imp__XamShowDeviceSelectorUI) {
  static const low::PPCFn fn = low::XamOrig("__imp__XamShowDeviceSelectorUI");
  const uint32_t user = ctx.r3.u32, type = ctx.r4.u32, flags = ctx.r5.u32;
  low::Forward(fn, ctx, base);
  REXLOG_INFO("LoW-Spielstand: Speichergeraet-Auswahl(Spieler {}, Typ {}, Flags {:X}) -> {:08X}",
              user, type, flags, ctx.r3.u32);
}

#endif  // _WIN32

// Sprache. Gueltig fuer die EUR-Version: 1 Englisch, 3 Deutsch, 4 Franzoesisch,
// 5 Spanisch, 6 Italienisch.
extern "C" REX_FUNC(__imp__XGetLanguage) {
  (void)base;
  uint32_t language = 1;
  const std::string value = rex::cvar::GetFlagByName("user_language");
  if (!value.empty()) {
    const unsigned long parsed = std::strtoul(value.c_str(), nullptr, 10);
    if (parsed == 1 || parsed == 3 || parsed == 4 || parsed == 5 || parsed == 6) {
      language = static_cast<uint32_t>(parsed);
    }
  }
  ctx.r3.u64 = language;
}

// ---------------------------------------------------------------------------
// Bildrate (Midasm-Hooks aus config\default_codegen.toml; globaler Namensraum,
// die generierten Quellen deklarieren sie selbst als extern)
// ---------------------------------------------------------------------------

// Anfang von 0x8235EB90 (Herunterschalten 60 -> 30 bzw. 50 -> 25).
// true = Funktion sofort verlassen.
bool LowSkipFpsDowngrade() {
  static std::atomic<int> calls{0};
  const bool skip = REXCVAR_GET(low_fps60);
  const int n = calls.fetch_add(1) + 1;
  if (n <= 5 || (n % 100) == 0) {
    REXLOG_INFO("LoW-FPS: Spiel will auf 30 Bilder/s schalten ({}. Mal) -> {}", n,
                skip ? "bleibt bei 60 (60-fps-Modus)" : "30 Bilder/s");
  }
  return skip;
}

// 0x8235F37C, einmal pro Bild (Bildende des Spiels). r10 = Ziel-Bildrate des Spiels.
void LowFrameTick(PPCRegister& r10) {
  using clock = std::chrono::steady_clock;
  static clock::time_point window_start = clock::now();
  static uint32_t frames = 0;
  static uint32_t last_target = 0;
  const uint32_t target = r10.u32;
  ++frames;
  const auto now = clock::now();
  const double secs = std::chrono::duration<double>(now - window_start).count();
  if (target != last_target) {
    REXLOG_INFO("LoW-FPS: Ziel-Bildrate des Spiels jetzt {}", target);
    last_target = target;
  }
  if (secs >= 10.0) {
    REXLOG_INFO("LoW-FPS: {:.1f} Bilder/s (Ziel {})", frames / secs, target);
    frames = 0;
    window_start = now;
  }
}

// ---------------------------------------------------------------------------
// Spielstand-Ablauf (Midasm-Hooks, nur Diagnose): Zustaende der sechs
// Spielstand-Ablaeufe, Auftraege an den Speicher-Thread, Meldungen
// (50010 = "Du musst ein Speichergeraet waehlen ..."), gewaehltes Geraet.
// ---------------------------------------------------------------------------
namespace {
uint32_t GuestU32(uint32_t addr) {
  uint8_t* base = low::g_guest_base.load(std::memory_order_relaxed);
  if (!base || !addr) return 0xFFFFFFFF;
  uint32_t v;
  std::memcpy(&v, base + addr, 4);
  return std::byteswap(v);
}

void TraceFlow(int id, uint32_t self, uint32_t state) {
  static std::mutex m;
  static std::unordered_map<uint64_t, uint32_t> last;
  const uint64_t key = (uint64_t(id) << 32) | self;
  {
    std::lock_guard lock(m);
    auto it = last.find(key);
    if (it != last.end() && it->second == state) return;
    last[key] = state;
  }
  REXLOG_INFO("LoW-Spielstand: Ablauf {} ({:08X}) Zustand {} [52={} 64={} 2840={} 2844={} 2852={}]",
              id, self, state, GuestU32(self + 52), GuestU32(self + 64), GuestU32(self + 2840),
              GuestU32(self + 2844), GuestU32(self + 2852));
}
}  // namespace

void LowSaveFlow1(PPCRegister& r3, PPCRegister& r11) { TraceFlow(1, r3.u32, r11.u32); }
void LowSaveFlow2(PPCRegister& r3, PPCRegister& r11) { TraceFlow(2, r3.u32, r11.u32); }
void LowSaveFlow3(PPCRegister& r3, PPCRegister& r11) { TraceFlow(3, r3.u32, r11.u32); }
void LowSaveFlow4(PPCRegister& r3, PPCRegister& r11) { TraceFlow(4, r3.u32, r11.u32); }
void LowSaveFlow5(PPCRegister& r3, PPCRegister& r11) { TraceFlow(5, r3.u32, r11.u32); }
void LowSaveFlow6(PPCRegister& r3, PPCRegister& r11) { TraceFlow(6, r3.u32, r11.u32); }

// 0x823895C8: Meldung (r4 = Art: 0 OK, 1 ..., 2 Ja/Nein; r5 = Text-ID)
void LowSaveMessage(PPCRegister& r4, PPCRegister& r5) {
  REXLOG_INFO("LoW-Spielstand: Meldung {} (Art {})", r5.u32, r4.u32);
}

// 0x82388258: Auftrag an den Speicher-Thread (r4 = Befehl, r5 = Spieler)
void LowSaveRequest(PPCRegister& r4, PPCRegister& r5) {
  REXLOG_INFO("LoW-Spielstand: Auftrag {} (Spieler {})", r4.u32, r5.u32);
}

// 0x82388B34: Speicher-Thread hat einen Auftrag erledigt (r31 = Speicher-Objekt)
void LowSaveDone(PPCRegister& r31) {
  const uint32_t self = r31.u32;
  REXLOG_INFO("LoW-Spielstand: Auftrag {} erledigt -> Status {} (Geraet {})", GuestU32(self + 60),
              GuestU32(self + 68), GuestU32(self + 104));
}

// 0x82387818: Geraet setzen (r4)
void LowSaveSetDevice(PPCRegister& r4) {
  REXLOG_INFO("LoW-Spielstand: Geraet wird gesetzt auf {}", r4.u32);
}

// 0x8238B078: Start-Ablauf Zustand 9 - zeigt Meldung 50010 ("Du musst ein
// Speichergeraet waehlen ... Moechtest du ein Speichergeraet waehlen?").
// Auf dem PC gibt es nur ein Geraet; wir beantworten die Frage selbst mit "Ja":
// Zustand 11 (Auftrag 107 = Geraeteauswahl, danach Laden) und weiter bei
// 0x8238B09C (Funktionsende). true = Meldung ueberspringen.
// Hoechstens dreimal pro Spielstart, damit ein dauerhaftes Problem nicht in einer
// Schleife endet - danach erscheint die Frage wie im Original.
bool LowAutoStorageYes(PPCRegister& r31) {
  static std::atomic<int> count{0};
  if (!REXCVAR_GET(low_auto_storage)) return false;
  uint8_t* base = low::g_guest_base.load(std::memory_order_relaxed);
  if (!base || !r31.u32) return false;
  if (count.fetch_add(1) >= 3) {
    REXLOG_WARN("LoW-Spielstand: Speichergeraet-Frage erscheint wieder (mehrfach noetig)");
    return false;
  }
  const uint32_t next = std::byteswap(uint32_t(11));
  std::memcpy(base + r31.u32 + 56, &next, 4);
  REXLOG_INFO("LoW-Spielstand: Speichergeraet-Frage automatisch mit Ja beantwortet");
  return true;
}
