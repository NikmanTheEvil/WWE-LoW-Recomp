// LoWRecomp-Launcher - SmackDown vs. RAW 2009 importieren
//
// WWE Legends of WrestleMania (Titel-ID 54510838) traegt SvR 2009 (54510826)
// als "alternative Titel-ID" im XEX-Header. Beim Import (0x823884C0) oeffnet LoW
// den SvR-Stand gezielt per XContentCreate mit Fremd-Titel-ID (0x8246FA90 ->
// xam.xex Ordinal 625 = XamContentCreateInternal, XCONTENT_AGGREGATE_DATA):
//   Paketname "SaveData.dat" (fest, Daten bei 0x8267FE90), Inhaltstyp 1,
//   Titel-ID 54510826 (Tabelle 0x8267FEBC, Index = SKU 3 dieses Builds),
//   XUID des angemeldeten Profils, OPEN_EXISTING
// und liest danach "<Laufwerk>:\SaveData.Dat".
// ReXGlue legt solche Pakete hier ab (ContentManager::ResolvePackagePath):
//   <Spielstände>\B13EBABEBABEBABE\54510826\00000001\SaveData.dat\SaveData.dat
//   <Spielstände>\B13EBABEBABEBABE\54510826\Headers\00000001\SaveData.dat.header
// Es gibt also genau einen SvR-Platz; ein neuer Import ersetzt den alten.
//
// Fruehere Launcher-Version (falsch): Pakete lagen in LoWs eigenem Ordner
// (54510838) unter beliebigem Namen -> MigrateLegacyImports() verschiebt sie.

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "stfs_reader.h"

namespace low::launcher {

namespace fs = std::filesystem;

constexpr uint32_t kLowTitleId = 0x54510838;
constexpr uint32_t kSvr2009TitleId = 0x54510826;
constexpr uint64_t kProfileXuid = 0xB13EBABEBABEBABEull;  // ReXGlue-Standardprofil
constexpr uint32_t kContentTypeSavedGame = 1;
constexpr const char* kOwnSaveName = "LegendSaveData.dat";
constexpr const char* kSvrSaveFile = "SaveData.dat";

struct ImportedSave {
  std::string package_name;
  std::string display_name;
  fs::path folder;
};

struct ImportResult {
  bool ok = false;
  std::string message;
  std::vector<std::string> imported;  // Paketnamen
};

// Pfad <-> UTF-8 ohne ANSI-Codepage (unter Windows wichtig fuer Umlaute)
inline std::string U8(const fs::path& p) {
  const auto u = p.u8string();
  return std::string(u.begin(), u.end());
}
inline fs::path FromU8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

inline std::string ToLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
  return s;
}

constexpr const char* kSvrPackageName = "SaveData.dat";  // Paketname, den LoW oeffnet

inline fs::path TitleRoot(const fs::path& user_data_root, uint32_t title_id) {
  char xuid[17], title[9];
  std::snprintf(xuid, sizeof(xuid), "%016llX", static_cast<unsigned long long>(kProfileXuid));
  std::snprintf(title, sizeof(title), "%08X", title_id);
  return user_data_root / xuid / title;
}
// LoWs eigene Spielstaende
inline fs::path PackagesDir(const fs::path& user_data_root) {
  return TitleRoot(user_data_root, kLowTitleId) / "00000001";
}
inline fs::path HeadersDir(const fs::path& user_data_root) {
  return TitleRoot(user_data_root, kLowTitleId) / "Headers" / "00000001";
}
// Platz fuer den SvR-2009-Stand
inline fs::path SvrPackagesDir(const fs::path& user_data_root) {
  return TitleRoot(user_data_root, kSvr2009TitleId) / "00000001";
}
inline fs::path SvrHeadersDir(const fs::path& user_data_root) {
  return TitleRoot(user_data_root, kSvr2009TitleId) / "Headers" / "00000001";
}
inline fs::path SvrPackageDir(const fs::path& user_data_root) {
  return SvrPackagesDir(user_data_root) / kSvrPackageName;
}
inline fs::path SvrHeaderPath(const fs::path& user_data_root) {
  return SvrHeadersDir(user_data_root) / (std::string(kSvrPackageName) + ".header");
}

inline bool ReadAll(const fs::path& p, std::vector<uint8_t>& out, uint64_t limit = 512ull << 20) {
  std::error_code ec;
  const auto size = fs::file_size(p, ec);
  if (ec || size > limit) return false;
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  out.resize(size_t(size));
  f.read(reinterpret_cast<char*>(out.data()), std::streamsize(size));
  return bool(f) || f.gcount() == std::streamsize(size);
}

