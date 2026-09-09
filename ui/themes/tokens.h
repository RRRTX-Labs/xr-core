// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// GENERATED FILE — do not hand-edit. Regenerate with
//   python3 tools/tokens_gen.py   (xr-browser)
// and keep `tools/tokens_gen.py --check` diff-clean in CI.
//
// Token pipeline (P8-T2): one JSON source (ui/themes/tokens.json)
// -> tokens.h (C++ uint32_t 0xAARRGGBB) + tokens.css (--xr-* vars).
// Colors are uint32_t 0xAARRGGBB, NEVER SkColor and with NO
// Skia/Chromium include — consumers must not pull Skia (the
// recorded header-type decision, research-log-P8). 'critical-red'
// is reserved in data + validator.
#pragma once

#include <cstdint>

namespace xr {
namespace tokens {

// surface: #f3f4f6 (page background)
inline constexpr uint32_t kSurface = 0xFFF3F4F6;

// surface-raised: #ffffff (cards, dialogs, chips)
inline constexpr uint32_t kSurfaceRaised = 0xFFFFFFFF;

// surface-muted: #eef1f4 (secondary fills, table stripes)
inline constexpr uint32_t kSurfaceMuted = 0xFFEEF1F4;

// surface-sunken: #e6e9ed (inset wells, code blocks base)
inline constexpr uint32_t kSurfaceSunken = 0xFFE6E9ED;

// text: #1f2328 (primary text on surfaces)
inline constexpr uint32_t kText = 0xFF1F2328;

// text-dim: #57606a (secondary/disabled text)
inline constexpr uint32_t kTextDim = 0xFF57606A;

// text-inverse: #ffffff (text on accents and trust colors)
inline constexpr uint32_t kTextInverse = 0xFFFFFFFF;

// border: #d0d7de (default hairlines)
inline constexpr uint32_t kBorder = 0xFFD0D7DE;

// border-strong: #8c959f (emphasized borders)
inline constexpr uint32_t kBorderStrong = 0xFF8C959F;

// accent: #0969da (primary interactive accent)
inline constexpr uint32_t kAccent = 0xFF0969DA;

// accent-hover: #0550ae (accent hover state)
inline constexpr uint32_t kAccentHover = 0xFF0550AE;

// accent-soft: #ddf4ff (accent tint fills (hints))
inline constexpr uint32_t kAccentSoft = 0xFFDDF4FF;

// danger-caution: #8a5f00 (caution (warning) text/border — §10 danger classes)
inline constexpr uint32_t kDangerCaution = 0xFF8A5F00;

// danger-caution-bg: #fff8c5 (caution tint fill)
inline constexpr uint32_t kDangerCautionBg = 0xFFFFF8C5;

// danger-destructive: #cf222e (destructive text/border — §10 danger classes)
inline constexpr uint32_t kDangerDestructive = 0xFFCF222E;

// danger-destructive-bg: #ffebe9 (destructive tint fill)
inline constexpr uint32_t kDangerDestructiveBg = 0xFFFFEBE9;

// critical-red: #d1242f (system-critical alarms — RESERVED (see top note))
inline constexpr uint32_t kCriticalRed = 0xFFD1242F;
// RESERVED token — never themeable to a non-alarming color.

// critical-red-bg: #ffe9e9 (critical tint fill)
inline constexpr uint32_t kCriticalRedBg = 0xFFFFE9E9;

// success-ok: #1a7f37 (positive confirmation)
inline constexpr uint32_t kSuccessOk = 0xFF1A7F37;

// success-ok-bg: #dafbe1 (positive tint fill)
inline constexpr uint32_t kSuccessOkBg = 0xFFDAFBE1;

// trust-standard: #2e7d32 (identity trust tier Standard (Identity color-bar))
inline constexpr uint32_t kTrustStandard = 0xFF2E7D32;

// trust-shield: #1565c0 (identity trust tier Shield (Identity color-bar))
inline constexpr uint32_t kTrustShield = 0xFF1565C0;

// trust-fortress: #6a1b9a (identity trust tier Fortress (Identity color-bar))
inline constexpr uint32_t kTrustFortress = 0xFF6A1B9A;

// trust-standard-bg: #e6f4ea (Standard tier tint fill)
inline constexpr uint32_t kTrustStandardBg = 0xFFE6F4EA;

// trust-shield-bg: #e7f0fb (Shield tier tint fill)
inline constexpr uint32_t kTrustShieldBg = 0xFFE7F0FB;

// trust-fortress-bg: #f3e8f9 (Fortress tier tint fill)
inline constexpr uint32_t kTrustFortressBg = 0xFFF3E8F9;

// code-bg: #f6f8fa (inline code background)
inline constexpr uint32_t kCodeBg = 0xFFF6F8FA;

// focus-ring: #0969da (keyboard focus ring (a11y: visible focus))
inline constexpr uint32_t kFocusRing = 0xFF0969DA;

// overlay-scrim: #1f232866 (modal scrim (alpha))
inline constexpr uint32_t kOverlayScrim = 0x661F2328;

// space-1: 4px (4px spacing unit)
inline constexpr int kSpace1 = 4;

// space-2: 8px (8px spacing unit)
inline constexpr int kSpace2 = 8;

// space-3: 12px (12px spacing unit)
inline constexpr int kSpace3 = 12;

// space-4: 16px (16px spacing unit)
inline constexpr int kSpace4 = 16;

// radius: 6px (default corner radius (px))
inline constexpr int kRadius = 6;

// radius-lg: 12px (large corner radius (px))
inline constexpr int kRadiusLg = 12;

// font-size-sm: 12px (small text size (px))
inline constexpr int kFontSizeSm = 12;

// font-size-base: 14px (base text size (px))
inline constexpr int kFontSizeBase = 14;

// font-size-lg: 16px (large text size (px))
inline constexpr int kFontSizeLg = 16;

// font-family-ui (UI font stack (system fonts only — no bundled fonts in v0))
inline constexpr char kFontFamilyUi[] = "system-ui, -apple-system, 'Segoe UI', sans-serif";

// font-family-mono (monospace stack for code/ids (system fonts only))
inline constexpr char kFontFamilyMono[] = "ui-monospace, 'Cascadia Code', monospace";

}  // namespace tokens
}  // namespace xr
