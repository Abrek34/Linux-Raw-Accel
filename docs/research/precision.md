# Numeric Precision — float vs double, ULP risks, math-library notes

> Author: Aj 8 (P88). Scope: how the accel formulas behave under IEEE-754
> double/float, where precision is lost, and what the build guarantees.
> References: IEEE 754-2008, Goldberg "What Every Computer Scientist Should
> Know About Floating-Point Arithmetic", glibc/libm accuracy notes, Arm
> Libamath ULP docs (see learning.md). Cross-references use `file:line`.

## 1. What the project uses

| Type | Used for | Where |
|------|----------|-------|
| `double` (53-bit, rel ε ≈ 1.11e-16) | All algorithm math, all config fields | everything in `include/accel-*.hpp` |
| `float` (24-bit, rel ε ≈ 5.96e-8) | Synchronous GAIN LUT cell storage only | `accel-synchronous.hpp:138` (`static_cast<float>`), array at `rawaccel-base.hpp:59` |
| `double` epsilon compares | Config equality, mode selection | `rawaccel-base.hpp:65` (`DBL_EPSILON_CMP = 1e-9`), `rawaccel.hpp:124,192-199` |

Good news: the official RawAccel reference itself also computes in `double` and
stores the synchronous LUT as `float` (`ref/accel-synchronous.hpp:143-145`), so
the port is **bit-for-bit faithful** in type choice. The oracle builds both
sides at `-O1` and still sees rel 1e-9 agreement on 737/768 rows.

## 2. Where precision is actually lost (hot path: `au.apply(v, args)` per event)

### 2.1 `pow` argument reduction — the dominant error source
`accel-classic.hpp:162-167` (`base_fn`):
```cpp
double p = std::pow(x - args.input_offset, args.exponent_classic); // :164
double r = ar * p / x;                                              // :166
```
- glibc's `pow` is good to ~0.5–1 ULP (relative ~1e-16), but for fractional
  exponents it internally computes `exp(y · log(x))`. The **absolute** error of
  `log` is multiplied by `|y|`; a 1-ULP error in `log` becomes |y|·ULP in the
  exponent, hence |y| ULP in the result (result relative error ≈ |y|·1.1e-16).
- With the sanitize ceiling `exponent_classic = 10` and a typical low `x − io`
  (≈0.001–1), the input to `pow` is tiny and the exponent large → worst-case
  relative error is a few ULP (≈1e-15). Gaining correctness beyond that
  requires `powl`/custom `exp(y·log x)` in long double or `fma`-compensated
  forms — **not worth it** for a mouse (see 4.1).
- Danger is not silent inaccuracy but **overflow**: at `exp = 10`,
  `pow(1e-3 − io, 10)` is fine, but `pow(x, 10)` for `x > 4.6` overflows;
  the guard at `:165` returns identity `0` instead of Inf — the right trade.

### 2.2 Division by small `x` (relative-error amplifier)
- `accel-classic.hpp:166` `ar · p / x`: at `x = 0.001` the dividend is scaled by
  1000. Relative error of `r` stays ≈ the relative error of `p` (division does
  not add relative error), but **absolute** gate-below speed gets noisy. This is
  the same amplification the classic `exp≤1` linear path avoids.
- `accel-power.hpp:106` `.. + constant / x` and `accel-classic.hpp:60`
  `constant / x + cap_y`: for huge `x` the `constant/x` term → 0 and adds
  nothing visible; for tiny `x` near 0 the term can dominate → the cap tail has
  relative error ≈ 1 ULP, no amplification beyond that.
- `accel-synchronous.hpp:151` `scalbn(x, −e) − 1`: this *extracts the mantissa*
  into [0,1). Exact except the final −1 (Sterbenz-lemma exact), so index
  computation is essentially exact; the precision concentration is all in the
  LUT itself (2.3).

### 2.3 The synchronous GAIN LUT — the only `float` path
`accel-synchronous.hpp:105-142`:
- Trapezoid integration with only **2 partitions per FP-range cell**
  (`interval = (b−a)/2`, `:117-124`) — same as the official reference
  (`ref/accel-synchronous.hpp:90-99`). The *numerical-integration* error of
  the sigmoid per cell is O(interval²)≈O(8e-3) relative locally, dominating all
  floating-point error combined. This is the true accuracy ceiling of the
  synchronous-gain mode — not precision, but **discretization**.