inline bool WriteAll(const fs::path& p, const uint8_t* data, size_t size) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(data), std::streamsize(size));
  return bool(f);
}

// XCONTENT_AGGREGATE_DATA (0x148 Byte, Big Endian), wie ReXGlue sie als
// <Paket>.header ablegt: Anzeigename für die Spielstand-Liste.
inline std::vector<uint8_t> BuildContentHeader(const std::string& package_name,
                                               const std::u16string& display_name) {
  std::vector<uint8_t> h(0x148, 0);
  auto put32 = [&](size_t off, uint32_t v) {
    h[off] = uint8_t(v >> 24); h[off + 1] = uint8_t(v >> 16); h[off + 2] = uint8_t(v >> 8); h[off + 3] = uint8_t(v);
  };
  put32(0x000, 1);                       // device_id: HDD
  put32(0x004, kContentTypeSavedGame);   // content_type
  for (size_t i = 0; i < display_name.size() && i < 127; ++i) {
    h[0x008 + i * 2] = uint8_t(display_name[i] >> 8);
    h[0x008 + i * 2 + 1] = uint8_t(display_name[i]);
  }
  std::memcpy(&h[0x108], package_name.data(), std::min<size_t>(package_name.size(), 42));
  put32(0x138, uint32_t(kProfileXuid >> 32));  // xuid
  put32(0x13C, uint32_t(kProfileXuid));
  put32(0x140, kSvr2009TitleId);               // title_id: SvR 2009
  return h;
}

inline std::u16string Utf8ToUtf16(const std::string& s) {
  std::u16string out;
  for (size_t i = 0; i < s.size();) {
    uint32_t c = uint8_t(s[i]);
    size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
    if (n > 1) {
      c &= (0xFF >> (n + 1));
      for (size_t k = 1; k < n && i + k < s.size(); ++k) c = (c << 6) | (uint8_t(s[i + k]) & 0x3F);
    }
    i += n;
    if (c >= 0x10000) {
      c -= 0x10000;
      out += char16_t(0xD800 + (c >> 10));
      out += char16_t(0xDC00 + (c & 0x3FF));
    } else {
      out += char16_t(c);
    }
  }
  return out;
}

inline std::string HeaderDisplayName(const fs::path& header) {
  std::vector<uint8_t> h;
  if (!ReadAll(header, h, 4096) || h.size() < 0x148) return {};
  std::string out;
  for (size_t i = 0; i < 128; ++i) {
    const uint16_t c = uint16_t((h[0x008 + i * 2] << 8) | h[0x008 + i * 2 + 1]);
    if (!c) break;
    out += c < 0x80 ? char(c) : '?';
  }
  return out;
}

inline bool ContainsSaveData(const fs::path& folder) {
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(folder, ec)) {
    if (e.is_regular_file(ec) && ToLower(U8(e.path().filename())) == ToLower(kSvrSaveFile)) return true;
  }
  return false;
}

// Ordnernamen, die LoW selbst fuer eigene Inhalte benutzt (nie anfassen)
inline bool IsLowOwnPackage(const std::string& name) {
  const std::string n = ToLower(name);
  auto ends = [&](const char* suf) {
    const size_t l = std::strlen(suf);
    return n.size() >= l && n.compare(n.size() - l, l, suf) == 0;
  };
  return n == ToLower(kOwnSaveName) || ends("recordingdat.rec") || ends("scenedat.scn") ||
         ends("screenshot.ss");
}

// Der importierte SvR-2009-Stand (0 oder 1 Eintrag).
inline std::vector<ImportedSave> ListImported(const fs::path& user_data_root) {
  std::vector<ImportedSave> out;
  const fs::path dir = SvrPackageDir(user_data_root);
  if (ContainsSaveData(dir)) {
    ImportedSave s;
    s.package_name = kSvrPackageName;
    s.folder = dir;
    s.display_name = HeaderDisplayName(SvrHeaderPath(user_data_root));
    out.push_back(s);
  }
  return out;
}

inline bool RemoveImported(const fs::path& user_data_root, const std::string& package_name) {
  if (package_name != kSvrPackageName) return false;
  std::error_code ec;
  const fs::path dir = SvrPackageDir(user_data_root);
  if (!ContainsSaveData(dir)) return false;
  fs::remove_all(dir, ec);
  if (ec) return false;
  fs::remove(SvrHeaderPath(user_data_root), ec);
  return true;
}

