#include "../include/config.hpp"
#include "../include/rawaccel.hpp"
#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <iomanip>
#include <cmath>
#include <csignal>
#include <unistd.h>
#include <sys/stat.h>

// Version number comes from RAWACCEL_VERSION in rawaccel-base.hpp (single source of truth).
static constexpr const char* VERSION = rawaccel::RAWACCEL_VERSION;

using namespace rawaccel;

// ── Daemon communication ──────────────────────────────────────────────────────

/// Check whether a PID is alive by probing /proc/<pid>.
/// Unlike kill(pid, 0), this works even when the daemon runs as root
/// and the caller is an unprivileged user (kill -0 returns EPERM in that case).
static bool pid_alive(pid_t pid) {
    if (pid <= 0) return false;
    // /proc/<pid> exists as long as the process is alive — readable by any user.
    std::string proc_path = "/proc/" + std::to_string(pid);
    struct stat st{};
    return stat(proc_path.c_str(), &st) == 0;
}

static bool send_signal_to_daemon(int sig) {
    // N6: daemon prefers $XDG_RUNTIME_DIR/rawaccel.pid — check it first.
    std::vector<std::string> paths;
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg && xdg[0] != '\0') paths.push_back(std::string(xdg) + "/rawaccel.pid");
    paths.push_back("/run/rawaccel.pid");
    paths.push_back("/tmp/rawaccel.pid");

    for (auto& path : paths) {
        std::ifstream f(path);
        if (!f.is_open()) continue;
        pid_t pid = 0;
        f >> pid;
        if (!pid_alive(pid)) continue;
        return kill(pid, sig) == 0;
    }
    return false;
}

// ── Profile display ────────────────────────────────────────────────────────────

static void print_accel_args(const accel_args& a, const std::string& prefix = "  ") {
    auto mode_str = [](accel_mode m) -> std::string {
        switch (m) {
        case accel_mode::classic:     return "classic";
        case accel_mode::power:       return "power";
        case accel_mode::natural:     return "natural";
        case accel_mode::jump:        return "jump";
        case accel_mode::synchronous: return "synchronous";
        case accel_mode::lookup:      return "lookup";
        default:                      return "noaccel";
        }
    };

    std::cout << prefix << "mode:             " << mode_str(a.mode) << "\n";
    std::cout << std::fixed << std::setprecision(4);
    std::cout << prefix << "gain:             " << (a.gain ? "true" : "false") << "\n";
    std::cout << prefix << "acceleration:     " << a.acceleration     << "\n";
    std::cout << prefix << "input_offset:     " << a.input_offset     << "\n";
    std::cout << prefix << "output_offset:    " << a.output_offset    << "\n";
    std::cout << prefix << "scale:            " << a.scale            << "\n";
    std::cout << prefix << "exponent_classic: " << a.exponent_classic << "\n";
    std::cout << prefix << "exponent_power:   " << a.exponent_power   << "\n";
    std::cout << prefix << "limit:            " << a.limit            << "\n";
    std::cout << prefix << "motivity:         " << a.motivity         << "\n";
    std::cout << prefix << "gamma:            " << a.gamma            << "\n";
    std::cout << prefix << "decay_rate:       " << a.decay_rate       << "\n";
    std::cout << prefix << "sync_speed:       " << a.sync_speed       << "\n";
    std::cout << prefix << "smooth:           " << a.smooth           << "\n";
    std::cout << prefix << "cap:              [" << a.cap.x << ", " << a.cap.y << "]\n";
    auto cap_mode_str = [](cap_mode c) -> std::string {
        switch (c) {
        case cap_mode::in:  return "in";
        case cap_mode::io:  return "io";
        default:            return "out";
        }
    };
    std::cout << prefix << "cap_mode:         " << cap_mode_str(a.cap_mode_val) << "\n";
}

