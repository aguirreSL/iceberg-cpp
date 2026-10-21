# Specification: `iceberg::native::start_ir`

This specification describes what the function does. It was written from ISO 3382-1:2009,
Annex A.3.4, and from the behaviour the render requires; it contains no source code. The
implementation is written from this document alone, without access to the ITA-Toolbox source,
the Iceberg MATLAB `native_*.m` files, or any earlier implementation of this function (including
the git history of `src/native/start_ir.*`).

## Purpose

The start (onset) of an impulse response, per channel. ISO 3382-1 A.3.4 places the start of the
impulse response where the signal first rises significantly above the background noise, before
the direct sound, and not more than 20 dB below the maximum. The render uses it to align an
impulse response on its onset (`native::time_shift`, automatic mode) and to reference the time
axis of the centre-time computation (`native::center_time`).

## Interface

Files: `src/native/start_ir.h` (declaration, `#pragma once`) and `src/native/start_ir.cpp`.

```
namespace iceberg::native {
Eigen::VectorXi start_ir(const Eigen::MatrixXd& ir, double threshold_db = 20.0);
}
```

- `ir`: samples (rows) by channels (columns), in double.
- `threshold_db`: the level below the maximum, in dB, at which the onset is placed. Only its
  magnitude is used: 20 and -20 mean the same.
- Returns one onset per channel, as a 0-based sample index into the column.
- Existing callers: `src/native/time_shift.cpp` and `src/native/center_time.cpp`, both with
  `threshold_db = 20.0`.

## Behaviour

Each channel is processed independently. For one channel, with `N` samples `x[0], ..., x[N-1]`:

1. If `N` is 0, every onset is 0 (the returned vector has one 0 per column).
2. Energy: `e[k] = x[k]^2`.
3. Noise region: the last part of the response, starting at the 0-based index `m - 1`, where
   `m = round(0.9 * N)` with rounding half away from zero (`std::round`). It runs to `N - 1`
   inclusive. The noise level `P` is the arithmetic mean of `e` over that region.
4. Peak: `p` is the index of the maximum of `e`; when the maximum occurs more than once, the
   earliest index is taken. `E` is `e[p]`.
5. Guard. The onset is 0 (no shift) when any of these holds:
   - the peak is less than 20 dB above the noise level, that is `E < 100 * P`;
   - the peak lies at or after the 0-based index `m`, that is `p + 1 > m` (a peak on the
     first sample of the noise region, `p = m - 1`, does not trigger this guard);
   - the peak is the first sample, `p = 0`.
6. Level relative to the peak, for `k = 0, ..., p`:
   `L[k] = 10 * log10(e[k]) - 10 * log10(E)`, computed as the difference of the two
   logarithms (not as the logarithm of the ratio), in double. A zero sample gives minus
   infinity, which counts as below any threshold.
7. Let `T = -|threshold_db|`.
8. First estimate: `s` is the largest index in `[0, p]` with `L[k] < T` (strictly). If there is
   none, `s = 0`.
9. Oscillation before the onset. Energy that rises again before the onset belongs to the
   response, so the onset must come before it. If some index `k` in `[0, s]` has
   `L[k] > T + 6` (strictly), let `q` be the smallest such index; the onset is then the largest
   index in `[0, q]` with `L[k] < T`, or 0 if there is none. Otherwise the onset is `s`.

## Acceptance

- The library and the tests build, and the whole suite passes against the MATLAB reference data
  (`-DICEBERG_FIXTURES_DIR=$HOME/Documents/git/iceberg-cpp-fixtures -DICEBERG_REQUIRE_FIXTURES=ON`).
  The render compares with Iceberg MATLAB v1.0.0 within the contract; an onset one sample off
  moves the whole render and fails those tests.
- The onsets are then compared, outside this branch, with the previous implementation on every
  channel of every WAV file of the reference data and on synthetic edge cases.
