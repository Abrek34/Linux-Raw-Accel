# Changelog

All notable changes to **rawaccel-linux** are documented here.

The canonical version string lives in `include/rawaccel-base.hpp`
(`RAWACCEL_VERSION`) and must stay in sync with `CMakeLists.txt` and
`packaging/PKGBUILD` — bump all three together.

## [0.5.0] — 2026-09-05

### Player round (oyuncu turu, R29–R37)
- **`rawaccel-cli create-preset`**: research-based game presets `cs2`, `valorant`,
  `apex`, `fps` (plus existing `gaming`/`office`/`precision`/`disable`) map onto
  the acceleration engine with per-game parameter choices; a `.tar`/file import
  arity fix (`import` with a single file) closes a CRIT round-trip gap (P65).
- **Default profile is 1:1 raw passthrough** (no acceleration by default — user
  decision, R34); acceleration is opt-in per profile.
- **Apex preset tuning** (`output_offset 0.2 → 0.9`): fixes the sub-1:1 "muddy"
  feel on the power curve while keeping the fast 180° flick ramp (R37, user
  approved).
- **Player guide** in README: game-specific preset tutorial / tuning workflow
  for CS2, Valorant, Apex, generic FPS.
- **Oracle esport grid** (`tests/oracle/`): the speed sweep now covers the
  tournament band `2000 / 3000 / 4000` ips between the previously sampled
  `1000/5000`; the four game presets are mirrored as dedicated cases. The
  differential oracle now compares **768 gain rows** (31 documented intentional
  deviations) vs the official reference.
- **Virtmouse live harness** (`scripts/virtmouse-game.c`, R35/P64): injects
  synthetic game-speed mouse motion (uinput virtual mouse) for live end-to-end
  latency / hot-path verification.

### Live telemetry (IPC `status`)
- Per-device motion telemetry published in the `status` JSON from the daemon:
  `telem_in_ips`, `telem_out_ips`, `telem_gain`, `telem_dx`, `telem_dy`.
- Lock-free lane: `flush_motion()` (loop thread) does relaxed stores + a
  release-bump of the seqlock counter; `status_json()` (IPC thread) does the
  double-load verify with bounded retry. No measurable hot-path cost
  (independent latency gap +1.1% / −1.6% = noise, see P32/P38).
- Raw-passthrough still fills the counter and deltas; gain is 0 when
  `in == 0`. `rawaccel-cli status` / GUI connection state surface it as-is.

### Stability / correctness fixes (Bug-Hunt package)
- **CLI (`cli/main.cpp`)**
  - All 8 config-mutating commands route through `safe_save()`: a save or
    I/O failure now reports a clean error and exits 1 instead of
    `std::terminate` → SIGABRT (exit 134).
  - Missing command arguments produce a targeted
    `Command 'X' is missing N argument(s)` message instead of a misleading
    "Unknown command" plus full help.
  - A trailing bare `-c` reports `Option '-c' requires a path argument`.
  - An existing-but-corrupt config is never overwritten: load refuses with
    `Refusing to overwrite — run validate`; a default is created only when the
    file genuinely does not exist.
- **Config (`src/config.cpp`)**
  - (P43-BF1, critical) The schema `version` is now read back from the JSON
    instead of defaulting empty on every load. Previously `migrate_config()`
    re-ran on each load, so a stored `lookup` + `gain` config grew on every
    save (200 → 20000 → 2000000 in a round-trip probe). Migration now runs
    exactly once and is a no-op for current files.
  - LUT `length` is clamped to `LUT_RAW_DATA_CAPACITY` (no out-of-bounds
    access from a programmatic by-pass).
  - `save_config()` temp files use a PID-suffixed name opened with
    `O_NOFOLLOW | O_EXCL` — no symlink clobber, no two-writer race.
  - Scalar/string fields (`mode`, `gain`, `cap_mode`, `active_profile`,
    `use_raw_input`, `device_id`, `name`) are type-guarded; `device_id` and
    `name` are capped at 256 characters; malformed types degrade to defaults.
  - `lut_data` min computation is `size_t`-safe (no narrowing cast UB).
