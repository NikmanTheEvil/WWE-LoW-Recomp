// LoWRecomp - Eingabe (siehe low_input.h)
//
// Belegung der Original-Konsole (aus den Spieltexten in pac\string.pac und
// Reviews abgeleitet, Details: docs/CONTROLS.md):
//   Linker Stick  Bewegen          X   Strike          A   Grapple / Irish Whip
//   Y             Reversal/Block   B   Kontext (Pin, Ring verlassen, Klettern)
//   X + A         Finisher         B + Y  Taunt        RT (+Stick)  Rennen
//   Rechter Stick Quick Grapple    LT + rechter Stick  Strong/Ultimate Grapple
//   LB / RB + Stick  Ausweichrolle Steuerkreuz Taunts
//
// Diese Datei bildet darauf eine Tastatur/Maus-Steuerung im Stil der
// WWE-2K-PC-Versionen ab (WASD, Shift = Rennen, J/K = Schlaege, L = Griff,
// I = Konter, Leertaste = Spezial-Modifikator).

#include "low_input.h"

#include <algorithm>
#include <thread>
#include <chrono>
#include <cctype>
#include <atomic>
#include <array>
#include <bitset>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <vector>

#include <rex/cvar.h>
#include <rex/input/device_assignment.h>
#include <rex/input/input.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/input/nop/nop_input_driver.h>
#include <rex/input/sdl/sdl_input_driver.h>
#include <rex/logging.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

// ---------------------------------------------------------------------------
// Optionen (in lowrecomp.toml unter [low], z. B. "kbm = false")
// ---------------------------------------------------------------------------
REXCVAR_DEFINE_BOOL(low_kbm, true, "LoW/Input",
                    "Tastatur/Maus-Steuerung im WWE-2K-Stil aktivieren");
// Belegung laut Spieltext + Schrifttabelle (U+E00B=LB, E00E=RB, E00C=LT, E00F=RT):
//   Rennen = LB + Stick, starker Griff = RB halten, Ausweichen = LT oder RT.
// low_swap_triggers stellt die fruehere Annahme her (Rennen RT, Modifikator LT,
// Ausweichen LB/RB) - nur als Rueckfalloption.
REXCVAR_DEFINE_BOOL(low_swap_triggers, false, "LoW/Input",
                    "Fruehere Belegung: Rennen auf RT, Modifikator auf LT, Ausweichen auf LB/RB");
REXCVAR_DEFINE_BOOL(low_single_player_input, false, "LoW/Input",
                    "Alle Geraete steuern Spieler 1 (hilft bei DS4Windows, das den "
                    "Controller doppelt meldet)");

// Belegungen: kommagetrennte Alternativen, "A+B" = Akkord.
// Tastennamen wie in ReXGlue (A-Z, 0-9, F1-F24, Space, Return, Escape, Tab,
// Backspace, Up/Down/Left/Right, Shift, Control, Alt, LMB, RMB, MMB ...)
// plus Mouse4 / Mouse5 (Seitentasten).
REXCVAR_DEFINE_STRING(low_bind_move_up, "W", "LoW/Input/Bind", "Bewegen hoch (linker Stick)");
REXCVAR_DEFINE_STRING(low_bind_move_down, "S", "LoW/Input/Bind", "Bewegen runter (linker Stick)");
REXCVAR_DEFINE_STRING(low_bind_move_left, "A", "LoW/Input/Bind", "Bewegen links (linker Stick)");
REXCVAR_DEFINE_STRING(low_bind_move_right, "D", "LoW/Input/Bind", "Bewegen rechts (linker Stick)");
REXCVAR_DEFINE_STRING(low_bind_run, "Shift", "LoW/Input/Bind", "Rennen (LB halten)");
REXCVAR_DEFINE_STRING(low_bind_strike, "Left,LMB", "LoW/Input/Bind", "Strike (X)");
REXCVAR_DEFINE_STRING(low_bind_grapple, "Down,RMB", "LoW/Input/Bind",
                      "Grapple / Irish Whip mit Richtung (A)");
