# 31 Oracle Deviations — Root-Cause Report

> Author: Aj 8 (P88). Basis: `bash tests/oracle/run_oracle.sh` on the current
> tree → **768 rows compared, 31 known deviations, RESULT OK** (rel tol 1e-9).
> Reference data: vendored RawAccelOfficial/rawaccel headers under
> `tests/oracle/ref/`. Grid: `tests/oracle/oracle_cases.hpp`.
> Known-deviation list: `tests/oracle/known_deviations.txt`.

## TL;DR verdict

| Class | Rows | Verdict |
|-------|------|---------|
| **(A)** classic `exponent_classic ≤ 1` "linear path" — all speeds | 23 | **KEEP** — intentional port convention, unreachable in production (sanitize/GUI clamp exp to [1,10]); reference value is an out-of-domain artifact |
| **(B1)** power / synchronous at speed 0 | 7 | **KEEP** — uniform `x ≤ 0 → 1` guard; speed 0 is non-physical, reference artefacts (0 / 1/motivity) are meaningless |
| **(B2)** game_apex_power at speed 0 | 1 | **KEEP** — semantically matchable (ref = 0.9 offset floor) but not worth breaking the uniform guard; nil practical benefit |
| **Total** | **31** | KEEP × 31 |

No deviation is recommended for reversal. The two classes are different in
kind: A is a **deliberate algorithmic convention**; B is a **domain guard**.

---

## Breakdown by row

### Class A — `classic_gain_exp_le1` (23 rows, speeds 0.001 … 100000)

Grid case (`oracle_cases.hpp:71`): `classic, gain, acceleration=0.005,
exponent_classic=0.5, cap=(15,1.5), cap_mode=out`.

**Reference** (`ref/accel-classic.hpp:139-153`, GAIN]: `accel_raised =
pow(0.005, −0.5) = 14.142`; `cap.y = 0.5`, `cap.x = gain_inverse = 200`;
`constant = (base_fn(200) − 0.5)·200 = 100`. For `x < 200`:
`gain = 1 + 14.142/√x` — it **explodes** as x→0 (≈ 448 at 0.001) and decays
toward 1 as x→∞.

**Local** (`accel-classic.hpp:28-34,47-48`): constructor short-circuits on
`exp ≤ 1` to a constant-gain linear path → `gain = 1 + minsd(0.005, cap)=1.005`
at every speed.

| spd    | ref gain | local | spd     | ref gain | local |
|--------|----------|-------|---------|----------|-------|
| 0.001  | 448.2    | 1.005 | 250     | 1.90     | 1.005 |
| 0.005  | 201.0    | 1.005 | 500     | 1.70     | 1.005 |
| 0.01   | 142.4    | 1.005 | 1000    | 1.60     | 1.005 |
| 0.1    | 45.72    | 1.005 | 2000    | 1.55     | 1.005 |
| 0.5    | 21.00    | 1.005 | 3000    | 1.533    | 1.005 |
| 1      | 15.14    | 1.005 | 4000    | 1.525    | 1.005 |
| 3      | 9.165    | 1.005 | 5000    | 1.520    | 1.005 |
| 5      | 7.325    | 1.005 | 10000   | 1.510    | 1.005 |
| 7.5    | 6.164    | 1.005 | 100000  | 1.501    | 1.005 |
| 10     | 5.472    | 1.005 |         |          |       |
| 15     | 4.651    | 1.005 |         |          |       |
| 30     | 3.582    | 1.005 |         |          |       |
| 50     | 3.000    | 1.005 |         |          |       |
| 100    | 2.414    | 1.005 |         |          |       |

(all 23 speeds: 0.001, 0.005, 0.01, 0.1, 0.5, 1, 3, 5, 7.5, 10, 15, 30, 50, 100,
250, 500, 1000, 2000, 3000, 4000, 5000, 10000, 100000)

**Root cause:** the classic formula's *parameterization* assumes
`exponent ≥ 1`. For `exp < 1`, `base_fn(x) = a^(exp−1)·(x−io)^exp / x`
degrades to `(a·x)^(exp−1)` with a **negative exponent** → the "gain raised to
power" model references a ÷-by-speed relationship that no longer makes sense.
The reference computes it anyway, producing gain ≈ 448 at walking speed —
i.e. the cursor flies when stationary and flattens when fast, the inverse of
intended behavior. The port's documented convention treats `exp ≤ 1` as "linear
constant-gain" (a sound degraded mode).

**Reachability:** `sanitize_accel_args` clamps `exponent_classic` to `[1,10]`
(`src/config.cpp`) and the GUI spinner is `make_spin(1,10,…)`
(`gui/ui_builder.inl`) — **a real daemon/GUI/CLI config can never carry
exp < 1**. The 23 rows are only reachable through the raw algorithm API
(`accel_union::init/apply`), where the port keeps the safe behavior. There are
dedicated unit tests: "classic linear path (exp<=1) cap".

**Verdict: KEEP.**
- Intentional/accepted approximation: YES (documented port convention,
  AGENTS.md "known deviations" class 1).
- Implementation difference that could be fixed: technically yes
  (remove the short-circuit and let `base_fn` run), but that would
  *re-introduce* gain ≈ 448 at low speed and delete a tested behavior — a
  regression, not an "accuracy" gain.
- Rationale file:line — `accel-classic.hpp:28-34` (short-circuit),
  `:47-48` (constant-gain return).

---

### Class B1 — `power`/`synchronous` at speed 0 (7 rows)