static void print_profile(const device_profile& dp) {
    auto& p = dp.prof;
    std::cout << "Profile: " << dp.name << "\n";
    std::cout << "  device_id:    " << (dp.device_id.empty() ? "(all)" : dp.device_id) << "\n";
    if (p.raw_passthrough) {
        std::cout << "  raw:          true  (all processing bypassed)\n";
        std::cout << "  dpi:          " << dp.dev_cfg.dpi         << "\n";
        std::cout << "  polling_rate: " << dp.dev_cfg.polling_rate << "\n";
        return;
    }
    std::cout << "  dpi:          " << dp.dev_cfg.dpi         << "\n";
    std::cout << "  polling_rate: " << dp.dev_cfg.polling_rate << "\n";
    std::cout << "  rotation:     " << p.degrees_rotation      << "°\n";
    std::cout << "  snap:         " << p.degrees_snap          << "°\n";
    std::cout << "  speed_min:    " << p.speed_min << (p.speed_min == 0 ? "  (disabled)" : "") << "\n";
    std::cout << "  speed_max:    " << p.speed_max << (p.speed_max == 0 ? "  (disabled)" : "") << "\n";
    std::cout << "  output_dpi:   " << p.output_dpi << (std::fabs(p.output_dpi - NORMALIZED_DPI) < 1e-9 ? "  (default 1000)" : "") << "\n";
    std::cout << "  lr_ratio:     " << p.lr_output_dpi_ratio << (std::fabs(p.lr_output_dpi_ratio - 1.0) < 1e-9 ? "  (off)" : "") << "\n";
    std::cout << "  ud_ratio:     " << p.ud_output_dpi_ratio << (std::fabs(p.ud_output_dpi_ratio - 1.0) < 1e-9 ? "  (off)" : "") << "\n";
    {
        auto& sp = p.speed_processor_args;
        std::string dist = sp.whole ? (sp.lp_norm >= 16 || sp.lp_norm <= 0 ? "max" :
                           (std::fabs(sp.lp_norm - 2.0) > 1e-9 ? "lp" : "euclidean")) : "separate";
        std::cout << "  distance_mode:  " << dist << "\n";
        if (dist == "lp")
            std::cout << "  lp_norm:        " << sp.lp_norm << "\n";
        if (sp.input_speed_smooth_halflife > 0)
            std::cout << "  input_smooth_halflife:  " << sp.input_speed_smooth_halflife << "\n";
        if (sp.scale_smooth_halflife > 0)
            std::cout << "  scale_smooth_halflife:  " << sp.scale_smooth_halflife << "\n";
        if (sp.output_speed_smooth_halflife > 0)
            std::cout << "  output_smooth_halflife: " << sp.output_speed_smooth_halflife << "\n";
    }
    std::cout << "  Acceleration (X axis):\n";
    print_accel_args(p.accel_x, "    ");
    if (p.accel_x != p.accel_y) {
        std::cout << "  Acceleration (Y axis):\n";
        print_accel_args(p.accel_y, "    ");
    } else {
        std::cout << "  Acceleration (Y axis): same as X\n";
    }
}

// ── Commands ──────────────────────────────────────────────────────────────────

static int cmd_list(const app_config& cfg) {
    std::cout << "Active profile: " << cfg.active_profile << "\n\n";
    for (auto& dp : cfg.profiles) {
        print_profile(dp);
        std::cout << "\n";
    }
    return 0;
}

static int cmd_show(const app_config& cfg, const std::string& name) {
    for (auto& dp : cfg.profiles) {
        if (dp.name == name) {
            print_profile(dp);
            return 0;
        }
    }
    std::cerr << "Profile not found: " << name << "\n";
    return 1;
}

static int cmd_set(app_config& cfg, const std::string& config_path, const std::string& name) {
    for (auto& dp : cfg.profiles) {
        if (dp.name == name) {
            cfg.active_profile = name;
            save_config(cfg, config_path);
            std::cout << "Active profile set to: " << name << "\n";
            // Signal daemon to reload
            if (send_signal_to_daemon(SIGHUP)) {
                std::cout << "Daemon reloaded.\n";
            } else {
                std::cout << "Note: daemon not running or not signaled.\n";
            }
            return 0;
        }
    }
    std::cerr << "Profile not found: " << name << "\n";
    return 1;
}

static int cmd_create(app_config& cfg, const std::string& config_path, const std::string& name) {
    // Check duplicate
    for (auto& dp : cfg.profiles) {
        if (dp.name == name) {
            std::cerr << "Profile already exists: " << name << "\n";
            return 1;
        }
    }
    device_profile dp;
    dp.name = name;
    dp.dev_cfg.dpi = 800;
    dp.dev_cfg.polling_rate = 1000;
    dp.prof.accel_x.mode = accel_mode::noaccel;
    dp.prof.accel_y.mode = accel_mode::noaccel;
    cfg.profiles.push_back(dp);
    save_config(cfg, config_path);
    std::cout << "Created profile: " << name << "\n";
    if (send_signal_to_daemon(SIGHUP))
        std::cout << "Daemon reloaded.\n";
    return 0;
}

