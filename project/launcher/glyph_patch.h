// LoWRecomp-Launcher - Tastensymbole (PlayStation 3 / Tastatur)
//
// Die Tastensymbole (U+E0xx im Spieltext) liegen als Bild in den Schrift-
// Texturen von pac\menu\fontHD.pac / fontSD.pac: eine Symbolseite (DDS, DXT5,
// 256x512, ohne Mipmaps), die in jeder Datei 21-mal identisch vorkommt.
// Die PS3-Version hat dieselbe Seite mit identischer Zeichentabelle, nur mit
// PS3-Symbolen (Quadrat/Dreieck/Kreis/Kreuz an Stelle von X/Y/B/A usw.).
//
// Der Launcher schreibt Kopien mit getauschten Seiten:
//   pac\menu\fontHD.pac -> fontHP.pac (PS3) / fontHK.pac (Tastatur), SD ebenso
// Match-HUD und Ladebildschirm siehe kHudFiles (fertige Fassungen).
// Gleich lange Namen, damit das Spiel beim Oeffnen nur ein Zeichen im Pfad
// tauschen muss (low_system.cpp). Die Originale bleiben unveraendert.

#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace low::launcher {

namespace gfs = std::filesystem;

constexpr size_t kGlyphPageBytes = 256 * 512;  // DXT5: 1 Byte pro Pixel

inline bool GlyphReadAll(const gfs::path& p, std::vector<uint8_t>& out) {
  std::error_code ec;
  const auto size = gfs::file_size(p, ec);
  if (ec || size > (256ull << 20)) return false;
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  out.resize(size_t(size));
  f.read(reinterpret_cast<char*>(out.data()), std::streamsize(size));
  return f.gcount() == std::streamsize(size);
}

inline uint32_t GlyphLE32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// Tauscht in `data` jede Symbolseite `from` gegen `to`. Liefert die Anzahl.
inline int SwapGlyphPages(std::vector<uint8_t>& data, const std::vector<uint8_t>& from,
                          const std::vector<uint8_t>& to) {
  if (from.size() != kGlyphPageBytes || to.size() != kGlyphPageBytes) return 0;
  int count = 0;
  static const uint8_t kMagic[8] = {'D', 'D', 'S', ' ', 0x7C, 0, 0, 0};
  for (size_t o = 0; o + 128 + kGlyphPageBytes <= data.size(); ++o) {
    if (std::memcmp(&data[o], kMagic, 8) != 0) continue;
    const uint8_t* h = &data[o];
    const uint32_t height = GlyphLE32(h + 12), width = GlyphLE32(h + 16), mips = GlyphLE32(h + 28);
    if (width != 256 || height != 512 || mips > 1 || std::memcmp(h + 84, "DXT5", 4) != 0) continue;
    uint8_t* page = &data[o + 128];
    if (std::memcmp(page, from.data(), kGlyphPageBytes) != 0) continue;
    std::memcpy(page, to.data(), kGlyphPageBytes);
    ++count;
    o += 128 + kGlyphPageBytes - 1;
  }
  return count;
}

// Varianten: 'P' = PlayStation 3 (Originalgrafiken der PS3-Version),
//            'K' = Tastatur (eigene Tastenkappen, tools\kb_glyphs.py)
struct GlyphVariant {
  char tag;              // ersetzt das 'D' im Dateinamen (fontHD -> fontHP / fontHK)
  const char* hd_asset;  // Symbolseite HD in <exe>\glyphs
  const char* sd_asset;  // Symbolseite SD
  const char* hud_dir;   // fertige HUD-/Ladebildschirm-Fassungen unter <exe>\...
  const char* name;      // fuer Fehlermeldungen
};
constexpr GlyphVariant kPs3Variant = {'P', "ps3_hd.bin", "ps3_sd.bin", "ps3hud", "PlayStation symbols"};
constexpr GlyphVariant kKeyboardVariant = {'K', "kb_hd.bin", "kb_sd.bin", "kbhud", "Keyboard symbols"};

// Zieldatei: Originalname mit Variante statt 'D' (gleiche Laenge, siehe low_system.cpp)
inline std::string VariantName(const char* original, char tag) {
  std::string s(original);
  const size_t dot = s.rfind('.');
  if (dot != std::string::npos && dot > 0) s[dot - 1] = tag;
  return s;
}