// Importe der frueheren Launcher-Version (in LoWs Ordner, beliebiger Name)
// an den richtigen Platz verschieben. Liefert die Zahl der aufgeraeumten Pakete.
inline int MigrateLegacyImports(const fs::path& user_data_root) {
  std::error_code ec;
  std::vector<fs::path> legacy;
  for (const auto& e : fs::directory_iterator(PackagesDir(user_data_root), ec)) {
    if (!e.is_directory(ec) || IsLowOwnPackage(U8(e.path().filename()))) continue;
    if (ContainsSaveData(e.path())) legacy.push_back(e.path());
  }
  if (legacy.empty()) return 0;
  // neuestes Paket zuerst
  auto mtime = [](const fs::path& p) {
    std::error_code e2;
    return fs::last_write_time(p, e2);
  };
  std::sort(legacy.begin(), legacy.end(),
            [&](const fs::path& a, const fs::path& b) { return mtime(a) > mtime(b); });
  int cleaned = 0;
  for (const auto& dir : legacy) {
    const std::string name = U8(dir.filename());
    const fs::path old_header = HeadersDir(user_data_root) / (name + ".header");
    if (!ContainsSaveData(SvrPackageDir(user_data_root))) {
      fs::create_directories(SvrPackagesDir(user_data_root), ec);
      fs::rename(dir, SvrPackageDir(user_data_root), ec);
      if (ec) continue;
      const std::string display = HeaderDisplayName(old_header);
      const auto header = BuildContentHeader(kSvrPackageName,
                                             Utf8ToUtf16(display.empty() ? "SmackDown vs. RAW 2009" : display));
      WriteAll(SvrHeaderPath(user_data_root), header.data(), header.size());
    } else {
      fs::remove_all(dir, ec);
      if (ec) continue;
    }
    fs::remove(old_header, ec);
    ++cleaned;
  }
  return cleaned;
}

namespace detail {

// Schreibt ein Paket (Dateiliste) nach LoWs Spielstand-Ordner.
struct PackageFile {
  std::string path;  // relativ, '/'-getrennt
  std::vector<uint8_t> data;
};

inline bool InstallPackage(const fs::path& user_data_root, const std::string& display,
                           const std::vector<PackageFile>& files, std::string& error) {
  std::error_code ec;
  const fs::path dir = SvrPackageDir(user_data_root);
  fs::remove_all(dir, ec);  // es gibt nur einen SvR-Platz: alten Import ersetzen
  ec.clear();
  fs::create_directories(dir, ec);
  if (ec) {
    error = "Could not create folder: " + U8(dir);
    return false;
  }
  for (const auto& f : files) {
    fs::path target = dir;
    size_t start = 0;
    while (start <= f.path.size()) {
      const size_t slash = f.path.find('/', start);
      const std::string part = f.path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
      if (part.empty() || part == "." || part == "..") {
        error = "Invalid file name in the package: " + f.path;
        return false;
      }
      target /= FromU8(part);
      if (slash == std::string::npos) break;
      start = slash + 1;
    }
    if (!WriteAll(target, f.data.data(), f.data.size())) {
      error = "Could not write file: " + U8(target);
      return false;
    }
  }
  const auto header = BuildContentHeader(kSvrPackageName, Utf8ToUtf16(display));
  if (!WriteAll(SvrHeaderPath(user_data_root), header.data(), header.size())) {
    error = "Could not write the content header.";
    return false;
  }
  return true;
}

inline bool HasSaveData(const std::vector<PackageFile>& files) {
  for (const auto& f : files)
    if (ToLower(f.path) == ToLower(kSvrSaveFile)) return true;
  return false;
}

// STFS-Paket pruefen und entpacken (noch nichts schreiben).
struct LoadedSave {
  std::string display;
  std::vector<PackageFile> files;
};

inline bool LoadStfs(const fs::path& file, LoadedSave& out, std::string& message, bool strict_title) {
  std::vector<uint8_t> data;
  if (!ReadAll(file, data)) {
    message = "Could not read file: " + U8(file.filename());
    return false;
  }
  StfsReader stfs;
  std::string error;
  if (!stfs.Open(std::move(data), error)) {
    message = U8(file.filename()) + ": " + error;
    return false;
  }
  if (stfs.title_id() != kSvr2009TitleId) {
    if (strict_title) {
      char buf[160];
      std::snprintf(buf, sizeof(buf),
                    "The package belongs to title ID %08X, not to SmackDown vs. RAW 2009 (54510826).",
                    stfs.title_id());
      message = buf;
    }
    return false;
  }
  if (stfs.content_type() != kContentTypeSavedGame) {
    if (strict_title)
      message = "The package is not a save game (content type " + std::to_string(stfs.content_type()) + ").";
    return false;
  }
  out.files.clear();
  for (const auto& e : stfs.entries()) {
    if (e.directory) continue;
    PackageFile pf;
    pf.path = e.path;
    if (!stfs.Extract(e, pf.data, error)) {
      message = error;
      return false;
    }
    out.files.push_back(std::move(pf));
  }
  if (!HasSaveData(out.files)) {
    message = "SaveData.dat is missing in the package - is this a SvR 2009 save?";
    return false;
  }
  out.display = stfs.display_name().empty() ? "SmackDown vs. RAW 2009" : stfs.display_name();
  return true;
}

inline bool IsStfsFile(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  uint8_t magic[4] = {};
  if (!f.read(reinterpret_cast<char*>(magic), 4)) return false;
  return StfsReader::HasStfsMagic(magic, 4);
}

}  // namespace detail