static int cmd_delete(app_config& cfg, const std::string& config_path, const std::string& name) {
    auto it = std::remove_if(cfg.profiles.begin(), cfg.profiles.end(),
                             [&](const device_profile& dp) { return dp.name == name; });
    if (it == cfg.profiles.end()) {
        std::cerr << "Profile not found: " << name << "\n";
        return 1;
    }
    cfg.profiles.erase(it, cfg.profiles.end());
    // If we just deleted the active profile, fall back to the first remaining one.
    if (cfg.active_profile == name && !cfg.profiles.empty())
        cfg.active_profile = cfg.profiles[0].name;
    save_config(cfg, config_path);
    std::cout << "Deleted profile: " << name << "\n";
    if (cfg.active_profile != name)
        std::cout << "Active profile is now: " << cfg.active_profile << "\n";
    if (send_signal_to_daemon(SIGHUP))
        std::cout << "Daemon reloaded.\n";
    return 0;
}

/// Quick parameter setter: rawaccel set-param <profile> <key> <value>
static int cmd_set_param(app_config& cfg, const std::string& config_path,
                         const std::string& profile_name,
                         const std::string& key, const std::string& val)
{
    device_profile* dp = nullptr;
    for (auto& p : cfg.profiles) {
        if (p.name == profile_name) { dp = &p; break; }
    }
    if (!dp) { std::cerr << "Profile not found: " << profile_name << "\n"; return 1; }

    auto& a = dp->prof.accel_x;
    auto& ay = dp->prof.accel_y;
    auto set_mode = [](accel_args& a, const std::string& m) {
        if (m == "classic")     a.mode = accel_mode::classic;
        else if (m == "power")  a.mode = accel_mode::power;
        else if (m == "natural") a.mode = accel_mode::natural;
        else if (m == "jump")   a.mode = accel_mode::jump;
        else if (m == "synchronous") a.mode = accel_mode::synchronous;
        else if (m == "lookup") a.mode = accel_mode::lookup;
        else                    a.mode = accel_mode::noaccel;
    };

    // Parse numeric value only for numeric params (not for string/bool keys)
    static const std::vector<std::string> non_numeric_keys = {
        "mode", "gain", "cap_mode", "distance_mode", "raw"
    };
    double v = 0;
    bool need_numeric = true;
    for (auto& nk : non_numeric_keys) if (nk == key) { need_numeric = false; break; }
    if (need_numeric) {
        try { v = std::stod(val); }
        catch (...) { std::cerr << "Invalid numeric value: " << val << "\n"; return 1; }
    }

    // For boolean keys accept "1"/"true"/"yes" as true, anything else as false
    auto parse_bool = [](const std::string& s) -> bool {
        return s == "1" || s == "true" || s == "yes";
    };

    bool ok = true;
    if      (key == "mode")             { set_mode(a, val); set_mode(ay, val); }
    else if (key == "gain")             { a.gain = ay.gain = parse_bool(val); }
    else if (key == "acceleration")     { a.acceleration = ay.acceleration = v; }
    else if (key == "exponent_classic") { a.exponent_classic = ay.exponent_classic = v; }
    else if (key == "exponent_power")   { a.exponent_power = ay.exponent_power = v; }
    else if (key == "limit")            { a.limit = ay.limit = v; }
    else if (key == "decay_rate")       { a.decay_rate = ay.decay_rate = v; }
    else if (key == "input_offset")     { a.input_offset = ay.input_offset = v; }
    else if (key == "output_offset")    { a.output_offset = ay.output_offset = v; }
    else if (key == "scale")            { a.scale = ay.scale = v; }
    else if (key == "sync_speed")       { a.sync_speed = ay.sync_speed = v; }
    else if (key == "smooth")           { a.smooth = ay.smooth = v; }
    else if (key == "motivity")         { a.motivity = ay.motivity = v; }
    else if (key == "gamma")            { a.gamma = ay.gamma = v; }
    else if (key == "cap_y")            { a.cap.y = ay.cap.y = v; }
    else if (key == "cap_x")            { a.cap.x = ay.cap.x = v; }
    else if (key == "cap_mode")         { a.cap_mode_val = ay.cap_mode_val =
                                            (val == "io" ? cap_mode::io :
                                             val == "in" ? cap_mode::in : cap_mode::out); }
    else if (key == "raw")              { dp->prof.raw_passthrough = parse_bool(val); }
    else if (key == "rotation")         { dp->prof.degrees_rotation = v; }
    else if (key == "snap")             { dp->prof.degrees_snap = v; }
    else if (key == "dpi")              { dp->dev_cfg.dpi = (int)v; }
    else if (key == "polling_rate")     { dp->dev_cfg.polling_rate = (int)v; }
    else if (key == "speed_min")        { dp->prof.speed_min = v; }
    else if (key == "speed_max")        { dp->prof.speed_max = v; }
    else if (key == "output_dpi")        { dp->prof.output_dpi = v; }
    else if (key == "lr_ratio")         { dp->prof.lr_output_dpi_ratio = v; }
    else if (key == "ud_ratio")         { dp->prof.ud_output_dpi_ratio = v; }
    else if (key == "distance_mode")    {
        if      (val == "separate") { dp->prof.speed_processor_args.whole = false; }
        else if (val == "max")      { dp->prof.speed_processor_args.whole = true;  dp->prof.speed_processor_args.lp_norm = 9999; }
        else if (val == "lp")       { dp->prof.speed_processor_args.whole = true; /* lp_norm set separately */ }
        else                        { dp->prof.speed_processor_args.whole = true;  dp->prof.speed_processor_args.lp_norm = 2; }
    }
    else if (key == "lp_norm")          { dp->prof.speed_processor_args.lp_norm = v; }
    else if (key == "input_smooth_halflife")  { dp->prof.speed_processor_args.input_speed_smooth_halflife = v; }
    else if (key == "scale_smooth_halflife")  { dp->prof.speed_processor_args.scale_smooth_halflife = v; }
    else if (key == "output_smooth_halflife") { dp->prof.speed_processor_args.output_speed_smooth_halflife = v; }
    else                                { ok = false; std::cerr << "Unknown key: " << key << "\n"; return 1; }

    if (ok) {
        // Sanitize after setting — clamps DPI, polling rate, rotation, etc. to safe ranges
        sanitize_device_profile(*dp);
        save_config(cfg, config_path);
        std::cout << "Set " << key << " = " << val << " in profile '" << profile_name << "'\n";
        if (send_signal_to_daemon(SIGHUP))
            std::cout << "Daemon reloaded.\n";
    }
    return ok ? 0 : 1;
}