- `static_cast<float>` storage (`:138`) quantizes each cell to 24-bit → up to
  5.96e-8 relative error per cell, and `lerp` (`accel-lookup.hpp:34-40`)
  interpolates between two quantized endpoints. Summed with the trapezoid error,
  the final gain is accurate to ~1e-4 … 1e-3 relative in the worst (steepest)
  cells of the sigmoid — invisible to the mouse (which has integer-count output)
  and only marginally visible in the GUI curve.
- `data[idx]` is read back as `double` for lerp (`:158-159`); a `double` LUT
  would remove the quantization term but **not** the trapezoid term, so it
  would buy almost nothing while increasing memory traffic on the hottest array.

### 2.3.1 Measured N=2 vs N=4 vs exact integral (P102, Aj 8)

Reference params `sync_speed=5, smooth=0.5, motivity=1.5, gamma=1`, faithful
`fill_lut` reproduction (cross-checked against the real header — bit-equal to all
printed digits). "Exact" = 400k-point trapezoid of ∫ sigmoid over [0,x], i.e.
the ideal average-sensitivity the LUT approximates. `flt-ULP` = the gain error
expressed in float32 ULPs of the exact gain (float32 ULP at gain g = 2^(ilogb(g)−23)).

| ips | N=2 err% | N=4 err% | N=2 flt-ULP | N=4 flt-ULP | N2/N4 |
|-----|----------|----------|-------------|-------------|-------|
| 0.5 | 4.5e-05  | 4.5e-05  | 5           | 5           | 1.0   |
| 1   | 3.9e-04  | 1.7e-04  | 44          | 18          | 2.4   |
| 3   | 1.2e-01  | 6.0e-02  | 1.4e4       | 6.8e3       | 2.0   |
| 5   | 8.8e-01  | 4.4e-01  | 1.1e5       | 5.4e4       | 2.0   |
| 7.5 | 1.15     | 0.58     | 1.7e5       | 8.6e4       | 2.0   |
| 10  | 9.5e-01  | 4.8e-01  | 8.1e4       | 4.1e4       | 2.0   |
| 15  | 5.9e-01  | 3.0e-01  | 5.9e4       | 3.0e4       | 2.0   |
| 30  | 2.7e-01  | 1.3e-01  | 3.0e4       | 1.5e4       | 2.0   |
| 50  | 1.5e-01  | 7.7e-02  | 1.8e4       | 9.1e3       | 2.0   |
| 100 | 7.4e-02  | 3.7e-02  | 9.0e3       | 4.5e3       | 2.0   |
| 250 | 2.9e-02  | 1.5e-02  | 3.6e3       | 1.8e3       | 2.0   |
| 500 | 1.4e-02  | 7.2e-03  | 1.8e3       | 9.0e2       | 2.0   |
| 1e3 | 7.1e-03  | 3.5e-03  | 8.9e2       | 4.5e2       | 2.0   |
| 2e3 | 3.6e-03  | 1.8e-03  | 4.5e2       | 2.3e2       | 2.0   |
| 4e3 | 1.8e-03  | 9.3e-04  | 2.3e2       | 1.2e2       | 1.9   |

Readings:
- **The greedy ruler is % error, not ULP.** Discretization error (trapezoid vs
  exact) is a *bias*, not a rounding artefact: 10⁴–10⁵ float32 ULPs at the knee
  correspond to only 0.9–1.15% relative gain error. Float32 storage itself
  contributes ≤1 ULP (≤5.96e-8) per cell — invisible next to the trapezoid term.
- **Worst N=2 error is ~1.15% and only in the 5–15 ips knee** (sigmoid steepest,
  straddles `sync_speed`). Above ~250 ips (typical flick/track) it is <0.03%.
- **N=4 halves the error everywhere** (N2/N4 ≈ 2.0), i.e. one less halving of the
  `O(interval²)` trapezoid term. At 0.5 ips both are ~equal (5 ULP) because the
  cell is already flat there.
- Second-order nuance (measured, smooth=4/sync=0.05 test): when the operating
  speed is *far* from `sync_speed`, N=2 and N=4 converge (the curve is flat and
  the trapezoid is exact there); the halving is concentrated in the ips band near
  `sync_speed`, which is exactly where a synchronous user points their cursor.

### 2.3.2 float→double feasibility (P102) — what `data[514]` actually is