REXCVAR_DEFINE_STRING(low_bind_reversal, "Up,MMB", "LoW/Input/Bind", "Reversal / Block (Y)");
REXCVAR_DEFINE_STRING(low_bind_action, "Right,Mouse4", "LoW/Input/Bind",
                      "Kontext: Pin, Klettern, Ring betreten/verlassen, aufheben (B)");
REXCVAR_DEFINE_STRING(low_bind_finisher, "Mouse5", "LoW/Input/Bind",
                      "Finisher / Signature (X + A; auch Links + Runter gleichzeitig)");
REXCVAR_DEFINE_STRING(low_bind_taunt, "2", "LoW/Input/Bind", "Taunt (B + Y)");
REXCVAR_DEFINE_STRING(low_bind_modifier, "Control", "LoW/Input/Bind",
                      "Modifikator: Strong/Ultimate Grapple, Move Theft (RB)");
REXCVAR_DEFINE_STRING(low_bind_dodge_left, "Q", "LoW/Input/Bind", "Ausweichen (LT)");
REXCVAR_DEFINE_STRING(low_bind_dodge_right, "E", "LoW/Input/Bind", "Ausweichen (RT)");
REXCVAR_DEFINE_STRING(low_bind_grapple_up, "Space+Up", "LoW/Input/Bind", "Quick Grapple hoch (rechter Stick)");
REXCVAR_DEFINE_STRING(low_bind_grapple_down, "Space+Down", "LoW/Input/Bind", "Quick Grapple runter (rechter Stick)");
REXCVAR_DEFINE_STRING(low_bind_grapple_left, "Space+Left", "LoW/Input/Bind", "Quick Grapple links (rechter Stick)");
REXCVAR_DEFINE_STRING(low_bind_grapple_right, "Space+Right", "LoW/Input/Bind", "Quick Grapple rechts (rechter Stick)");
REXCVAR_DEFINE_STRING(low_bind_dpad_up, "1", "LoW/Input/Bind", "Steuerkreuz hoch (Taunt)");
REXCVAR_DEFINE_STRING(low_bind_dpad_right, "3", "LoW/Input/Bind", "Steuerkreuz rechts (Taunt)");
REXCVAR_DEFINE_STRING(low_bind_dpad_down, "4", "LoW/Input/Bind", "Steuerkreuz runter (Taunt)");
REXCVAR_DEFINE_STRING(low_bind_dpad_left, "5", "LoW/Input/Bind", "Steuerkreuz links (Taunt)");
REXCVAR_DEFINE_STRING(low_bind_confirm, "Return", "LoW/Input/Bind", "Menue bestaetigen (A)");
REXCVAR_DEFINE_STRING(low_bind_cancel, "Backspace", "LoW/Input/Bind", "Menue zurueck (B)");
REXCVAR_DEFINE_STRING(low_bind_pause, "Escape", "LoW/Input/Bind", "Pause / Start");
REXCVAR_DEFINE_STRING(low_bind_select, "Tab", "LoW/Input/Bind", "Back / Select");
REXCVAR_DEFINE_STRING(low_bind_l3, "F", "LoW/Input/Bind", "Linken Stick druecken (L3)");
REXCVAR_DEFINE_STRING(low_bind_r3, "V", "LoW/Input/Bind", "Rechten Stick druecken (R3)");

