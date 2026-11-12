// LoWRecomp - Eingabe
//
// Eigener Tastatur/Maus-Treiber fuer WWE Legends of WrestleMania, angelehnt an
// die PC-Steuerung der aktuellen WWE-2K-Spiele. Ersetzt ReXGlues generischen
// mnk-Treiber, weil der Modifikatortasten (Shift/Strg/Alt) exakt abgleicht:
// "Shift halten = Rennen" plus WASD waere damit nicht moeglich.
//
// Controller (Xbox 360, Xbox One/Series, DualShock 4, DualSense ...) laufen
// unveraendert ueber ReXGlues SDL3-Treiber.
//
// Alle Belegungen sind CVars (Abschnitt [low] in lowrecomp.toml):
//   bind_<aktion> = "Taste1,Taste2,Modifikator+Taste"
// Eine Alternative mit "+" ist ein Akkord: alle Tasten muessen gedrueckt sein.
// Solange ein Akkord aktiv ist, loesen seine Tasten keine Einzelbelegungen aus
// (Leertaste+K = Finisher, nicht zusaetzlich Leertaste = LT und K = Strike).

#pragma once

#include <memory>

namespace rex::system {
class IInputSystem;
}

namespace low {

/// Fabrik fuer RuntimeConfig::input_factory: SDL-Controller + LoW-Tastatur/Maus
/// + NOP-Fallback, Spielerzuweisung nach CVar low_single_player_input.
std::unique_ptr<rex::system::IInputSystem> CreateInputSystem(bool tool_mode);

/// Welcher Controller ist angeschlossen? (fuer "Tastensymbole: automatisch")
/// kPlayStation, sobald ein Sony-Controller (DualShock/DualSense) dabei ist.
/// Wartet hoechstens wait_ms, falls SDL beim Aufruf noch keine Controller
/// gemeldet hat.
enum class PadKind { kNone, kXbox, kPlayStation };
PadKind ConnectedPadKind(int wait_ms = 0);

}  // namespace low