// Match-HUD und Ladebildschirm: viele eigene Bilder mit Knopfsymbolen (QTE-Rad,
// "Klettern: B gedrueckt halten" ...), komprimiert (BPE) und verschachtelt.
// Fertige Fassungen (tools\ps3_hud_merge.py / kb_hud_build.py) unter <exe>\<hud_dir>\.
constexpr const char* kHudFiles[] = {"pac/game2dHD.pac", "pac/game2dSD.pac",
                                     "pac/menu/LoadHD.pac", "pac/menu/LoadSD.pac"};

// Kopiert die HUD-Fassungen in den Spielordner (nur wenn sich der Inhalt
// unterscheidet). Fehlen sie, bleibt das HUD bei Xbox-Symbolen - kein Fehler.
inline bool EnsureHudFiles(const gfs::path& game_dir, const gfs::path& exe_dir,
                           const GlyphVariant& v, std::string& error) {
  for (const char* orig : kHudFiles) {
    const std::string target = VariantName(orig, v.tag);
    const gfs::path src = exe_dir / v.hud_dir / gfs::path(target).filename();
    const gfs::path dst = game_dir / gfs::path(target).make_preferred();
    std::error_code e1;
    if (!gfs::exists(src, e1)) continue;
    {
      std::vector<uint8_t> a, b;
      if (GlyphReadAll(src, a) && GlyphReadAll(dst, b) && a == b) continue;
    }
    const gfs::path tmp = gfs::path(dst).concat(".tmp");
    std::error_code ec;
    gfs::copy_file(src, tmp, gfs::copy_options::overwrite_existing, ec);
    if (!ec) gfs::rename(tmp, dst, ec);
    if (ec) {
      gfs::remove(tmp, ec);
      error = std::string(v.name) + ": could not create " + target + " (game folder write-protected?).";
      return false;
    }
  }
  return true;
}

// Erzeugt (falls noetig) fontH?.pac / fontS?.pac mit getauschten Symbolseiten.
inline bool EnsureGlyphFonts(const gfs::path& game_dir, const gfs::path& exe_dir,
                             const GlyphVariant& v, std::string& error) {
  std::error_code ec;
  const gfs::path assets_dir = exe_dir / "glyphs";
  const struct { const char* original; const char* xbox; const char* to; } jobs[] = {
      {"pac/menu/fontHD.pac", "xbox_hd.bin", v.hd_asset},
      {"pac/menu/fontSD.pac", "xbox_sd.bin", v.sd_asset},
  };
  for (const auto& job : jobs) {
    const std::string patched = VariantName(job.original, v.tag);
    const gfs::path src = game_dir / gfs::path(job.original).make_preferred();
    const gfs::path dst = game_dir / gfs::path(patched).make_preferred();
    if (!gfs::exists(src, ec)) {
      error = std::string(job.original) + " is missing in the game folder.";
      return false;
    }
    std::vector<uint8_t> data, from, to;
    if (!GlyphReadAll(assets_dir / job.xbox, from) || !GlyphReadAll(assets_dir / job.to, to)) {
      error = std::string(v.name) + ": symbol pages missing (folder glyphs next to the launcher).";
      return false;
    }
    if (!GlyphReadAll(src, data)) {
      error = std::string(job.original) + " could not be read.";
      return false;
    }
    if (SwapGlyphPages(data, from, to) == 0) {
      error = std::string(job.original) + ": no Xbox symbol page found (different game version?).";
      return false;
    }
    // Schon aktuell? (Inhalt vergleichen; 25 MB, dauert Millisekunden)
    {
      std::vector<uint8_t> cur;
      if (GlyphReadAll(dst, cur) && cur == data) continue;
    }
    const gfs::path tmp = gfs::path(dst).concat(".tmp");
    {
      std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
      f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
      if (!f) {
        error = "Could not write " + patched + " (game folder write-protected?).";
        gfs::remove(tmp, ec);
        return false;
      }
    }
    gfs::rename(tmp, dst, ec);
    if (ec) {
      error = "Could not create " + patched + ".";
      gfs::remove(tmp, ec);
      return false;
    }
  }
  return true;
}

inline bool EnsureGlyphVariant(const gfs::path& game_dir, const gfs::path& exe_dir,
                               const GlyphVariant& v, std::string& error) {
  return EnsureGlyphFonts(game_dir, exe_dir, v, error) && EnsureHudFiles(game_dir, exe_dir, v, error);
}

}  // namespace low::launcher