namespace low {

using rex::X_RESULT;
using rex::X_STATUS;

namespace {

using rex::input::DeviceId;
using rex::input::DeviceInfo;
using rex::input::X_INPUT_CAPABILITIES;
using rex::input::X_INPUT_KEYSTROKE;
using rex::input::X_INPUT_STATE;
using rex::input::X_INPUT_VIBRATION;
using rex::ui::VirtualKey;

constexpr DeviceId kLowKbmDevice = static_cast<DeviceId>(0x4C4F574B00ull);  // "LOWK"
constexpr size_t kMaxQueuedKeystrokes = 256;

// Was eine Aktion am virtuellen Controller ausloest.
struct PadOutput {
  uint16_t buttons = 0;
  bool lt = false;
  bool rt = false;
  int8_t lx = 0, ly = 0;  // -1 / 0 / +1
  int8_t rx = 0, ry = 0;
};

struct Action {
  std::string& (*binding)();
  PadOutput out;
};

// Reihenfolge egal; jede Aktion kann mehrere Controller-Elemente setzen.
const std::array<Action, 30>& Actions() {
  using namespace rex::input;
  static const std::array<Action, 30> actions = {{
      {&FLAGS_low_bind_move_up_storage_, {.ly = 1}},
      {&FLAGS_low_bind_move_down_storage_, {.ly = -1}},
      {&FLAGS_low_bind_move_left_storage_, {.lx = -1}},
      {&FLAGS_low_bind_move_right_storage_, {.lx = 1}},
      {&FLAGS_low_bind_run_storage_, {.buttons = X_INPUT_GAMEPAD_LEFT_SHOULDER}},
      {&FLAGS_low_bind_strike_storage_, {.buttons = X_INPUT_GAMEPAD_X}},
      {&FLAGS_low_bind_grapple_storage_, {.buttons = X_INPUT_GAMEPAD_A}},
      {&FLAGS_low_bind_reversal_storage_, {.buttons = X_INPUT_GAMEPAD_Y}},
      {&FLAGS_low_bind_action_storage_, {.buttons = X_INPUT_GAMEPAD_B}},
      {&FLAGS_low_bind_finisher_storage_, {.buttons = X_INPUT_GAMEPAD_X | X_INPUT_GAMEPAD_A}},
      {&FLAGS_low_bind_taunt_storage_, {.buttons = X_INPUT_GAMEPAD_B | X_INPUT_GAMEPAD_Y}},
      {&FLAGS_low_bind_modifier_storage_, {.buttons = X_INPUT_GAMEPAD_RIGHT_SHOULDER}},
      {&FLAGS_low_bind_dodge_left_storage_, {.lt = true}},
      {&FLAGS_low_bind_dodge_right_storage_, {.rt = true}},
      {&FLAGS_low_bind_grapple_up_storage_, {.ry = 1}},
      {&FLAGS_low_bind_grapple_down_storage_, {.ry = -1}},
      {&FLAGS_low_bind_grapple_left_storage_, {.rx = -1}},
      {&FLAGS_low_bind_grapple_right_storage_, {.rx = 1}},
      {&FLAGS_low_bind_dpad_up_storage_, {.buttons = X_INPUT_GAMEPAD_DPAD_UP}},
      {&FLAGS_low_bind_dpad_right_storage_, {.buttons = X_INPUT_GAMEPAD_DPAD_RIGHT}},
      {&FLAGS_low_bind_dpad_down_storage_, {.buttons = X_INPUT_GAMEPAD_DPAD_DOWN}},
      {&FLAGS_low_bind_dpad_left_storage_, {.buttons = X_INPUT_GAMEPAD_DPAD_LEFT}},
      {&FLAGS_low_bind_confirm_storage_, {.buttons = X_INPUT_GAMEPAD_A}},
      {&FLAGS_low_bind_cancel_storage_, {.buttons = X_INPUT_GAMEPAD_B}},
      {&FLAGS_low_bind_pause_storage_, {.buttons = X_INPUT_GAMEPAD_START}},
      {&FLAGS_low_bind_select_storage_, {.buttons = X_INPUT_GAMEPAD_BACK}},
      {&FLAGS_low_bind_l3_storage_, {.buttons = X_INPUT_GAMEPAD_LEFT_THUMB}},
      {&FLAGS_low_bind_r3_storage_, {.buttons = X_INPUT_GAMEPAD_RIGHT_THUMB}},
      // Platzhalter, damit die Tabelle eine feste Groesse behaelt
      {nullptr, {}},
      {nullptr, {}},
  }};
  return actions;
}

// Eine Alternative einer Belegung: alle Tasten muessen gedrueckt sein.
struct Alternative {
  std::vector<uint8_t> keys;
  size_t action;
};

std::string_view Trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

// Tastenname -> VirtualKey-Index (0 = unbekannt).
uint8_t ParseKey(std::string_view name) {
  if (name == "Ctrl" || name == "Strg") return static_cast<uint8_t>(VirtualKey::kControl);
  if (name == "Mouse4" || name == "XButton1") return static_cast<uint8_t>(VirtualKey::kXButton1);
  if (name == "Mouse5" || name == "XButton2") return static_cast<uint8_t>(VirtualKey::kXButton2);
  const auto vk = static_cast<uint16_t>(rex::ui::ParseVirtualKey(name));
  return vk < 256 ? static_cast<uint8_t>(vk) : 0;
}

// Liest alle Belegungen; Fehler werden einmalig gemeldet.
std::vector<Alternative> ParseBindings(std::vector<std::string>& snapshot) {
  std::vector<Alternative> alts;
  const auto& actions = Actions();
  snapshot.assign(actions.size(), {});
  for (size_t i = 0; i < actions.size(); ++i) {
    if (!actions[i].binding) continue;
    const std::string& text = actions[i].binding();
    snapshot[i] = text;
    std::string_view rest(text);
    while (!rest.empty()) {
      const size_t comma = rest.find(',');
      std::string_view alt = Trim(rest.substr(0, comma));
      rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
      if (alt.empty()) continue;
      Alternative a{{}, i};
      bool ok = true;
      while (!alt.empty()) {
        const size_t plus = alt.find('+', 1);  // "+" als erstes Zeichen waere der Name "Plus"
        const std::string_view name = Trim(alt.substr(0, plus));
        alt = plus == std::string_view::npos ? std::string_view() : alt.substr(plus + 1);
        const uint8_t key = ParseKey(name);
        if (!key) {
          REXLOG_WARN("LoW-Eingabe: unbekannte Taste '{}' in Belegung '{}'", name, text);
          ok = false;
          break;
        }
        a.keys.push_back(key);
      }
      if (ok && !a.keys.empty()) alts.push_back(std::move(a));
    }
  }
  // Akkorde zuerst, damit sie ihre Tasten fuer Einzelbelegungen sperren.
  std::stable_sort(alts.begin(), alts.end(), [](const Alternative& x, const Alternative& y) {
    return x.keys.size() > y.keys.size();
  });
  return alts;
}

// Virtuelle Controller-Tasten fuer XamInputGetKeystroke(Ex) - Menues lesen das.
struct PadKey {
  VirtualKey vk;
  bool (*pressed)(const PadOutput&, uint16_t);
};

const std::array<PadKey, 26>& PadKeys() {
  using namespace rex::input;
#define LOW_BTN(bit) [](const PadOutput&, uint16_t b) { return (b & (bit)) != 0; }
  static const std::array<PadKey, 26> keys = {{
      {VirtualKey::kXInputPadA, LOW_BTN(X_INPUT_GAMEPAD_A)},
      {VirtualKey::kXInputPadB, LOW_BTN(X_INPUT_GAMEPAD_B)},
      {VirtualKey::kXInputPadX, LOW_BTN(X_INPUT_GAMEPAD_X)},
      {VirtualKey::kXInputPadY, LOW_BTN(X_INPUT_GAMEPAD_Y)},
      {VirtualKey::kXInputPadLShoulder, LOW_BTN(X_INPUT_GAMEPAD_LEFT_SHOULDER)},
      {VirtualKey::kXInputPadRShoulder, LOW_BTN(X_INPUT_GAMEPAD_RIGHT_SHOULDER)},
      {VirtualKey::kXInputPadDpadUp, LOW_BTN(X_INPUT_GAMEPAD_DPAD_UP)},
      {VirtualKey::kXInputPadDpadDown, LOW_BTN(X_INPUT_GAMEPAD_DPAD_DOWN)},
      {VirtualKey::kXInputPadDpadLeft, LOW_BTN(X_INPUT_GAMEPAD_DPAD_LEFT)},
      {VirtualKey::kXInputPadDpadRight, LOW_BTN(X_INPUT_GAMEPAD_DPAD_RIGHT)},
      {VirtualKey::kXInputPadStart, LOW_BTN(X_INPUT_GAMEPAD_START)},
      {VirtualKey::kXInputPadBack, LOW_BTN(X_INPUT_GAMEPAD_BACK)},
      {VirtualKey::kXInputPadLThumbPress, LOW_BTN(X_INPUT_GAMEPAD_LEFT_THUMB)},
      {VirtualKey::kXInputPadRThumbPress, LOW_BTN(X_INPUT_GAMEPAD_RIGHT_THUMB)},
      {VirtualKey::kXInputPadLTrigger, [](const PadOutput& o, uint16_t) { return o.lt; }},
      {VirtualKey::kXInputPadRTrigger, [](const PadOutput& o, uint16_t) { return o.rt; }},
      {VirtualKey::kXInputPadLThumbUp, [](const PadOutput& o, uint16_t) { return o.ly > 0; }},
      {VirtualKey::kXInputPadLThumbDown, [](const PadOutput& o, uint16_t) { return o.ly < 0; }},
      {VirtualKey::kXInputPadLThumbLeft, [](const PadOutput& o, uint16_t) { return o.lx < 0; }},
      {VirtualKey::kXInputPadLThumbRight, [](const PadOutput& o, uint16_t) { return o.lx > 0; }},
      {VirtualKey::kXInputPadRThumbUp, [](const PadOutput& o, uint16_t) { return o.ry > 0; }},
      {VirtualKey::kXInputPadRThumbDown, [](const PadOutput& o, uint16_t) { return o.ry < 0; }},
      {VirtualKey::kXInputPadRThumbLeft, [](const PadOutput& o, uint16_t) { return o.rx < 0; }},
      {VirtualKey::kXInputPadRThumbRight, [](const PadOutput& o, uint16_t) { return o.rx > 0; }},
      {VirtualKey::kNone, nullptr},
      {VirtualKey::kNone, nullptr},
  }};
#undef LOW_BTN
  return keys;
}

class LowKbmDriver final : public rex::input::InputDriver,
                           public rex::ui::WindowInputListener,
                           public rex::ui::WindowListener {
 public:
  LowKbmDriver() : InputDriver(nullptr, 0) {}
  ~LowKbmDriver() override { DetachFromWindow(); }

  X_STATUS Setup() override {
    std::lock_guard lock(mutex_);
    RefreshBindingsLocked(/*log=*/true);
    return X_STATUS_SUCCESS;
  }

  void EnumerateDevices(std::vector<DeviceInfo>& out) override {
    if (!REXCVAR_GET(low_kbm)) return;
    DeviceInfo info;
    info.id = kLowKbmDevice;
    info.name = "Tastatur und Maus (LoW, 2K-Stil)";
    info.subtype = rex::input::XINPUT_DEVSUBTYPE_GAMEPAD;
    info.synthetic = true;  // immer Spieler 1, zusaetzlich zum ersten Controller
    out.push_back(info);
  }

  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) override {
    if (!REXCVAR_GET(low_kbm) || id != kLowKbmDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    std::lock_guard lock(mutex_);
    PadOutput o;
    uint16_t buttons = 0;
    if (has_focus_ && is_active()) {
      RefreshBindingsLocked(false);
      Evaluate(o, buttons);
    }
    if (out_state) {
      std::memset(out_state, 0, sizeof(*out_state));
      out_state->packet_number = ++packet_number_;
      out_state->gamepad.buttons = buttons;
      out_state->gamepad.left_trigger = o.lt ? 0xFF : 0;
      out_state->gamepad.right_trigger = o.rt ? 0xFF : 0;
      out_state->gamepad.thumb_lx = Axis(o.lx);
      out_state->gamepad.thumb_ly = Axis(o.ly);
      out_state->gamepad.thumb_rx = Axis(o.rx);
      out_state->gamepad.thumb_ry = Axis(o.ry);
    }
    return X_ERROR_SUCCESS;
  }

  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t, X_INPUT_CAPABILITIES* caps) override {
    if (!REXCVAR_GET(low_kbm) || id != kLowKbmDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (caps) {
      std::memset(caps, 0, sizeof(*caps));
      caps->type = rex::input::XINPUT_DEVTYPE_GAMEPAD;
      caps->sub_type = rex::input::XINPUT_DEVSUBTYPE_GAMEPAD;
      caps->gamepad.buttons = 0xFFFF;
      caps->gamepad.left_trigger = 0xFF;
      caps->gamepad.right_trigger = 0xFF;
      caps->gamepad.thumb_lx = static_cast<int16_t>(0x7FFF);
      caps->gamepad.thumb_ly = static_cast<int16_t>(0x7FFF);
      caps->gamepad.thumb_rx = static_cast<int16_t>(0x7FFF);
      caps->gamepad.thumb_ry = static_cast<int16_t>(0x7FFF);
    }
    return X_ERROR_SUCCESS;
  }

  X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION*) override {
    return (REXCVAR_GET(low_kbm) && id == kLowKbmDevice) ? X_ERROR_SUCCESS
                                                         : X_ERROR_DEVICE_NOT_CONNECTED;
  }

  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t, X_INPUT_KEYSTROKE* out) override {
    if (!REXCVAR_GET(low_kbm) || id != kLowKbmDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    std::lock_guard lock(mutex_);
    if (keystrokes_.empty()) return X_ERROR_EMPTY;
    if (out) *out = keystrokes_.front();
    keystrokes_.pop();
    return X_ERROR_SUCCESS;
  }

  void OnWindowAvailable(rex::ui::Window* window) override {
    if (!window) return;
    {
      std::lock_guard lock(mutex_);
      window_ = window;
    }
    window->AddInputListener(this, window_z_order());
    window->AddListener(this);
  }

  // --- Fensterereignisse (UI-Thread) ---
  void OnKeyDown(rex::ui::KeyEvent& e) override { SetKey(static_cast<uint16_t>(e.virtual_key()), true); }
  void OnKeyUp(rex::ui::KeyEvent& e) override { SetKey(static_cast<uint16_t>(e.virtual_key()), false); }
  void OnMouseDown(rex::ui::MouseEvent& e) override { SetKey(MouseKey(e), true); }
  void OnMouseUp(rex::ui::MouseEvent& e) override { SetKey(MouseKey(e), false); }

  void OnClosing(rex::ui::UIEvent&) override { DetachFromWindow(); }
  void OnLostFocus(rex::ui::UISetupEvent&) override {
    std::lock_guard lock(mutex_);
    has_focus_ = false;
    down_.reset();  // keine "klemmenden" Tasten nach Alt+Tab
    RefreshKeystrokesLocked();
  }
  void OnGotFocus(rex::ui::UISetupEvent&) override {
    std::lock_guard lock(mutex_);
    has_focus_ = true;
  }

 private:
  static int16_t Axis(int8_t v) { return v > 0 ? int16_t(0x7FFF) : v < 0 ? int16_t(-0x7FFF) : 0; }

  static uint16_t MouseKey(const rex::ui::MouseEvent& e) {
    using B = rex::ui::MouseEvent::Button;
    switch (e.button()) {
      case B::kLeft: return static_cast<uint16_t>(VirtualKey::kLButton);
      case B::kRight: return static_cast<uint16_t>(VirtualKey::kRButton);
      case B::kMiddle: return static_cast<uint16_t>(VirtualKey::kMButton);
      case B::kX1: return static_cast<uint16_t>(VirtualKey::kXButton1);
      case B::kX2: return static_cast<uint16_t>(VirtualKey::kXButton2);
      default: return 0;
    }
  }

  void SetKey(uint16_t vk, bool down) {
    if (!REXCVAR_GET(low_kbm) || vk == 0 || vk >= 256) return;
    std::lock_guard lock(mutex_);
    if (down && !has_focus_) return;
    down_[vk] = down;
    RefreshKeystrokesLocked();
  }

  // Belegungen neu einlesen, wenn sich eine CVar geaendert hat (Konsole/Settings).
  void RefreshBindingsLocked(bool log) {
    const auto& actions = Actions();
    bool changed = snapshot_.size() != actions.size();
    for (size_t i = 0; !changed && i < actions.size(); ++i) {
      if (actions[i].binding && actions[i].binding() != snapshot_[i]) changed = true;
    }
    if (!changed && !log) return;
    alternatives_ = ParseBindings(snapshot_);
    if (log) {
      REXLOG_INFO("LoW-Eingabe: Tastatur/Maus {} ({} Belegungen)",
                  REXCVAR_GET(low_kbm) ? "aktiv" : "aus", alternatives_.size());
    }
  }

  void Evaluate(PadOutput& o, uint16_t& buttons) const {
    std::bitset<256> consumed;
    int lx = 0, ly = 0, rx = 0, ry = 0;
    const auto& actions = Actions();
    for (const auto& alt : alternatives_) {
      bool active = true;
      for (uint8_t k : alt.keys) {
        if (!down_[k] || consumed[k]) { active = false; break; }
      }
      if (!active) continue;
      if (alt.keys.size() > 1) {
        for (uint8_t k : alt.keys) consumed[k] = true;
      }
      const PadOutput& a = actions[alt.action].out;
      buttons |= a.buttons;
      o.lt |= a.lt;
      o.rt |= a.rt;
      lx += a.lx; ly += a.ly; rx += a.rx; ry += a.ry;
    }
    if (REXCVAR_GET(low_swap_triggers)) {
      // fruehere Belegung: LB->RT, RB->LT, LT->LB, RT->RB
      using rex::input::X_INPUT_GAMEPAD_LEFT_SHOULDER;
      using rex::input::X_INPUT_GAMEPAD_RIGHT_SHOULDER;
      const bool lb = buttons & X_INPUT_GAMEPAD_LEFT_SHOULDER;
      const bool rb = buttons & X_INPUT_GAMEPAD_RIGHT_SHOULDER;
      buttons &= uint16_t(~(X_INPUT_GAMEPAD_LEFT_SHOULDER | X_INPUT_GAMEPAD_RIGHT_SHOULDER));
      if (o.lt) buttons |= X_INPUT_GAMEPAD_LEFT_SHOULDER;
      if (o.rt) buttons |= X_INPUT_GAMEPAD_RIGHT_SHOULDER;
      o.lt = rb;
      o.rt = lb;
    }
    o.lx = static_cast<int8_t>(std::clamp(lx, -1, 1));
    o.ly = static_cast<int8_t>(std::clamp(ly, -1, 1));
    o.rx = static_cast<int8_t>(std::clamp(rx, -1, 1));
    o.ry = static_cast<int8_t>(std::clamp(ry, -1, 1));
  }

  // Unterschiede zum letzten Zustand als Controller-Tastendruecke einreihen.
  void RefreshKeystrokesLocked() {
    PadOutput o;
    uint16_t buttons = 0;
    RefreshBindingsLocked(false);
    Evaluate(o, buttons);
    const auto& keys = PadKeys();
    uint32_t pressed = 0;
    for (size_t i = 0; i < keys.size(); ++i) {
      if (keys[i].pressed && keys[i].pressed(o, buttons)) pressed |= 1u << i;
    }
    const uint32_t changed = pressed ^ pad_pressed_;
    pad_pressed_ = pressed;
    for (size_t i = 0; changed && i < keys.size(); ++i) {
      if (!(changed & (1u << i))) continue;
      X_INPUT_KEYSTROKE ks{};
      ks.virtual_key = static_cast<uint16_t>(keys[i].vk);
      ks.unicode = 0;
      ks.flags = static_cast<uint16_t>((pressed & (1u << i)) ? rex::input::X_INPUT_KEYSTROKE_KEYDOWN
                                                             : rex::input::X_INPUT_KEYSTROKE_KEYUP);
      ks.user_index = 0;  // InputSystem traegt den echten Spieler ein
      ks.hid_code = 0;
      if (keystrokes_.size() >= kMaxQueuedKeystrokes) keystrokes_.pop();
      keystrokes_.push(ks);
    }
  }

  void DetachFromWindow() {
    rex::ui::Window* window = nullptr;
    {
      std::lock_guard lock(mutex_);
      window = window_;
    }
    if (!window) return;
    window->app_context().CallInUIThreadSynchronous([this, window] {
      {
        std::lock_guard lock(mutex_);
        window_ = nullptr;
      }
      window->RemoveInputListener(this);
      window->RemoveListener(this);
    });
  }

  mutable std::mutex mutex_;
  rex::ui::Window* window_ = nullptr;
  std::bitset<256> down_;
  bool has_focus_ = true;
  std::vector<Alternative> alternatives_;
  std::vector<std::string> snapshot_;
  std::queue<X_INPUT_KEYSTROKE> keystrokes_;
  uint32_t pad_pressed_ = 0;
  uint32_t packet_number_ = 0;
};

}  // namespace