// Hauptfunktion: source = Paketdatei, SaveData.dat, Ordner mit SaveData.dat
// oder ein Ordner (z. B. USB-Stick "Content"), der durchsucht wird.
// Es gibt nur einen SvR-Platz; bei mehreren Funden wird der neueste genommen.
inline ImportResult ImportSvr2009(const fs::path& source, const fs::path& user_data_root) {
  ImportResult result;
  std::error_code ec;
  std::string error;
  detail::LoadedSave save;
  bool loaded = false;
  std::string note;

  if (fs::is_regular_file(source, ec)) {
    if (detail::IsStfsFile(source)) {
      loaded = detail::LoadStfs(source, save, result.message, /*strict_title=*/true);
    } else if (ToLower(U8(source.filename())) == ToLower(kSvrSaveFile)) {
      detail::PackageFile pf{kSvrSaveFile, {}};
      if (!ReadAll(source, pf.data)) {
        result.message = "Could not read SaveData.dat.";
        return result;
      }
      save.display = "SmackDown vs. RAW 2009";
      save.files.push_back(std::move(pf));
      loaded = true;
    } else {
      result.message = "Unknown format. Expected: Xbox 360 save package (CON) or SaveData.dat.";
    }
  } else if (fs::is_directory(source, ec)) {
    if (ContainsSaveData(source)) {
      // Bereits entpackter Spielstand: Ordner 1:1 uebernehmen
      for (const auto& e : fs::recursive_directory_iterator(source, ec)) {
        if (!e.is_regular_file(ec)) continue;
        detail::PackageFile pf;
        pf.path = U8(fs::relative(e.path(), source, ec).generic_u8string());
        if (!ReadAll(e.path(), pf.data)) {
          result.message = "Could not read file: " + pf.path;
          return result;
        }
        save.files.push_back(std::move(pf));
      }
      save.display = "SmackDown vs. RAW 2009";
      loaded = true;
    } else {
      // Ordner durchsuchen (max. 8 Ebenen): alle SvR-2009-Pakete sammeln, neuestes nehmen
      std::vector<std::pair<fs::file_time_type, fs::path>> found;
      std::string last_error;
      for (auto it = fs::recursive_directory_iterator(source, fs::directory_options::skip_permission_denied, ec);
           it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it.depth() > 8) {
          it.disable_recursion_pending();
          continue;
        }
        if (!it->is_regular_file(ec) || !detail::IsStfsFile(it->path())) continue;
        detail::LoadedSave probe;
        std::string msg;
        if (detail::LoadStfs(it->path(), probe, msg, /*strict_title=*/false)) {
          std::error_code e2;
          found.emplace_back(fs::last_write_time(it->path(), e2), it->path());
        } else if (!msg.empty()) {
          last_error = msg;
        }
      }
      if (found.empty()) {
        result.message = last_error.empty() ? "No SmackDown vs. RAW 2009 save found in the folder."
                                            : last_error;
      } else {
        std::sort(found.begin(), found.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });
        loaded = detail::LoadStfs(found.front().second, save, result.message, /*strict_title=*/true);
        if (found.size() > 1)
          note = " (" + std::to_string(found.size()) + " found, the newest one was used)";
      }
    }
  } else {
    result.message = "File or folder not found.";
  }

  if (loaded) {
    result.ok = detail::InstallPackage(user_data_root, save.display, save.files, error);
    if (result.ok) {
      result.imported.push_back(kSvrPackageName);
      result.message = "Imported: " + save.display + note;
    } else {
      result.message = error;
    }
  }
  return result;
}

}  // namespace low::launcher