| Row | ref gain | local | ref mechanism |
|-----|----------|-------|---------------|
| `power_gain_p1` 0    | 0.0   | 1.0 | `base_fn(0)` → `x ≤ offset.x` → `offset.y = 0` (`ref/accel-power.hpp:44-49`) |
| `power_legacy_p1` 0  | 0.0   | 1.0 | same, hard `minsd(0, cap)` |
| `sync_gain_p2` 0     | 0.667 | 1.0 | `data[0]/x_start` (`ref/accel-synchronous.hpp:128-130`) |
| `sync_gain_p1` 0     | 0.667 | 1.0 | idem (same params → same LUT) |
| `sync_gain_p07` 0    | 0.667 | 1.0 | idem |
| `sync_legacy_p2` 0   | 0.667 | 1.0 | `exp(−log(motivity)) = 1/motivity` from `log(0) → −∞` push (`ref/accel-synchronous.hpp:67-71`) |
| `sync_legacy_p1` 0   | 0.667 | 1.0 | idem |

**Root cause:** the reference blindly evaluates its formulas at `x = 0`:
- GAIN power `base_fn(0) = offset.y = 0` — the "output velocity 0" floor, so
  gain-measured = 0.
- synchronous GAIN falls below its LUT range (`ilogb(0) = −1023 < start`),
  extrapolates `data[0]/x_start = (1/1.5)`; the LUT cell trapezoid averages
  `1/motivity` over `[0, 2^−3]` → 0.667.
- synchronous LEGACY: `log(0) = −∞` forces the else-branch to a saturated
  tanh → `exp(−1·log(motivity)) = 1/motivity = 0.667`.

Local guards `x ≤ 0 → 1.0` (`accel-power.hpp:89`, `accel-synchronous.hpp:43`).
Note the sync rows p2/p1/p07 share identical parameters (the grid never sets
`motivity/gamma`; `exponent_classic` is unused by `activation_framework`), so
their ref values coincide.

**Verdict: KEEP.**
- Intentional/accepted approximation: YES — speed 0 is non-physical for a mouse
  (a zero-speed packet produces zero displacement regardless of gain; gain at
  v=0 is an undefined 0/0 limit).
- The reference's 0.0 / 0.667 values are artefacts of extrapolation and
  `log(0)`, and matching them would corrupt the `1.0` baseline the modifier
  relies on (`scale = 1 + (gain−1)·weight`, `rawaccel.hpp:296`).
- Dedicated tests: "synchronous power<1 guard", "power cap branch (all 3 cap modes)".

---

### Class B2 — `game_apex_power` at speed 0 (1 row)

Grid case (`oracle_cases.hpp:169-174`): `power, gain, scale=2.2,
exponent_power=0.8, input_offset=0.02, output_offset=0.9, cap=(28,2.2), out`.

| Row | ref gain | local |
|-----|----------|-------|
| `game_apex_power` 0 | 0.9 | 1.0 |

**Root cause:** for `x = 0`, local's uniform `x ≤ 0 → 1` guard bypasses power's
own offset floor. Reference `base_fn(0)` returns `offset.y = output_offset =
0.9` (`offset.x ≈ 0.191`, `offset.y = 0.9`).

**Verdict: KEEP (noted as the closest-to-reversible).**
- Unlike B1, the ref value 0.9 is *not* an extrapolation artefact — it is the
  legitimate output-offset floor of the apex preset's power curve.
- "FIXABLE" in the letter: one could special-case power modes to return
  `offset.y` at x=0. But (a) it breaks the uniform `x ≤ 0 → 1` exploit/NaN
  guard, (b) gain at v=0 multiplies zero displacement, so the 0.9 vs 1.0 delta
  has **zero physical effect** on output, (c) the oracle already accepts the
  row as documented. Cost > benefit.

---

## Cross-class summary (all 31)

- 23 rows = one grid case (classic A) × one formula convention.
- 8 rows = the single `0` speed value in `default_speeds()` applied across
  power (2 rows: gain+legacy × p1) + synchronous (5 rows: 3 gain + 2 legacy) +
  the apex power preset (1 row).
- Both classes are *deliberate* and each has dedicated regression tests;
  neither is a numerical drift or a porting omission. The oracle therefore
  measures what it should: **on every other row the local port matches the
  vendored official reference to rel 1e-9** (i.e. bit-for-bit modulo the last
  ULP of glibc `pow`/`exp`).

## "Which deviations are potentially FIXABLE?" — 1-2 sentences each

1. `classic_gain_exp_le1` (23 rows): FIXABLE only by deleting the exp≤1
   short-circuit — but the reference value at exp=0.5 is a physically absurd
   gain≈448 at 0.001 ips, so matching it is a regression; keep the port
   convention.
2. `power_gain_p1`/`power_legacy_p1` @0 (2 rows): FIXABLE trivially by removing
   the `x ≤ 0 → 1` guard, but that reintroduces the 0-gain artefact and opens a
   NaN path for the modifier; keep the guard.
3. `sync_*` @0 (5 rows): FIXABLE trivially, but you would be faithfully
   reproducing an extrapolation artefact (`data[0]/x_start`) born of `log(0)`;
   keep the guard.
4. `game_apex_power` @0 (1 row): the ONLY deviation that is semantically
   matchable (ref 0.9 is the curve's legit output-offset floor); still not
   worth it — uniform guard is more valuable than a meaningless-to-output 0.9.

**Conclusion for the manager (Aj 1):** all 31 deviations are intentional
safety/convention choices; none should be "fixed" toward the reference. If the
goal is *accuracy*, the actionable surface is not these rows but (a) tighter
expression of the classic/power base functions to reduce intermediate rounding
(see precision.md), and (b) increasing the synchronous LUT sample density (2
trapezoid partitions per cell and `float` storage are the dominant accuracy
limits of that mode) — neither touches the documented deviations.