static int cmd_export(const app_config& cfg, const std::string& name) {
    for (auto& dp : cfg.profiles) {
        if (dp.name == name || name.empty()) {
            std::cout << profile_to_json(dp) << "\n";
            if (!name.empty()) return 0;
        }
    }
    return 0;
}

static int cmd_import(app_config& cfg, const std::string& config_path, const std::string& json_file) {
    std::ifstream f(json_file);
    if (!f.is_open()) { std::cerr << "Cannot open: " << json_file << "\n"; return 1; }
    std::string content((std::istreambuf_iterator<char>(f)), {});
    device_profile dp;
    try {
        dp = profile_from_json(content);
    } catch (std::exception& e) {
        std::cerr << "Invalid profile JSON: " << e.what() << "\n";
        return 1;
    }

    // Warn if either LUT axis was clamped during JSON parse (over LUT_POINTS_CAPACITY).
    auto check_lut = [](const accel_args& a, const char* axis) {
        if (a.mode == accel_mode::lookup &&
            a.length / 2 > (int)LUT_POINTS_CAPACITY) {
            std::cerr << "Warning: LUT (" << axis << " axis) in imported profile exceeds "
                      << LUT_POINTS_CAPACITY << " points and was truncated.\n";
        }
    };
    check_lut(dp.prof.accel_x, "X");
    check_lut(dp.prof.accel_y, "Y");

    cfg.profiles.push_back(dp);
    try {
        save_config(cfg, config_path);
    } catch (std::exception& e) {
        std::cerr << "Failed to save config: " << e.what() << "\n";
        return 1;
    }
    std::cout << "Imported profile: " << dp.name << "\n";
    if (send_signal_to_daemon(SIGHUP))
        std::cout << "Daemon reloaded.\n";
    return 0;
}

