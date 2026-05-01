#pragma once
// motion_math.hpp — Pure-math subpixel accumulation helper.
//
// No libevdev dependency: safe to include in unit tests.
// daemon.cpp includes this header and calls apply_motion_math() from flush_motion().

#include "../include/rawaccel.hpp"
#include <cmath>
#include <climits>

namespace rawaccel {

/// Apply acceleration + subpixel accumulation to (dx, dy).
/// Updates remainder_{x,y} in place.
/// Returns integer output counts in out_x / out_y.
///
/// dpi_factor  = device_dpi / NORMALIZED_DPI
/// time_ms     = interval since previous event (milliseconds), already clamped
inline void apply_motion_math(
    modifier&              mod,
    speed_processor&       sp,
    const modifier_settings& settings,
    double                 dpi_factor,
    milliseconds           time_ms,
    double                 dx,
    double                 dy,
    double&                remainder_x,
    double&                remainder_y,
    int&                   out_x,
    int&                   out_y)
{
    vec2d motion = { dx, dy };
    mod.modify(motion, sp, settings, dpi_factor, time_ms);

    // Subpixel accumulation: carry fractional remainder between frames
    // so no micro-movements are silently discarded.
    motion.x += remainder_x;
    motion.y += remainder_y;

    // D4: truncate toward zero (same behaviour as static_cast<int>, but explicit).
    // Clamp to INT range to prevent undefined behaviour when a pathological
    // acceleration config (or NaN/Inf from a buggy algorithm) produces a value
    // outside the representable int range.
    constexpr double INT_LO = static_cast<double>(INT_MIN);
    constexpr double INT_HI = static_cast<double>(INT_MAX);
    double tx = std::trunc(motion.x);
    double ty = std::trunc(motion.y);
    if (!std::isfinite(tx)) tx = 0;
    if (!std::isfinite(ty)) ty = 0;
    out_x = static_cast<int>(std::clamp(tx, INT_LO, INT_HI));
    out_y = static_cast<int>(std::clamp(ty, INT_LO, INT_HI));

    // Preserve sign-correct fractional remainder.
    // Guard against NaN/Inf propagating into the remainder accumulator.
    remainder_x = motion.x - static_cast<double>(out_x);
    remainder_y = motion.y - static_cast<double>(out_y);
    if (!std::isfinite(remainder_x)) remainder_x = 0;
    if (!std::isfinite(remainder_y)) remainder_y = 0;
}

} // namespace rawaccel
