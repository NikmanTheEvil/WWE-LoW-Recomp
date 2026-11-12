// LoWRecomp-Launcher - STFS-Leser (Xbox-360-Pakete: CON / LIVE / PIRS)
//
// Liest Spielstand-Pakete, wie sie auf der Konsole bzw. auf USB-Sticks unter
// Content\<Profil>\<Titel-ID>\00000001\<Name> liegen, und entpackt ihre
// Dateien. Blockadressierung und Hash-Ketten folgen Xenias
// StfsContainerDevice (ReXGlue: src/filesystem/devices/stfs_container_device.cpp).
//
// Header-Offsets (Big Endian, gepackt):
//   0x000 Magic  0x340 header_size  0x344 content_type  0x360 title_id
//   0x379 Volume-Deskriptor (0x24)  0x3A9 volume_type (0 = STFS)
//   0x411 Anzeigename (9 Sprachen x 128 UTF-16BE-Zeichen)

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace low::launcher {

class StfsReader {
 public:
  struct Entry {
    std::string path;  // relativ, mit '/' getrennt
    bool directory = false;
    uint32_t size = 0;
    uint32_t start_block = 0;
    uint32_t allocated_blocks = 0;
    bool contiguous = false;
  };

  static constexpr uint32_t kMagicCon = 0x434F4E20;   // "CON "
  static constexpr uint32_t kMagicLive = 0x4C495645;  // "LIVE"
  static constexpr uint32_t kMagicPirs = 0x50495253;  // "PIRS"

  static bool HasStfsMagic(const uint8_t* data, size_t size) {
    if (size < 4) return false;
    const uint32_t m = Be32(data);
    return m == kMagicCon || m == kMagicLive || m == kMagicPirs;
  }

  // data muss die komplette Paketdatei enthalten.
  bool Open(std::vector<uint8_t> data, std::string& error) {
    data_ = std::move(data);
    entries_.clear();
    hash_cache_.clear();
    if (data_.size() < 0x971A) return Fail(error, "File too small for an Xbox 360 package.");
    if (!HasStfsMagic(data_.data(), data_.size()))
      return Fail(error, "Not an Xbox 360 package (CON/LIVE/PIRS).");
    header_size_ = Be32(&data_[0x340]);
    content_type_ = Be32(&data_[0x344]);
    title_id_ = Be32(&data_[0x360]);
    if (Be32(&data_[0x3A9]) != 0) return Fail(error, "Package is not STFS (SVOD is not supported).");

    const uint8_t* vd = &data_[0x379];
    read_only_ = (vd[2] & 0x01) != 0;
    root_active_index_ = (vd[2] & 0x02) != 0;
    file_table_block_count_ = uint32_t(vd[3]) | (uint32_t(vd[4]) << 8);  // Little Endian
    file_table_block_number_ = Le24(vd + 5);
    total_block_count_ = Be32(vd + 0x1C);

    blocks_per_hash_table_ = read_only_ ? 1 : 2;
    block_step_[0] = kPerLevel[0] + blocks_per_hash_table_;
    block_step_[1] = kPerLevel[1] + (kPerLevel[0] + 1) * blocks_per_hash_table_;

    // Anzeigename (erste Sprache mit Inhalt)
    display_name_.clear();
    for (uint32_t lang = 0; lang < 9 && display_name_.empty(); ++lang) {
      display_name_ = Utf16BeToUtf8(&data_[0x411 + lang * 256], 128);
    }

    return ReadFileTable(error);
  }

  uint32_t title_id() const { return title_id_; }
  uint32_t content_type() const { return content_type_; }
  const std::string& display_name() const { return display_name_; }
  const std::vector<Entry>& entries() const { return entries_; }

  bool Extract(const Entry& e, std::vector<uint8_t>& out, std::string& error) {
    out.clear();
    if (e.directory) return true;
    out.reserve(e.size);
    uint32_t block = e.start_block;
    uint32_t remaining = e.size;
    uint32_t guard = 0;
    while (remaining && block != kEndOfChain) {
      const uint64_t off = BlockToOffset(block);
      const uint32_t n = std::min<uint32_t>(kBlockSize, remaining);
      if (off + n > data_.size()) return Fail(error, "Package is damaged (block outside the file).");
      out.insert(out.end(), data_.begin() + long(off), data_.begin() + long(off + n));
      remaining -= n;
      if (!remaining) break;
      if (e.contiguous) {
        ++block;
      } else {
        const uint8_t* h = BlockHash(block);
        if (!h) return Fail(error, "Package is damaged (hash table missing).");
        block = Be32(h + 0x14) & 0xFFFFFF;
      }
      if (++guard > 0x1000000) return Fail(error, "Package is damaged (block chain does not end).");
    }
    if (remaining) return Fail(error, "Package is damaged (file incomplete): " + e.path);
    return true;
  }

 private:
  static constexpr uint32_t kBlockSize = 0x1000;
  static constexpr uint32_t kEndOfChain = 0xFFFFFF;
  static constexpr uint32_t kPerLevel[3] = {170, 28900, 4913000};

  static uint32_t Be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  static uint16_t Be16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
  static uint32_t Le24(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16); }

  static bool Fail(std::string& error, const std::string& msg) {
    error = msg;
    return false;
  }

  static std::string Utf16BeToUtf8(const uint8_t* p, size_t max_chars) {
    std::string out;
    for (size_t i = 0; i < max_chars; ++i) {
      uint32_t c = Be16(p + i * 2);
      if (!c) break;
      if (c >= 0xD800 && c <= 0xDBFF && i + 1 < max_chars) {
        const uint32_t lo = Be16(p + (i + 1) * 2);
        if (lo >= 0xDC00 && lo <= 0xDFFF) {
          c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
          ++i;
        }
      }
      if (c < 0x80) {
        out += char(c);
      } else if (c < 0x800) {
        out += char(0xC0 | (c >> 6));
        out += char(0x80 | (c & 0x3F));
      } else if (c < 0x10000) {
        out += char(0xE0 | (c >> 12));
        out += char(0x80 | ((c >> 6) & 0x3F));
        out += char(0x80 | (c & 0x3F));
      } else {
        out += char(0xF0 | (c >> 18));
        out += char(0x80 | ((c >> 12) & 0x3F));
        out += char(0x80 | ((c >> 6) & 0x3F));
        out += char(0x80 | (c & 0x3F));
      }
    }
    return out;
  }

  uint64_t DataStart() const { return (uint64_t(header_size_) + kBlockSize - 1) & ~uint64_t(kBlockSize - 1); }

  // = StfsContainerDevice::BlockToOffsetSTFS
  uint64_t BlockToOffset(uint64_t block_index) const {
    uint64_t base = kPerLevel[0];
    uint64_t block = block_index;
    for (int i = 0; i < 3; ++i) {
      block += ((block_index + base) / base) * blocks_per_hash_table_;
      if (block_index < base) break;
      base *= kPerLevel[0];
    }
    return DataStart() + (block << 12);
  }

  // = StfsContainerDevice::BlockToHashBlockNumberSTFS
  uint32_t HashBlockNumber(uint32_t block_index, uint32_t level) const {
    if (level == 0) {
      if (block_index < kPerLevel[0]) return 0;
      uint32_t block = (block_index / kPerLevel[0]) * block_step_[0];
      block += ((block_index / kPerLevel[1]) + 1) * blocks_per_hash_table_;
      if (block_index < kPerLevel[1]) return block;
      return block + blocks_per_hash_table_;
    }
    if (level == 1) {
      if (block_index < kPerLevel[1]) return block_step_[0];
      return (block_index / kPerLevel[1]) * block_step_[1] + blocks_per_hash_table_;
    }
    return block_step_[1];
  }
  uint64_t HashBlockOffset(uint32_t block_index, uint32_t level) const {
    return DataStart() + (uint64_t(HashBlockNumber(block_index, level)) << 12);
  }

  const uint8_t* Table(uint64_t offset) const {
    return offset + kBlockSize <= data_.size() ? &data_[offset] : nullptr;
  }

  // = StfsContainerDevice::GetBlockHash: liefert den 0x18-Byte-Eintrag für block_index.
  // Bei Paketen mit zwei Tabellen-Kopien entscheidet das "active index"-Bit der
  // naechsthoeheren Ebene (bzw. root_active_index), welche Kopie gilt.
  const uint8_t* BlockHash(uint32_t block_index) {
    const uint64_t lv0 = HashBlockOffset(block_index, 0);
    auto it = hash_cache_.find(lv0);
    if (it == hash_cache_.end()) {
      uint64_t secondary = root_active_index_ ? kBlockSize : 0;
      if (read_only_) {
        secondary = 0;
      } else if (total_block_count_ > kPerLevel[0]) {
        const uint64_t lv1 = HashBlockOffset(block_index, 1);
        if (total_block_count_ > kPerLevel[1]) {
          const uint64_t lv2 = HashBlockOffset(block_index, 2);
          const uint8_t* t2 = Table(lv2 + secondary);
          if (!t2) return nullptr;
          const uint32_t rec = (block_index / kPerLevel[1]) % kPerLevel[0];
          secondary = (Be32(t2 + rec * 0x18 + 0x14) & 0x40000000) ? kBlockSize : 0;
        }
        const uint8_t* t1 = Table(lv1 + secondary);
        if (!t1) return nullptr;
        const uint32_t rec = (block_index / kPerLevel[0]) % kPerLevel[0];
        secondary = (Be32(t1 + rec * 0x18 + 0x14) & 0x40000000) ? kBlockSize : 0;
      }
      it = hash_cache_.emplace(lv0, lv0 + secondary).first;
    }
    const uint8_t* t0 = Table(it->second);
    if (!t0) return nullptr;
    return t0 + (block_index % kPerLevel[0]) * 0x18;
  }

  bool ReadFileTable(std::string& error) {
    std::vector<std::string> paths;  // Index -> Pfad (fuer Unterordner)
    uint32_t block = file_table_block_number_;
    for (uint32_t n = 0; n < file_table_block_count_ && block != kEndOfChain; ++n) {
      const uint64_t off = BlockToOffset(block);
      if (off + kBlockSize > data_.size()) return Fail(error, "The package's file table is damaged.");
      for (uint32_t m = 0; m < 64; ++m) {
        const uint8_t* e = &data_[off + m * 0x40];
        if (e[0] == 0) break;
        const uint8_t flags = e[0x28];
        std::string name(reinterpret_cast<const char*>(e), std::min<size_t>(flags & 0x3F, 40));
        const uint16_t parent = Be16(e + 0x32);
        std::string path = name;
        if (parent != 0xFFFF) {
          if (parent >= paths.size()) return Fail(error, "The package's file table is damaged.");
          path = paths[parent] + "/" + name;
        }
        paths.push_back(path);
        Entry entry;
        entry.path = path;
        entry.directory = (flags & 0x80) != 0;
        entry.contiguous = (flags & 0x40) != 0;
        entry.allocated_blocks = Le24(e + 0x2C);
        entry.start_block = Le24(e + 0x2F);
        entry.size = Be32(e + 0x34);
        entries_.push_back(entry);
      }
      const uint8_t* h = BlockHash(block);
      if (!h) return Fail(error, "The package's hash table is damaged.");
      block = Be32(h + 0x14) & 0xFFFFFF;
    }
    if (entries_.empty()) return Fail(error, "Package contains no files.");
    return true;
  }

  std::vector<uint8_t> data_;
  std::vector<Entry> entries_;
  std::map<uint64_t, uint64_t> hash_cache_;  // lv0-Offset -> tatsaechlich gueltige Kopie
  uint32_t header_size_ = 0, content_type_ = 0, title_id_ = 0;
  bool read_only_ = false, root_active_index_ = false;
  uint32_t file_table_block_count_ = 0, file_table_block_number_ = 0, total_block_count_ = 0;
  uint32_t blocks_per_hash_table_ = 1;
  uint32_t block_step_[2] = {0, 0};
  std::string display_name_;
};

}  // namespace low::launcher
