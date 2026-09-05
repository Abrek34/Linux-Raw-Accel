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
  Recommendation (optional, later): raise partitions to 4 and/or store `double`
  — only if the GUI "Z"-smoothness at `smooth≈0` ever matters.

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
3. Keep `-ffast-math` off; document it in CMake for future contributors.
4. If maximizer-level accuracy is ever needed for classic/power tails, the
   highest-leverage change is `pow(x,exp)·...` → `exp(exp·log(x))` with a
   compensated product — but measured benefit is ~1e-15 on a gain the mouse
   truncates to integers; not actionable today.