static int cmd_reload() {
    if (send_signal_to_daemon(SIGHUP)) {
        std::cout << "Daemon reloaded.\n";
        return 0;
    }
    std::cerr << "Could not signal daemon (is it running?)\n";
    return 1;
}

static int cmd_stop() {
    if (send_signal_to_daemon(SIGTERM)) {
        std::cout << "Daemon stopped.\n";
        return 0;
    }
    std::cerr << "Could not signal daemon (is it running?)\n";
    return 1;
}

static bool daemon_running() {
    std::vector<std::string> paths;
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg && xdg[0] != '\0') paths.push_back(std::string(xdg) + "/rawaccel.pid");
    paths.push_back("/run/rawaccel.pid");
    paths.push_back("/tmp/rawaccel.pid");
    for (auto& path : paths) {
        std::ifstream f(path);
        if (!f.is_open()) continue;
        pid_t pid = 0;
        f >> pid;
        if (pid_alive(pid)) return true;
    }
    return false;
}

static int cmd_status(const std::string& config_path) {
    bool running = daemon_running();
    std::cout << "Daemon:  " << (running ? "running" : "stopped") << "\n";
    try {
        auto cfg = load_config(config_path);
        std::cout << "Config:  " << config_path << "\n";
        std::cout << "Active:  " << cfg.active_profile << "\n";
        std::cout << "Profiles (" << cfg.profiles.size() << "):\n";
        for (auto& p : cfg.profiles) {
            bool is_active = (p.name == cfg.active_profile);
            std::cout << "  " << (is_active ? "* " : "  ") << p.name;
            // Show device assignment if set
            if (!p.device_id.empty())
                std::cout << "  [device: " << p.device_id << "]";
            // Show mode summary
            const char* mode_s = "noaccel";
            if (p.prof.raw_passthrough) {
                mode_s = "raw";
            } else {
                switch (p.prof.accel_x.mode) {
                case accel_mode::classic:     mode_s = "classic";     break;
                case accel_mode::power:       mode_s = "power";       break;
                case accel_mode::natural:     mode_s = "natural";     break;
                case accel_mode::jump:        mode_s = "jump";        break;
                case accel_mode::synchronous: mode_s = "synchronous"; break;
                case accel_mode::lookup:      mode_s = "lookup";      break;
                default: break;
                }
            }
            std::cout << "  (mode: " << mode_s
                      << ", DPI: " << p.dev_cfg.dpi
                      << ", poll: " << p.dev_cfg.polling_rate << "Hz)\n";
        }
        // Latency hint: tell user how to get stats
        if (running)
            std::cout << "Tip: kill -USR1 $(cat /run/rawaccel.pid)  → dump latency stats\n";
    } catch (...) {
        std::cout << "Config:  " << config_path << " (unreadable or missing)\n";
    }
    return running ? 0 : 1;
}

// ── Help ──────────────────────────────────────────────────────────────────────