- **Daemon (`daemon/main.cpp`)**
  - JSON log output escapes every `message` field (`\" \\ \n \r \t \b \f`,
    control chars → `\uXXXX`), so device names/paths/errno text can never
    corrupt the log stream.
  - `--config=PATH` / `--log-format=FMT` (`=` forms) accepted next to
    `-c PATH` / `--config PATH`; a missing value is a hard parse error
    (exit 1); explicit `--config=` paths receive the same validation as `-c`.
- **Hot-path & latency safety**
  - `lat_stats::record()` (`daemon/lat_stats.hpp`) drops non-finite samples
    and clamps negative ones to 0 — histogram invariants stay valid.
  - Lookup zero-width segment (`include/accel-lookup.hpp`): a duplicated X
    (denominator 0) falls through to the next point's `by` instead of
    producing ±Inf; valid strictly-increasing tables are unaffected.
  - Removed a dead guard (`hi < capacity-1`); no behavior change.
- **GUI (`gui/widgets_sync.inl`)**
  - `pkexec_systemctl_async()` returns `pid > 0`, so the systemd start/stop
    branch is actually taken — closes the double-start window where both
    `pkexec systemctl` and a direct `pkexec rawaccel-daemon` were launched.

### Documentation / UX
- README: new "Player profile (oyuncu profili)" section (gaming preset
  table), low-latency & safe-defaults documentation, performance expansion,
  `rawaccel-cli latency` statistics, telemetry-via-`status`.
- AGENTS.md: "Live Telemetry & Seqlock" and "Low-latency motion contract"
  design decisions, translation-coverage rules, hardening parity between
  `scripts/build.sh` and CMake.

### Tooling / QA / packaging
- Differential oracle (`tests/oracle/run_oracle.sh`) vs the vendored official
  RawAccel reference: 768 gain rows — 737 matched at REL 1e-9 + 31 documented
  intentional deviations (expanded with the R35 esport grid); wired into CI
  (`ci.yml`) so accel-math drift fails the build.
- Translation coverage suite (`tests/run_tr_coverage.sh`): every translatable
  UI string must have a Turkish entry (0 MISSING = PASS).
- SYN_DROPPED event-sequence machine tests (8 scenarios) added — 132 test
  groups / 21627 runtime assertions green under ASan+UBSan too.
- Fuzz smoke (60 s) in CI; deep run 11.7M+ executions crash-free.
- PKGBUILD (Arch/CachyOS) with hardened, PIE, `BIND_NOW` binaries; canonical
  `setup.sh` one-shot installer enforcing the dependency policy on the
  pacman/apt/dnf branches.

## [0.4.0] — 2026-09-05

- **Turkish GUI** with a live language switch (Otomatik / English / Türkçe);
  preference persisted in `~/.config/rawaccel/gui_lang`.
- **IPC config push sync**: the GUI sends `set_config` to the root daemon,
  which writes `/etc/rawaccel/settings.json` and applies the change live
  (previously SIGHUP reloaded a stale copy).
- **KDE Plasma double-acceleration protection**: `scripts/kde-fix-accel.sh`
  sets per-device Flat + 0 acceleration in `kwinrc`/`kcminputrc`.
- Reference alignment of all acceleration modes (classic GAIN / jump /
  synchronous / lookup) and the differential oracle harness.
- Hardened build flags in both `scripts/build.sh` and CMake, with
  `RAWACCEL_PORTABLE=1` support.
- Canonical one-shot installer: `sudo bash setup.sh` (deps + build + system
  files + service + KDE fix), `--uninstall`, `--reinstall`; `scripts/install.sh`
  kept as a thin wrapper.
- First Arch/CachyOS package (`packaging/PKGBUILD`).