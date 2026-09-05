#pragma once
#include "rawaccel-base.hpp"
#include <cmath>

namespace rawaccel {

/// Jump acceleration — a hard "step" in gain that can be smoothed by an
/// increasing sigmoid (reference RawAccel jump).
/// Parameters (reference):
///   cap.x  → step position (input speed where the step occurs)
///   cap.y  → step amount: gain jumps from 1.0 to cap.y
///   smooth → smoothing width in [0, 1]; 0 forces a hard step.
/// args.gain selects LEGACY (direct multiplier / sigmoid) or GAIN (output-speed
/// integral form that keeps output smooth).
struct jump {
    bool   gain_mode    = false;
    vec2d  step         = {};
    double smooth_rate  = 0;
    double antideriv_c  = 0; // GAIN mode: -smooth_antideriv(0)

    jump() = default;

    jump(const accel_args& args) : gain_mode(args.gain) {
        step = { args.cap.x, args.cap.y - 1 };
        double rate_inverse = args.smooth * step.x;
        smooth_rate = (rate_inverse < 1) ? 0.0 : (2 * M_PI) / rate_inverse;
        if (gain_mode && smooth_rate != 0)
            antideriv_c = -smooth_antideriv(0);
    }

    bool is_smooth() const { return smooth_rate != 0; }

    double decay(double x) const {
        return std::exp(smooth_rate * (step.x - x));
    }

    double smooth(double x) const {
        return step.y / (1 + decay(x));
    }

    double smooth_antideriv(double x) const {
        return step.y * (x + std::log(1 + decay(x)) / smooth_rate);
    }

    double operator()(double x, const accel_args&) const {
        if (!gain_mode) {
            // LEGACY: direct multiplier — sigmoid blend of the step.
            if (is_smooth()) return smooth(x) + 1.0;
            if (x < step.x)  return 1.0;
            return 1.0 + step.y;
        }

        // GAIN: integrate the multiplier so output speed is smooth.
        // antideriv_c = -smooth_antideriv(0) makes the curve pass through 1.0
        // at the origin.
        if (x <= 0) return 1.0;

        if (is_smooth()) return 1.0 + (smooth_antideriv(x) + antideriv_c) / x;
        if (x < step.x)  return 1.0;
        return 1.0 + step.y * (x - step.x) / x;
    }
};

} // namespace rawaccel