`accel_args::data` (`rawaccel-base.hpp:59`, `mutable float data[514]`, same in
the vendored ref `ref/rawaccel-base.hpp:65`) is consumed in three ways:

1. **Lookup mode (user-edited LUT):** GUI LUT editor `lut_get_points` /
   `lut_set_points` (`gui/graph.inl:307-343`) read/write `data[i]` directly and
   serialize `lut_data` to config JSON, **but only when `mode == lookup`**
   (`src/config.cpp:84`, gate `:84-90`, load `:124-150`). Lookup editor UI is
   hidden for synchronous mode (`update_lut_visibility`, graph.inl).
2. **Synchronous GAIN (internal LUT):** `fill_lut` overwrites `data` at
   construction from the params; `gain_apply` reads `data[idx]` per event. This
   is a *scratch buffer* — rebuilt on every `init_settings()` /
   `accel_union::init()` (SIGHUP reload, IPC push), **never persisted** (the
   `mode==lookup` gate above excludes it).
3. **Oracle/tests:** `const float*` reads in both lookup and synchronous paths;
   unit tests `memcpy` float arrays into `args.data`
   (`tests/test_accel.cpp:388,412,424,603`); the vendored reference reads it as
   `reinterpret_cast<const vec2<float>*>` (`ref/accel-lookup.hpp:57`).

ABI verdict:
- **There is no cross-process binary ABI.** All IPC is text JSON
  (`app_config_to_json`), and the struct is never memcpy'd/serialized by size
  across a boundary. `sizeof(accel_args)` growth (float 2056 B → double 4112 B)
  would not break any daemon/GUI/CLI channel.
- **There IS a source-level contract.** Every in-tree consumer assumes `float`
  elements; changing to `double` touches `accel-synchronous.hpp`,
  `accel-lookup.hpp`, `gui/graph.inl`, `src/config.cpp` (load casts to float),
  the tests listed above, and the oracle's ref-side `vec2<float>` cast. It is a
  mechanical ~7-file change, not a one-line edit.
- **It would break the oracle even at N=2.** Local double-storage vs ref
  float-storage makes LUT cells differ by up to 1 float ULP → applied gain
  drifts up to 5.0e-8 relative (measured, 21/23 grid speeds > TOL 1e-9). Same
  "drift-by-divergence" problem as N=4, at 50× smaller magnitude and ~zero
  perceptible benefit (≤1 ULP is rounding noise, already invisible).
- Config JSON: only lookup mode serializes `data`; a double array would dump
  more digits for lookup users but loads back as float today (`:148`), so the
  on-disk schema is unchanged. No version bump needed for the format — but the
  round-trip quieting that `LUT_EPSILON=1e-5` was tuned around
  (`rawaccel-base.hpp:66`) belongs to the float world; don't change it.

### 2.4 Construction-time solves (once per config load, not hot)
- `accel-classic.hpp:76-77,82,119`: `pow(a, exp−1)` — same argument-reduction
  concern as 2.1; a wrong `constant` here shifts the whole tail. Guarded by
  `isfinite` checks (`:75-77,83,118-120,156-158`).
- `accel-power.hpp:126`: `pow(gain/(n+1), 1/n)` for tiny `n` — exponent up to
  1e4 (sanitize floor `n=1e-4`); the reference overflows to Inf here, the port
  floors `n` to 1e-3 inside `scale_from_gain_point` (`:125`) and falls back to
  `scale=1` (`:127`). This is a **correctness winner** over the reference, not
  an approximation.

## 3. ULP risks near thresholds/branches

- **Cap-position branches** compare `x` against `cap_x`
  (`accel-classic.hpp:57`, `accel-power.hpp:96`, `:71`): these are computed
  from inverse-`pow` solves, so a speed equal to `cap_x` to within 1 ULP can
  land on either side. The two sides are **tangent-continuous by construction**
  (`constant = (base_fn(cap_x) − cap_y)·cap_x`), so a 1-ULP branch flip changes
  the gain by ~1e-15 — no visible discontinuity, no acceleration "pop". This is
  exactly why the GAIN caps feel smooth; the LEGACY hard caps
  (`accel-power.hpp:92`, `accel-classic.hpp:51`) do **not** have this property
  and a ULP-scale jitter around the cap is a genuine (inherited-from-reference)
  non-smoothness — inherent, not a port defect.