namespace {
// Gehoert dem InputSystem (lebt bis Programmende); nur fuer die Geraeteabfrage.
std::atomic<rex::input::sdl::SDLInputDriver*> g_sdl_driver{nullptr};

bool IsPlayStationDevice(const rex::input::DeviceInfo& d) {
  std::string name = d.name;
  for (auto& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
  for (const char* key : {"playstation", "dualshock", "dualsense", "ps3", "ps4", "ps5", "sony"}) {
    if (name.find(key) != std::string::npos) return true;
  }
  // SDL-GUID: Bytes 4-5 = Hersteller (Little Endian) -> Sony 0x054C = "4c05"
  std::string guid = d.guid;
  for (auto& c : guid) c = char(std::tolower(static_cast<unsigned char>(c)));
  return guid.size() >= 12 && guid.compare(8, 4, "4c05") == 0;
}
}  // namespace

PadKind ConnectedPadKind(int wait_ms) {
  auto* sdl = g_sdl_driver.load();
  if (!sdl) return PadKind::kNone;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
  for (;;) {
    std::vector<rex::input::DeviceInfo> devices;
    sdl->EnumerateDevices(devices);
    for (const auto& d : devices) {
      if (IsPlayStationDevice(d)) {
        REXLOG_INFO("LoW-Eingabe: PlayStation-Controller erkannt ({})", d.name);
        return PadKind::kPlayStation;
      }
    }
    if (!devices.empty()) {
      REXLOG_INFO("LoW-Eingabe: Controller erkannt ({})", devices.front().name);
      return PadKind::kXbox;
    }
    if (std::chrono::steady_clock::now() >= deadline) return PadKind::kNone;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
}

std::unique_ptr<rex::system::IInputSystem> CreateInputSystem(bool tool_mode) {
  if (tool_mode) {
    return rex::input::CreateDefaultInputSystem(true);
  }
  auto input = std::make_unique<rex::input::InputSystem>(nullptr);

  // Controller: SDL3 (XInput, GameInput, HIDAPI fuer DualShock 4 / DualSense,
  // RawInput, WGI, DirectInput). Vibration laeuft ebenfalls darueber.
  auto sdl = std::make_unique<rex::input::sdl::SDLInputDriver>(nullptr, 0);
  if (sdl->Setup() == X_STATUS_SUCCESS) {
    g_sdl_driver.store(sdl.get());
    input->AddDriver(std::move(sdl));
  } else {
    REXLOG_WARN("LoW-Eingabe: SDL-Controllertreiber konnte nicht starten");
  }

  auto kbm = std::make_unique<LowKbmDriver>();
  if (kbm->Setup() == X_STATUS_SUCCESS) {
    input->AddDriver(std::move(kbm));
  }

  // Fallback wie bei ReXGlue: haelt Spieler 1 "verbunden", auch ohne Geraet.
  input->AddDriver(std::make_unique<rex::input::nop::NopInputDriver>(nullptr, 1));

  if (REXCVAR_GET(low_single_player_input)) {
    REXLOG_INFO("LoW-Eingabe: alle Geraete steuern Spieler 1");
    input->SetDeviceAssignment(std::make_unique<rex::input::SharedAssignment>());
  } else {
    input->SetDeviceAssignment(std::make_unique<rex::input::SlotAssignment>());
  }
  return input;
}

}  // namespace low
