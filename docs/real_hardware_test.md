# Real-Hardware Feel Session Protocol (P71)

This document is the step-by-step protocol for validating RawAccel Linux's
"feel" on **real hardware**. This VM contains only virtual mice, so real feel
must be assessed on your physical machine. Run this session on the machine and
mouse you actually game with.

> Play/test each profile long enough to judge it. Per README: "spend
> 30–60 min per config before judging" — for this fast A/B pass we use the
> compact 5–10 min window below, but lengthen it if you are unsure.

---

## 1. Scope

We verify **5 presets** against their stated design intent, plus check latency
per profile:

| Preset | Mode | Design intent |
|--------|------|---------------|
| `gaming` | classic | General FPS: classic curve, limit 1.8 |
| `cs2` | classic | Tactical shooter: early kick-in, cap 1.6 |
| `valorant` | natural | Smooth entry/exit: light gain, high cap 2.0 |
| `apex` | power | Tracking + verticality: fast 180°, output offset 0.9 |
| `fps` | classic | Balanced FPS starting point (moderate accel + cap 1.8) |

`office`, `precision` and `disable` are low-intensity/utility presets and are
out of scope for the latency-focused feel pass (they are still covered by the
user guide).

---

## 2. Pre-flight checklist

Do this once before starting.

```bash
# 1. Install (canonical one-shot installer)
sudo bash setup.sh

# 2. Daemon running?
systemctl status rawaccel
# expect: active (running)

# 3. Default profile is raw 1:1 (raw passthrough, no acceleration)
rawaccel-cli list          # note the default / active profile
rawaccel-cli status        # confirm daemon sees your real mouse (DPI/polling)

# 4. Confirm your mouse's actual DPI + polling rate and that the
#    default profile uses raw 1:1 (raw_passthrough, noaccel).
#    If DPI/polling shown by `status` differ from your mouse's real numbers,
#    correct them — ips math is only accurate when they are right.
```

Double-acceleration check: if you are on **KDE**, ensure pointer acceleration
is flat (see README → "KDE Plasma Setup"):

```bash
bash scripts/kde-fix-accel.sh --check
```

Create the profiles you will test (one-shot):

```bash
# Create from presets, then activate a raw 1:1 baseline for the session
rawaccel-cli create-preset gaming  p71-gaming
rawaccel-cli create-preset cs2     p71-cs2
rawaccel-cli create-preset valorant p71-valorant
rawaccel-cli create-preset apex    p71-apex
rawaccel-cli create-preset fps     p71-fps
rawaccel-cli create-preset disable p71-raw      # raw 1:1 reference
rawaccel-cli set p71-raw
```

The **raw 1:1 baseline** (`p71-raw`) is your reference point for every compare.

---

## 3. A/B test protocol

For each of the 5 presets, in order:

```bash
rawaccel-cli set p71-<preset>
```

Play **5–10 minutes** in your chosen game / aim trainer, then switch back to
the raw baseline and play 1 minute. Judge the differences.

**Focus tasks during each window:**

1. **Micro-aim** — slow corrections, **2–20 mm/s** of physical hand movement.
   - Watch for *sub-1:1 muddy micro-aim*: is the crosshair moving less than
     your hand at very low speed, feeling "stuck" or "washed out"? A good
     preset keeps slow corrections near 1:1 (or slightly above) — never below.
2. **Flicks** — fast 180° / panic flicks.
   - Watch for a *cap plateau feeling*: at high hand speed, does the cursor stop
     accelerating and feel like it hits a speed ceiling mid-flick?
3. **Tracking** — smooth, consistent-speed tracking.
   - Watch for *tracking smoothness*: any hitch, micro-stutter, or non-linear
     jump while following a moving target at constant hand speed.
4. **DPI mismatch** — does the on-screen sensitivity match what your hand does?
   - If a profile feels radically faster/slower than raw at *low* speed, flag a
     DPI/output_dpi mismatch rather than "an accel artifact".

**Record** a line per preset in the Feel Score table below.

---

## 4. Latency measurement (per profile)

Measure processing latency with the built-in histogram for each active profile.
`rawaccel-cli latency` sends `SIGUSR1`; the histogram appears in the daemon log.

```bash
# with the target profile active:
rawaccel-cli latency
journalctl -u rawaccel -n 30    # read p50 / p95 / p99 (µs)
```

Record the three numbers in the table. Do this **while your game is running**
(real load), after each 5–10 min feel window — not cold.

**Reference numbers** (from the project's own synthetic measurements, n=2454,
Aj 1 live data — README): p50 33 µs, p95 86 µs, p99 266 µs. Your real-hardware
numbers should be in the same ballpark while gaming; anything hugely worse at
p95/p99 points at a per-device / queue issue worth reporting (P-round).

---

## 5. Feel score table

Fill in **1–5** for each row (1 = unusable, 3 = acceptable, 5 = excellent).
`overall` is your gut call to accept or reject. Leave latency as measured µs.

| Profile | Slow aim (2–20 mm/s) /5 | Flick (180°) /5 | Tracking /5 | Overall /5 | p50 µs | p95 µs | p99 µs | Notes |
|---------|-------------------------|-----------------|-------------|------------|--------|--------|--------|-------|
| raw 1:1 (baseline) | | | | | | | | |
| `gaming` | | | | | | | | |
| `cs2` | | | | | | | | |
| `valorant` | | | | | | | | |
| `apex` | | | | | | | | |
| `fps` | | | | | | | | |

**What to look for in each row:**

- **Slow aim low (1–2)** → likely sub-1:1 muddy micro-aim. Check the preset's
  `input_offset` / `output_offset`; raise `input_offset` if slow aim feels floaty.
- **Flick low (1–2)** → cap plateau (cap too low / cap_mode wrong). Raise the
  cap or check `limit`.
- **Tracking low (1–2)** → curve too aggressive mid-range, or compositor
  double-acceleration still on.
- **Any row with p95/p99 badly elevated** → note it for the latency P-round,
  independent of feel scores.

---

## 6. Conclusion → acceptance recommendation

After all rows are filled:

- **ACCEPT** a profile if: overall ≥ 4, no task scores below 3, and p95/p99
  are within the reference ballpark.
- **ADJUST** a profile if: overall 3, or one task score is 2 and the cause is
  identifiable (sub-1:1, cap plateau, etc.). Adjust the specific parameter per
  the tuning tips in `docs/user_guide.md`, then re-run only that profile's
  window.
- **REJECT** a profile if: overall ≤ 2, or it shows an unfixable defect —
  report the exact symptom and parameter to the manager (Aj 1) for a fix round.

Record the final verdict here:

| Profile | Verdict (ACCEPT / ADJUST + param / REJECT) | Owner |
|---------|---------------------------------------------|-------|
| `gaming` | | |
| `cs2` | | |
| `valorant` | | |
| `apex` | | |
| `fps` | | |

---

## 7. Session hygiene

- Always compare against the **raw 1:1 baseline** — never a preset vs memory.
- Test at your **real DPI/polling** and the same mousepad / surface you game on.
- Run on the same game, same settings, same resolution for all profiles.
- If KDE: re-run `kde-fix-accel.sh` before the session to be safe.
- One window at a time — reset the table if you rush two presets back to back.