- **`x == syncspeed` branch** (`accel-synchronous.hpp:79` legacy): exact
  double equality is relied on the same way the reference does
  (`ref/accel-synchronous.hpp:53`); a ULP-off `x` takes the tanh path which is
  continuous there, so no discontinuity. Fine.
- **`idx < capacity−1`** (`accel-synchronous.hpp:157`): `clamp` to
  `range.size()−2` already bounds the index (P86, `:154`); the residual
  `idx_f − idx` ∈ [0,1) is the lerp fraction, exact to 1 ULP.
- **`minsd(base_fn, cap)` with NaN** (`accel-classic.hpp:51`): `min(·,NaN)`
  returns NaN per IEEE ordering rules, so any internal NaN would poison the gain
  (this is why the constructor NaN guards exist, `:75-77,83-84`). Same guard set
  exists in power (`:125-128`).

## 4. Math library / build-flag concerns

### 4.1 No `-ffast-math` anywhere — and that is correct
`scripts/build.sh:75` and `CMakeLists.txt` use `-std=c++20 -O3 -march=native`
(+ hardening). **No `-ffast-math`, no `-Ofast`, no `-funsafe-math-optimizations`.**
Consequences, all desirable for this code:
- No flush-to-zero: **subnormals survive**. The pipeline already guards the
  only subnormal hazard — `ips_factor = dpi_factor/time` overflowing to Inf for
  tiny `time` (`rawaccel.hpp:245-247`) — and the NaN/Inf escape check at
  `rawaccel.hpp:365-366`.
- NaN/Inf propagation stays IEEE-defined; every `isfinite` guard in the accel
  headers is meaningful.
- `-ffast-math` would have made `pow`/`exp` contraction unsafe and **broken the
  oracle guarantee** (the ref is compiled without it, so a fast-math local build
  could drift past TOL=1e-9). Keep it off.
