#pragma once
// T13 — shared parameter grid for the differential oracle.
//
// This file is intentionally dependency-free (plain values only) so that BOTH
// the vendored official RawAccel reference (tests/oracle/reference.cpp) and the
// local C++ port (tests/oracle/local.cpp) can map it onto their own accel_args
// structs. Field names were aligned 1:1 with RawAccelOfficial/rawaccel
// common/rawaccel-base.hpp (the local port only renames cap_mode → cap_mode_val).

#include <string>
#include <vector>
#include <utility>

namespace oracle {

struct Case {
    std::string name;
    std::string mode;              // classic|jump|lookup|natural|synchronous|power|noaccel
    bool        gain = true;

    double acceleration     = 0.005;
    double scale            = 1;
    double decay_rate       = 0.1;
    double gamma            = 1;
    double motivity         = 1.5;
    double exponent_classic = 2;
    double exponent_power   = 0.05;
    double limit            = 1.5;
    double sync_speed       = 5;
    double smooth           = 0.5;
    double input_offset     = 0;
    double output_offset    = 0;

    double cap_x            = 15;
    double cap_y            = 1.5;
    int    cap_mode         = 2;          // 0=io 1=in 2=out

    std::vector<float> lut;               // lookup: x,y,x,y,... pairs (as loaded)
    std::vector<double> speeds;
};

// Default speed sweep covering every behavioural region of every algorithm.
inline std::vector<double> default_speeds() {
    return { 0, 0.001, 0.005, 0.01, 0.1, 0.5, 1, 3, 5, 7.5, 10, 15, 30, 50,
             100, 250, 500, 1000, 5000, 10000, 100000 };
}

inline std::vector<Case> cases() {
    std::vector<Case> c;
    auto S = default_speeds();

    // ── Classic ────────────────────────────────────────────────────────────
    auto mkclass = [&](const std::string& n, bool gain, double accel, double exp,
                       double cx, double cy, int cm, double off = 0,
                       double spd = 5.0) {
        Case x;
        x.name = n; x.mode = "classic"; x.gain = gain;
        x.acceleration = accel; x.exponent_classic = exp;
        x.cap_x = cx; x.cap_y = cy; x.cap_mode = cm;
        x.input_offset = off; x.sync_speed = spd; x.speeds = S;
        return x;
    };
    c.push_back(mkclass("classic_gain_out_default",  true,  0.005, 2.0, 15,   1.5, 2));
    c.push_back(mkclass("classic_gain_io",           true,  0.005, 2.0, 150,  1.5, 0));
    c.push_back(mkclass("classic_gain_io_smallcapy", true,  0.005, 2.0, 150,  0.5, 0));
    c.push_back(mkclass("classic_gain_io_bigcapy",   true,  0.005, 2.0, 150,  2.0, 0));
    c.push_back(mkclass("classic_gain_in",           true,  0.005, 2.0, 150,  1.5, 1));
    c.push_back(mkclass("classic_gain_negaccel",     true, -0.01,  2.0, 0,    0,   2));
    c.push_back(mkclass("classic_gain_exp_le1",      true,  0.005, 0.5, 15,   1.5, 2));
    c.push_back(mkclass("classic_gain_offset",       true,  0.005, 2.0, 50,   1.5, 0, 10));
    c.push_back(mkclass("classic_legacy_out_default",false, 0.005, 2.0, 15,   1.5, 2));

    // ── Jump ───────────────────────────────────────────────────────────────
    auto mkjump = [&](const std::string& n, bool gain, double step_x, double step_y,
                      double smooth, double spd = 5.0) {
        Case x;
        x.name = n; x.mode = "jump"; x.gain = gain;
        x.acceleration = step_y; x.sync_speed = step_x; x.smooth = smooth;
        x.cap_x = step_x; x.cap_y = step_y;
        x.speeds = S;
        (void)spd;
        return x;
    };
    c.push_back(mkjump("jump_gain_smooth",     true,  15, 0.5, 0.5));
    c.push_back(mkjump("jump_legacy_smooth",   false, 15, 0.5, 0.5));
    c.push_back(mkjump("jump_legacy_tol1",     false, 15, 0.5, 1.0));
    c.push_back(mkjump("jump_legacy_hard",     false, 15, 1.0, 0.0));
    c.push_back(mkjump("jump_legacy_hard2",    false, 30, 1.0, 0.0));

    // ── Natural ─────────────────────────────────────────────────────────────
    auto mknat = [&](const std::string& n, bool gain, double limit, double decay,
                     double smooth) {
        Case x;
        x.name = n; x.mode = "natural"; x.gain = gain;
        x.limit = limit; x.decay_rate = decay; x.smooth = smooth;
        x.speeds = S;
        return x;
    };
    c.push_back(mknat("natural_gain_lim15", true, 1.5, 0.1, 0.5));
    c.push_back(mknat("natural_gain_lim05", true, 0.5, 0.1, 0.5));
    c.push_back(mknat("natural_legacy_lim15",false,1.5, 0.1, 0.5));
    c.push_back(mknat("natural_legacy_lim05",false,0.5, 0.1, 0.5));

    // ── Synchronous (activation_framework) ──────────────────────────────────
    auto mksync = [&](const std::string& n, bool gain, double power, double sync,
                      double smooth) {
        Case x;
        x.name = n; x.mode = "synchronous"; x.gain = gain;
        x.exponent_classic = power; x.sync_speed = sync; x.smooth = smooth;
        x.speeds = S;
        return x;
    };
    c.push_back(mksync("sync_gain_p2",    true, 2.0, 5.0, 0.5));
    c.push_back(mksync("sync_gain_p1",    true, 1.0, 5.0, 0.5));
    c.push_back(mksync("sync_gain_p07",   true, 0.7, 5.0, 0.5));
    c.push_back(mksync("sync_legacy_p2",  false,2.0, 5.0, 0.5));
    c.push_back(mksync("sync_legacy_p1",  false,1.0, 5.0, 0.5));

    // ── Lookup (velocity = gain) ────────────────────────────────────────────
    auto mklut = [&](const std::string& n, bool gain, const std::vector<float>& lut) {
        Case x;
        x.name = n; x.mode = "lookup"; x.gain = gain;
        x.lut = lut; x.speeds = S;
        return x;
    };
    const std::vector<float> lut7 = {
        0.f, 0.f, 1.f, 1.f, 2.f, 2.5f, 3.f, 4.f, 5.f, 10.f, 10.f, 30.f, 20.f, 100.f
    };
    c.push_back(mklut("lookup_velocity", true,  lut7));
    c.push_back(mklut("lookup_displacement", false, lut7));
    c.push_back(mklut("lookup_velocity_p1", true,  { 0.f, 1.f, 1.f, 1.f, 2.f, 1.f }));

    // ── Power ───────────────────────────────────────────────────────────────
    auto mkpow = [&](const std::string& n, bool gain, double scale, double exp) {
        Case x;
        x.name = n; x.mode = "power"; x.gain = gain;
        x.scale = scale; x.exponent_power = exp;
        x.speeds = S;
        return x;
    };
    c.push_back(mkpow("power_gain_p1",  true,  1.0, 1.0));
    c.push_back(mkpow("power_legacy_p1",false, 1.0, 1.0));

    return c;
}

} // namespace oracle