static void print_help() {
    std::cout <<
R"(rawaccel-cli v)" << VERSION << R"( — Raw Accel Linux

Usage: rawaccel-cli [OPTIONS] <COMMAND> [ARGS...]

Commands:
  list                          List all profiles
  show <profile>                Show profile details
  set <profile>                 Set active profile
  create <profile>              Create new profile with defaults
  delete <profile>              Delete a profile
  set-param <profile> <key> <value>
                                Set a parameter in a profile
  export [profile]              Export profile as JSON to stdout
  import <file.json>            Import profile from JSON file
  status                        Show daemon status, profiles, and device assignments
  reload                        Reload daemon config (SIGHUP)
  stop                          Stop daemon (SIGTERM)
  latency                       Dump per-device processing latency stats (SIGUSR1)

Options:
  -c, --config PATH             Config file path
  -h, --help                    Show this help
  -V, --version                 Show version

Parameters (for set-param):
  raw               true|false|1|0  (raw passthrough — bypass all processing)
  mode              classic|power|natural|jump|synchronous|lookup|noaccel
  gain              true|false|1|0  (gain mode on/off)
  acceleration      Acceleration multiplier (e.g. 0.005)
  exponent_classic  Classic exponent (e.g. 2.0)
  exponent_power    Power/synchronous exponent (e.g. 0.05)
  limit             Upper multiplier asymptote, jump/natural (e.g. 1.5)
  decay_rate        Natural decay rate (e.g. 0.1)
  motivity          Natural motivity (e.g. 1.5)
  gamma             Classic gamma (e.g. 1.0)
  input_offset      Speed offset before acceleration starts
  output_offset     Output offset (power mode)
  scale             Scale factor (power mode)
  sync_speed        Synchronous sync speed (e.g. 5.0)
  smooth            Jump smoothness (e.g. 0.5)
  cap_x             Input speed cap
  cap_y             Output gain cap
  cap_mode          out|in|io  (cap mode)
  rotation          Rotation in degrees
  snap              Snap angle in degrees
  dpi               Mouse DPI
  polling_rate      Mouse polling rate (Hz)
  speed_min         Minimum speed clamp (ips)
  speed_max         Maximum speed clamp (ips)
  output_dpi        Output DPI normalization value (default: 1000)
  lr_ratio          Left/right output DPI ratio
  ud_ratio          Up/down output DPI ratio
  distance_mode     euclidean|max|lp|separate  (speed calculation method)
  lp_norm           Lp-norm value (when distance_mode=lp, e.g. 3.0)
  input_smooth_halflife   Input speed EMA halflife (ms, 0=off)
  scale_smooth_halflife   Scale EMA halflife (ms, 0=off)
  output_smooth_halflife  Output speed EMA halflife (ms, 0=off)

Examples:
  rawaccel-cli list
  rawaccel-cli create gaming
  rawaccel-cli set-param gaming mode classic
  rawaccel-cli set-param gaming acceleration 0.005
  rawaccel-cli set-param gaming exponent_classic 2
  rawaccel-cli set-param gaming limit 1.8
  rawaccel-cli set gaming
  rawaccel-cli export gaming > backup.json
)";
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::string config_path;
    std::vector<std::string> args;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            std::cout << "rawaccel-cli " << VERSION << "\n";
            return 0;
        } else {
            args.push_back(argv[i]);
        }
    }

    if (args.empty()) { print_help(); return 0; }

    if (config_path.empty()) config_path = find_config_path();

    // Commands that don't need config loaded
    if (args[0] == "reload") return cmd_reload();
    if (args[0] == "stop")   return cmd_stop();
    if (args[0] == "status") return cmd_status(config_path);
    if (args[0] == "latency") {
        // Send SIGUSR1 to daemon to dump latency stats to its stdout (journald)
        if (!send_signal_to_daemon(SIGUSR1)) {
            std::cerr << "Daemon not running or no PID file found.\n";
            return 1;
        }
        std::cout << "SIGUSR1 sent. View stats with: journalctl -u rawaccel -n 30\n";
        return 0;
    }

    // Load config (create default if missing)
    app_config cfg;
    try {
        cfg = load_config(config_path);
    } catch (...) {
        // Create minimal default
        device_profile dp;
        dp.name = "default";
        dp.dev_cfg.dpi = 800;
        dp.dev_cfg.polling_rate = 1000;
        cfg.profiles.push_back(dp);
        try { save_config(cfg, config_path); } catch (...) {}
    }

    const std::string& cmd = args[0];

    if (cmd == "list")   return cmd_list(cfg);
    if (cmd == "show"  && args.size() >= 2) return cmd_show(cfg, args[1]);
    if (cmd == "set"   && args.size() >= 2) return cmd_set(cfg, config_path, args[1]);
    if (cmd == "create" && args.size() >= 2) return cmd_create(cfg, config_path, args[1]);
    if (cmd == "delete" && args.size() >= 2) return cmd_delete(cfg, config_path, args[1]);
    if (cmd == "set-param" && args.size() >= 4) return cmd_set_param(cfg, config_path, args[1], args[2], args[3]);
    if (cmd == "export") return cmd_export(cfg, args.size() >= 2 ? args[1] : "");
    if (cmd == "import" && args.size() >= 2) return cmd_import(cfg, config_path, args[1]);

    std::cerr << "Unknown command: " << cmd << "\n";
    print_help();
    return 1;
}