- Note: GCC's default `-ffp-contract=fast` at `-O3 -march=native` may FMA-contract
  *arithmetic around* library calls in the hot binaries (`base_fn`'s `ar·p/x`,
  power's `scale·x`, cap tails). FMA keeps a full-precision product before the
  single rounding — strictly more accurate than separate mul+add — but can
  differ in the last ULP from the `-O1` oracle build. This is within noise
  (TOL 1e-9 » 1e-15) and **unobservable**; documented here for completeness.

### 4.2 libm functions used and their worst-case error
| Function | glibc/typical worst rel err | Used at |
|----------|-----------------------------|---------|
| `std::pow(x,y)` frac. y | ~0.5–1 ULP (1e-16…|y|·1e-16) | classic :164,173,178,182,188; power :106,115,126,132 |
| `std::exp` | ~0.5–1 ULP (1e-16) | natural :31,42,56; synchronous :76,90,95; jump :34,42 |
| `std::log` | ~0.5–1 ULP | synchronous :53,57,73,81 |
| `std::tanh` | ~1–2 ULP | synchronous :88,93 |
| `std::scalbn` / `std::ilogb` | exact (bit manipulation) | synchronous :147,151-154 |
| `std::hypot` | 1 ULP, overflow-safe | `math-vec2.hpp:12` (magnitude) |

No `sin/cos` in the hot path except GUI graph/rotation setup
(`math-vec2.hpp:40-41`) — not latency-critical.

## 5. Measured baseline (proof the port is not numerically worse)

- Oracle: 768 rows; 737 within rel 1e-9 of the official reference; 31 documented
  (see deviations.md). At `TOL=1e-11` the same 737 would still pass — the port
  does not add noise beyond libm ULP.
- All NaN/Inf guards are `isfinite`-based (no `x != x`), consistent with
  "-ffast-math would break" reasoning. `rawaccel-base.hpp:63-66` uses epsilon
  comparisons so JSON double→string→double round-trips (sub-ULP strings) cannot
  falsely unlink X/Y profiles.

## 6. Recommendations (accuracy, no deviation changes)

1. **(Optional, low cost)** in `classic::base_fn` rewrite `ar·p/x` (two
   roundings + div) as `ar·(p/x)` or `std::fma` chains only when the compiler
   hasn't already contracted — gains are ≤1 ULP, i.e. cosmetic. Do **not** add
   `-mfma`-specific math.
2. **(The real wins)** synchronous GAIN mode: (a) increase trapezoid
   partitions 2 → 4 (`accel-synchronous.hpp:117`), (b) optionally store the LUT
   as `double` (`:138`, `rawaccel-base.hpp:59`). Both are internal to the GAIN
   LUT and are *not oracle rows* (the LUT content is computed, not compared
   row-by-row) — they change no documented deviation.
   **P102 update:** (a) halves the error from 1.15% → 0.58% worst-case (knee) —
   below the ~1% perceptual threshold even before the change; (b) removes only
   ≤1 ULP of float noise. But neither is oracle-free: measured drift-vs-ref is
   69 rows/23 speeds (N=4, up to 5.7e-3 rel) and 21 rows/23 speeds (double
   storage, up to 5.0e-8 rel). Execute only with a coordinated plan (Options
   A/C/D below; see deviations.md §P102).
3. Keep `-ffast-math` off; document it in CMake for future contributors.
4. If maximizer-level accuracy is ever needed for classic/power tails, the
   highest-leverage change is `pow(x,exp)·...` → `exp(exp·log(x))` with a
   compensated product — but measured benefit is ~1e-15 on a gain the mouse
   truncates to integers; not actionable today.

## 7. P102 decision — ranked roadmap for the synchronous GAIN LUT

Problem restated: the LUT uses 2-partition trapezoids (matches the vendored
official reference exactly); going local-only to N=4 feeds 69 drift rows to the
oracle; float→double is not oracle-free either (21 rows). This ranks the escape
routes by risk ÷ benefit.

| Option | Change | Oracle impact | UX gain | Risk | Verdict |
|--------|--------|---------------|---------|------|---------|
| **D. no change** | none | green (31 known devs) | none (current truth) | none | **Recommended today** |
| **C. N=4 + documented deviation class** | local only, 2→4 | +69 known rows → ~100 total | knee err 1.15→0.58% | new deviation class to maintain; ref divergence grows over time; "what is the oracle verifying?" | viable, deferred |
| **A. N=4 on BOTH local + ref** | 2→4 in both | green; but **ref is no longer verbatim** | knee err 1.15→0.58% | loses the "matches the official reference" property; next `git clone` refresh silently reverts ref→N=2 and re-drifts 69 rows | viable only with a stamped "forked reference" marker + refresh guard |
| **B. keep N=2 + deviation rows** | none (list grows 31→~100) | green | none | deviation list balloons to ~100 rows with zero product benefit | worst option for zero gain |
| float→double | (independent of N) | +21 rows (5e-8 each, ≈0 UX benefit) | ≤1 ULP, imperceptible | touches ~7 files + tests for nothing | **not worth it**; see §2.3.2 |

Decision logic:
1. **The error is sub-perceptual in every config.** Worst N=2 knee error 1.15%
   (relative gain); above 100 ips it is ≤0.074%; the mouse truncates to integer
   counts. Both N=2 and N=4 are far inside what a hand can resolve (sub-1% gain
   deltas are not adjustable in the GUI — spinners step 0.01 in gain, 1 free);
   the *shipped* Windows reference behaves exactly like N=2 today, so no user
   can perceive a "fix".
2. **Option D is therefore the honest recommendation:** no code change; document
   the measured surface so a future need (GUI Z-smoothness at smooth≈0, or a
   bug report requesting 1:1 parity with a hypothetical N=4 Windows build) has a
   ready plan.
3. **If/when a change is wanted, Option A is the cleanest** — but ONLY under a
   coordinated, explicit decision that the oracle's contract changes from
   "matches RawAccelOfficial/rawaccel" to "matches a precision-enhanced,
   MIT-forked reference". That requires a visible stamp (header note + LICENSE
   refresh caveat + CI guard that a `git clone` refresh fails loudly rather than
   silently re-drifting). N=4 was measured to halve the knee error to 0.58% —
   a genuine but imperceptible win.
4. **float→double is a dead end** until Option A lands: it carries 21 rows of
   drift for ≤1 ULP of benefit, and the moment local stores double, the oracle
   cannot compare against a float-storing reference at all.
5. **B is never worth it**: growing the known-deviations list by 69 rows to
   document a change that produces zero perceivable difference.

Recommended eventual action, if any: **A** (with a `ref` "fork stamp"), executed
with `double`-storage folded in **only if** the ABI source-contract ramification
is accepted at the same time. Until then, **D**.