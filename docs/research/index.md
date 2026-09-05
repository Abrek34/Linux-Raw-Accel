# RawAccel Research — Index (team entry point)

> Aj 8 (P88) · maintained by the Research & Learning officer.
> All docs below are reference material; no code changes accompany them.

## Documents

| Doc | What you'll find |
|-----|------------------|
| [formulas.md](formulas.md) | Every mode's exact formula (classic/natural/power/synchronous/jump/lookup/noaccel), the GAIN vs LEGACY distinction, all three cap-mode branches, edge cases, and `include/accel-*.hpp` cross-references. **Start here.** |
| [deviations.md](deviations.md) | Root-cause of the 31 oracle rows (views: `tests/oracle/known_deviations.txt`), per-class KEEP/FIXABLE verdicts, reference values vs local, and why none should be "fixed". |
| [precision.md](precision.md) | float vs double, where ULP is lost in the hot path, `pow` argument-reduction error, the only `float` (synchronous LUT), `-ffast-math` analysis, accuracy recommendations. |
| [learning.md](learning.md) | The RawAccel model origins (InterAccel, Quake/Source heritage), log-space sigmoid rationale, cap theory, and the literature + sources for education. |

## One-line summary of findings

- The local math matches the official reference to rel 1e-9 on 737/768 oracle
  rows; the 31 remaining are **deliberate**: 23 from the classic `exp≤1`
  constant-gain convention (unreachable from any production config; reference
  behaviour is an out-of-domain artifact), 8 from the uniform `x≤0 → 1` guard
  (speed 0 is non-physical; reference yields 0 / 1÷motivity artifacts).
  → **Keep all 31.** No accuracy win is waiting on this surface.
- The two real accuracy ceilings are algorithmic, not precision:
  (1) synchronous GAIN LUT — 2-partition trapezoids + `float` storage;
  (2) nothing else — libm `pow/exp/log` are already 0.5–1 ULP on glibc.

## Verification (current tree)

```
bash tests/oracle/run_oracle.sh   → 768 rows, 31 known, RESULT OK (rel 1e-9)
```

## Research agenda (open ideas, no commitments)

1. Measure synchronous GAIN LUT accuracy (trapezoid vs exact integral) at 2 / 4
   / 8 partitions; quantify vs `float32`/`float64` storage.
2. Compare glibc `pow` vs Intel/windows `pow` for the exact same formula —
   reproducibility across the Windows and Linux builds.
3. Document the classic `exp≤1` linear path in the CLI help text so raw-API
   users are not surprised.
4. Long-term: double-double (or `long double`) evaluation of the cap-tail
   constants — predicted gain ≤ 1 ULP, i.e. unobservable; only if profiling
   shows it free.