#pragma once

#include <string>

#include <borealis.hpp>

// UI_PLAN O1 — design tokens. Single source of truth for colors, corner
// radii, the font scale and the spacing step.
//
// Colors are registered into both Borealis theme variants under "goonies/*"
// and resolved through brls::Application::getTheme(), so every view follows
// the console light/dark theme automatically.
//
// Rule (see CONTRIBUTING / UI_PLAN S13): no nvgRGB/nvgRGBA literals anywhere
// in src/ui outside this file.

namespace goonies::ui::theme {

// --- Corner radii ------------------------------------------------------

inline constexpr float kRadiusSmall  = 6.0f;
inline constexpr float kRadiusMedium = 8.0f;
inline constexpr float kRadiusLarge  = 12.0f;

// --- Font scale ---------------------------------------------------------

inline constexpr float kFontTitle   = 32.0f;
inline constexpr float kFontHeading = 25.0f;
inline constexpr float kFontBody    = 21.0f;
inline constexpr float kFontSmall   = 17.0f;
inline constexpr float kFontCaption = 15.0f;

// --- Spacing ------------------------------------------------------------
// All paddings/margins should be multiples of this step.

inline constexpr float kSpacingUnit = 8.0f;

// --- Colors -------------------------------------------------------------
// Registers the goonies/* tokens into both Borealis themes. Call once after
// brls::Application::init() and before any view is constructed.

inline void registerColors() {
    struct Token {
        const char* name;
        NVGcolor light;
        NVGcolor dark;
    };
    static const Token kTokens[] = {
        // Brand / status
        {"goonies/accent", nvgRGB(0, 210, 247), nvgRGB(0, 210, 247)},   // Joy-Con Neon Cyan #00D2F7
        {"goonies/gold", nvgRGB(255, 204, 51), nvgRGB(255, 204, 51)},    // Goonies Pirate Gold #FFCC33
        {"goonies/error", nvgRGB(255, 69, 84), nvgRGB(255, 69, 84)},     // Neon Red #FF4554
        {"goonies/success", nvgRGB(40, 170, 90), nvgRGB(96, 220, 130)},  // #60DC82 (darkened on light)
        {"goonies/warning", nvgRGB(196, 110, 22), nvgRGB(230, 150, 80)},

        // Modern Accent Palette
        {"goonies/neon_green", nvgRGB(46, 204, 113), nvgRGB(46, 204, 113)},
        {"goonies/neon_purple", nvgRGB(165, 94, 234), nvgRGB(165, 94, 234)},
        {"goonies/neon_orange", nvgRGB(250, 130, 49), nvgRGB(250, 130, 49)},
        {"goonies/neon_pink", nvgRGB(252, 92, 101), nvgRGB(252, 92, 101)},

        // Text levels
        {"goonies/text_primary", nvgRGB(45, 45, 45), nvgRGB(245, 245, 250)},
        {"goonies/text_secondary", nvgRGB(90, 90, 95), nvgRGB(175, 180, 195)},
        {"goonies/text_tertiary", nvgRGB(125, 125, 130), nvgRGB(140, 145, 155)},
        {"goonies/text_disabled", nvgRGB(175, 175, 180), nvgRGB(105, 110, 120)},

        // Surfaces
        {"goonies/surface", nvgRGB(225, 225, 230), nvgRGB(32, 34, 44)},
        {"goonies/overlay", nvgRGBA(240, 240, 244, 235), nvgRGBA(22, 24, 30, 240)},
        {"goonies/panel", nvgRGBA(228, 228, 234, 180), nvgRGBA(36, 40, 52, 220)},
        {"goonies/card_bg", nvgRGBA(235, 238, 245, 240), nvgRGBA(25, 27, 36, 230)},
        {"goonies/card_bg_focus", nvgRGBA(245, 248, 255, 255), nvgRGBA(38, 42, 56, 245)},
        {"goonies/track", nvgRGBA(128, 128, 128, 70), nvgRGBA(128, 128, 128, 70)},
        {"goonies/graph_bg", nvgRGBA(208, 210, 216, 120), nvgRGBA(24, 26, 34, 120)},
        {"goonies/graph_grid", nvgRGBA(60, 60, 70, 35), nvgRGBA(180, 180, 190, 35)},
    };
    for (const auto& t : kTokens) {
        brls::Theme::getLightTheme().addColor(t.name, t.light);
        brls::Theme::getDarkTheme().addColor(t.name, t.dark);
    }
}

inline NVGcolor color(const std::string& name) {
    return brls::Application::getTheme().getColor(name);
}

inline NVGcolor accent() { return color("goonies/accent"); }
inline NVGcolor neonCyan() { return color("goonies/accent"); }
inline NVGcolor gold() { return color("goonies/gold"); }
inline NVGcolor neonGreen() { return color("goonies/neon_green"); }
inline NVGcolor neonPurple() { return color("goonies/neon_purple"); }
inline NVGcolor neonOrange() { return color("goonies/neon_orange"); }
inline NVGcolor neonPink() { return color("goonies/neon_pink"); }
inline NVGcolor error() { return color("goonies/error"); }
inline NVGcolor success() { return color("goonies/success"); }
inline NVGcolor warning() { return color("goonies/warning"); }
inline NVGcolor textPrimary() { return color("goonies/text_primary"); }
inline NVGcolor textSecondary() { return color("goonies/text_secondary"); }
inline NVGcolor textTertiary() { return color("goonies/text_tertiary"); }
inline NVGcolor textDisabled() { return color("goonies/text_disabled"); }
inline NVGcolor surface() { return color("goonies/surface"); }
inline NVGcolor overlay() { return color("goonies/overlay"); }
inline NVGcolor panel() { return color("goonies/panel"); }
inline NVGcolor cardBg() { return color("goonies/card_bg"); }
inline NVGcolor cardBgFocus() { return color("goonies/card_bg_focus"); }
inline NVGcolor track() { return color("goonies/track"); }
inline NVGcolor graphBg() { return color("goonies/graph_bg"); }
inline NVGcolor graphGrid() { return color("goonies/graph_grid"); }

} // namespace goonies::ui::theme
