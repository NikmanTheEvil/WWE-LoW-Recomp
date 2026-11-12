// LoWRecomp-Launcher - minimaler TOML-Zeileneditor
//
// Liest/aendert einzelne "schluessel = wert"-Zeilen in der Wurzel oder einem
// [abschnitt] und laesst alles andere (Kommentare, eigene Belegungen) stehen.
// Bewusst klein gehalten: lowrecomp.toml nutzt nur flache Werte.

#pragma once

#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace low::launcher {

class TomlEdit {
 public:
  bool Load(const std::string& utf8_text) {
    lines_.clear();
    std::istringstream in(utf8_text);
    std::string line;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      lines_.push_back(line);
    }
    // UTF-8-BOM entfernen
    if (!lines_.empty() && lines_[0].rfind("\xEF\xBB\xBF", 0) == 0) lines_[0].erase(0, 3);
    return true;
  }

  std::string Text() const {
    std::string out;
    for (const auto& l : lines_) {
      out += l;
      out += "\r\n";
    }
    return out;
  }

  // Roher Wert (ohne Kommentar), z. B. "true", "3", "\"xenos\"", "'C:\\x'"
  std::optional<std::string> GetRaw(std::string_view section, std::string_view key) const {
    const int i = Find(section, key);
    if (i < 0) return std::nullopt;
    return ValueOf(lines_[i]);
  }

  bool GetBool(std::string_view section, std::string_view key, bool fallback) const {
    auto v = GetRaw(section, key);
    if (!v) return fallback;
    if (*v == "true") return true;
    if (*v == "false") return false;
    return fallback;
  }

  long GetInt(std::string_view section, std::string_view key, long fallback) const {
    auto v = GetRaw(section, key);
    if (!v || v->empty()) return fallback;
    char* end = nullptr;
    const long r = std::strtol(v->c_str(), &end, 10);
    return (end && *end == '\0') ? r : fallback;
  }

  std::string GetString(std::string_view section, std::string_view key,
                        const std::string& fallback) const {
    auto v = GetRaw(section, key);
    if (!v || v->size() < 2) return fallback;
    const char q = v->front();
    if ((q != '"' && q != '\'') || v->back() != q) return fallback;
    std::string s = v->substr(1, v->size() - 2);
    if (q == '"') {  // Basis-Escapes
      std::string r;
      for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
          const char n = s[++i];
          r += n == 'n' ? '\n' : n == 't' ? '\t' : n;
        } else {
          r += s[i];
        }
      }
      return r;
    }
    return s;
  }

  void SetBool(std::string_view section, std::string_view key, bool v) {
    SetRaw(section, key, v ? "true" : "false");
  }
  void SetInt(std::string_view section, std::string_view key, long v) {
    SetRaw(section, key, std::to_string(v));
  }
  // Pfade als Literal-String ('...'), damit Backslashes unveraendert bleiben.
  void SetString(std::string_view section, std::string_view key, const std::string& v) {
    if (v.find('\'') == std::string::npos && v.find('\n') == std::string::npos) {
      SetRaw(section, key, "'" + v + "'");
    } else {
      std::string e = "\"";
      for (char c : v) {
        if (c == '"' || c == '\\') e += '\\';
        e += c;
      }
      SetRaw(section, key, e + "\"");
    }
  }

  void SetRaw(std::string_view section, std::string_view key, const std::string& value) {
    const std::string line = std::string(key) + " = " + value;
    const int i = Find(section, key);
    if (i >= 0) {
      // Kommentar am Zeilenende behalten
      const std::string comment = CommentOf(lines_[i]);
      lines_[i] = comment.empty() ? line : line + "  " + comment;
      return;
    }
    if (section.empty()) {
      // Wurzel-Schluessel muessen vor dem ersten [abschnitt] stehen: direkt davor
      // einfuegen, aber vor die Leerzeilen, die den Abschnitt abtrennen.
      size_t at = FirstHeader();
      while (at > 0 && Trim(lines_[at - 1]).empty()) --at;
      lines_.insert(lines_.begin() + static_cast<long>(at), line);
      return;
    }
    // ans Ende des Abschnitts (oder Abschnitt neu anlegen)
    int start = -1;
    for (size_t j = 0; j < lines_.size(); ++j) {
      if (HeaderName(lines_[j]) == section) {
        start = static_cast<int>(j);
        break;
      }
    }
    if (start < 0) {
      if (!lines_.empty() && !Trim(lines_.back()).empty()) lines_.push_back("");
      lines_.push_back("[" + std::string(section) + "]");
      lines_.push_back(line);
      return;
    }
    size_t end = static_cast<size_t>(start) + 1;
    size_t last_value = static_cast<size_t>(start);
    for (; end < lines_.size() && !IsHeader(lines_[end]); ++end) {
      const std::string_view t = Trim(lines_[end]);
      if (!t.empty() && t[0] != '#') last_value = end;
    }
    lines_.insert(lines_.begin() + static_cast<long>(last_value + 1), line);
  }

 private:
  static std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
  }
  static bool IsHeader(const std::string& l) {
    const std::string_view t = Trim(l);
    return !t.empty() && t[0] == '[';
  }
  static std::string HeaderName(const std::string& l) {
    std::string_view t = Trim(l);
    if (t.empty() || t[0] != '[') return {};
    const size_t close = t.find(']');
    if (close == std::string_view::npos) return {};
    return std::string(Trim(t.substr(1, close - 1)));
  }
  size_t FirstHeader() const {
    for (size_t j = 0; j < lines_.size(); ++j)
      if (IsHeader(lines_[j])) return j;
    return lines_.size();
  }
  // Position des '#', der einen Kommentar einleitet (nicht in Strings)
  static size_t CommentPos(const std::string& l) {
    char quote = 0;
    for (size_t i = 0; i < l.size(); ++i) {
      const char c = l[i];
      if (quote) {
        if (c == '\\' && quote == '"') {
          ++i;
        } else if (c == quote) {
          quote = 0;
        }
      } else if (c == '"' || c == '\'') {
        quote = c;
      } else if (c == '#') {
        return i;
      }
    }
    return std::string::npos;
  }
  static std::string CommentOf(const std::string& l) {
    const size_t p = CommentPos(l);
    return p == std::string::npos ? std::string() : l.substr(p);
  }
  static std::string KeyOf(const std::string& l) {
    const std::string_view t = Trim(l);
    if (t.empty() || t[0] == '#' || t[0] == '[') return {};
    const size_t eq = t.find('=');
    if (eq == std::string_view::npos) return {};
    return std::string(Trim(t.substr(0, eq)));
  }
  static std::string ValueOf(const std::string& l) {
    const size_t eq = l.find('=');
    if (eq == std::string::npos) return {};
    std::string v = l.substr(eq + 1);
    const size_t c = CommentPos(v);
    if (c != std::string::npos) v = v.substr(0, c);
    return std::string(Trim(v));
  }
  int Find(std::string_view section, std::string_view key) const {
    std::string current;
    for (size_t j = 0; j < lines_.size(); ++j) {
      if (IsHeader(lines_[j])) {
        current = HeaderName(lines_[j]);
        continue;
      }
      if (current == section && KeyOf(lines_[j]) == key) return static_cast<int>(j);
    }
    return -1;
  }

  std::vector<std::string> lines_;
};

}  // namespace low::launcher
