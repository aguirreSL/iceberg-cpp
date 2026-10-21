# Specification: `iceberg::native::center_time`

This specification describes how the render computes the centre time of an impulse response.
It contains no source code. The implementation is written from this document alone, without
access to the ITA-Toolbox source, the Iceberg MATLAB `native_*.m` files, or any earlier
implementation of this function (including its git history). The method is the centre time
`Ts` of ISO 3382-1, computed on the onset-aligned response, truncated at the point where the
decay meets the background noise and compensated for the truncated tail, with that point found
by the iterative method of Lundeby, Vigran, Bietz and Vorländer ("Uncertainties of measurements
in room acoustics", Acustica 81, 1995).

## Interface

Files: `src/native/center_time.h` (includes `iceberg_signal.h`) and
`src/native/center_time.cpp`. Other code calls it exactly as declared, so the declaration must
not change.

```
namespace iceberg::native {
double center_time(const Signal& ir);
}
```

- Returns `Ts` in seconds, computed on channel 0 of `ir` (the W channel of a B-format IR), in
  double.
- Throws `std::invalid_argument` when `ir` has no rows or no columns.
- Throws `std::runtime_error` in the failure cases marked below. The caller
  (`src/rendering/iceberg_core.cpp`) catches them and falls back to a fixed split.
- Uses `start_ir` from `src/native/start_ir.h` (a clean reimplementation; you may read
  `src/native/start_ir.h` and `src/native/start_ir.cpp`). Its result is a 0-based onset per
  channel; here it is always called with one channel and `threshold_db = 20.0`.

## Conventions

- Everything is in double. `dB(x) = 10 * log10(x)`.
- `R(x)` rounds to the nearest integer with halves away from zero (`std::round`).
- "First maximum" means the earliest index among equal maxima.
- A circular shift by `k` moves element `r` to index `(r + k) mod n`, with the modulo taken into
  `[0, n)`; a negative `k` moves the signal earlier.
- `realmin` is the smallest positive normal double (`std::numeric_limits<double>::min()`).
- Indices are 0-based unless a step says 1-based. Several bounds are defined 1-based because
  that is how the method was specified; convert them carefully.
- Expressions are evaluated in the order written, left to right, as they would be in C++.
- Least-squares line fit: for blocks `i..j`, fit `y = c0 + c1 * t` to the points
  `(t[k], dB(w[k]))`. Solve the tall system `[1 t] * [c0; c1] = y` by QR with column pivoting,
  Eigen's `colPivHouseholderQr().solve(...)`, which is the factorisation MATLAB's backslash uses
  for such a system.

## Part A: onset alignment and trimming

Let `p` be channel 0 cast to double, with `N` samples.

1. `s = start_ir(p, 20.0)(0)` and `k = -s`. Shift `p` circularly by `k`, which moves the onset to
   index 0.
2. `m = N + k + (k % 2)`, where `%` keeps the sign of the dividend (C++ `%`); this is a 1-based
   sample count. If `m < 1`, throw `std::runtime_error`.
3. Set to 0 the shifted samples at 0-based indices `m - 1` to `N - 1` (the part that wrapped
   around).
4. If `m > 1`: remember `m0 = m`. While the sample at 0-based index `m - 2` is exactly 0,
   decrease `m` by one; if `m` reaches 1 this way, throw `std::runtime_error` (an empty channel).
   After the walk, if `m < 5`, set `m = m0`, so a response is never cut below 5 samples.
5. `p` becomes the first `m` samples. Call its length `n`.

## Part B: the crossing point (Lundeby, broadband)

Input: `q`, a copy of `p` from Part A, and the sample rate `fs`. Output: a reverberation time
`T`, a noise energy `P` and a crossing time `ti`. On any "fail" below, the method stops with
`T` and `ti` undefined (NaN), and Part C uses its fallback.

Parameters: averaging window 0.03 s, 5 intervals per 10 dB of decay, a regression that ends
10 dB above the noise and starts 20 dB above that end, a noise window that starts 10 dB of decay
past the crossing point, convergence when the crossing point moves by at most 0.01 s, and at
most 30 iterations.

1. Inner alignment: `s2 = start_ir(q, 20.0)(0)`, `k2 = -s2`, `tshift = k2 / fs`, and `q` is
   shifted circularly by `k2`. (For a response already aligned in Part A, `k2` is 0.)
2. Usable length: `x = -tshift * fs`, computed in floating point from `tshift` (not taken as
   the integer `s2`). `Ld = n - (x + fmod(x, 2.0))`. The usable length `L` is `floor(Ld)` if
   `Ld >= 1`, otherwise 0. Only the first `L` samples enter the block averages.
3. Energy: `e[i] = q[i]^2`.
4. Block averaging with a block length of `b` samples: there are `nb = floor(L / b)` complete
   blocks; block `j` (0-based) has the value `w[j]`, the sum of `e[j*b]` to `e[j*b + b - 1]`
   divided by `b`, and the time `t[j] = (j * b) / fs`, with `j` and `b` converted to double
   before the product. Incomplete trailing samples are ignored.
5. First averaging, with `b = R(0.03 * fs)`. If `nb == 0`, fail.
6. First noise estimate: with the 1-based start `a = nb - R(nb / 10.0)`, `P` is the mean of the
   1-based blocks `a` to `nb`, plus `realmin`.
7. Preliminary regression:
   - `i0` is the first maximum of `w`.
   - `i1` is the largest index `k > i0` with `dB(w[k]) > dB(P) + 10`. If there is none, fail.
   - If `dB(w[i1]) - dB(w[i0]) > -5`, fail.
   - Fit the line over blocks `i0..i1` inclusive. If `c1 == 0`, or `c0` or `c1` is NaN, fail.
8. Preliminary crossing point: `tc = (dB(P) - c0) / c1`. If `tc > (n / fs + tshift) * 2.0`, fail
   (with `n` as a double).
9. New block length:
   - `d = (dB(w[i1]) - dB(w[i0])) / -10.0 * 5.0`, the number of intervals in the fitted decay;
   - `b = R((t[i1] - t[i0]) / d * fs)`, using the `w` and `t` of the first averaging.
   - If `b < 1`, throw `std::runtime_error`.
10. Second averaging, with the new `b`: recompute `w`, `t` and `nb` over the same `L` samples.
    If `nb == 0`, throw `std::runtime_error`. `imax` is the first maximum of the new `w`.
11. Iteration. Set `tcOld = 11.0 + tc` and a counter to 0. While `|tcOld - tc| > 0.01`:
    1. Noise window, 1-based:
       - `aLast = R(nb * 0.9)`;
       - `a10 = max(1, R((tc - 10.0 / c1) * fs / b))`, with `c1` the current slope;
       - `a = min(aLast, a10)`. If `a < 1`, throw `std::runtime_error`.
       - `P` is the mean of the 1-based blocks `a` to `nb`, plus `realmin`.
    2. Late-decay range:
       - `s0` is the first index `k >= imax` with `dB(w[k]) < dB(P) + 10 + 20`, or 0 if there is
         none;
       - `e0` is the first index `k > s0` with `dB(w[k]) < dB(P) + 10`. If there is none, fail.
    3. Fit the line over blocks `s0..e0` inclusive. If `c1 >= 0`, set `c1 = +infinity` and leave
       the loop.
    4. `tcOld = tc`, then `tc = (dB(P) - c0) / c1`.
    5. Add one to the counter; if it is now greater than 30, leave the loop.
12. Result: `T = -60.0 / c1`, `P` as last computed, and `ti = tc - tshift`.

## Part C: the centre time

On the `p` of Part A (length `n`), with `e[k] = p[k]^2` and `t[k] = k / fs`:

1. If Part B failed (`T` or `ti` is NaN), return the centroid of the whole response,
   `sum(e[k] * t[k]) / sum(e[k])` over all `k`.
2. Otherwise:
   - `k1` is the index whose `t[k]` is closest to `ti`, the earliest one on ties;
   - `C = P * T / (6.0 * ln(10)) * fs`, the energy of the tail beyond the crossing point,
     modelled as an exponential decay with reverberation time `T` that starts at the noise level
     `P`;
   - `E = sum(e[0..k1]) + C`;
   - `S = sum(e[k] * t[k] for k = 0..k1) + C * C + C * P * ti`.
   - Return `S / E`.

## Acceptance

- The library and the tests build, and the whole suite passes against the MATLAB reference data
  (`-DICEBERG_REQUIRE_FIXTURES=ON`). `CenterTime.*` compares with MATLAB directly, and the render
  tests depend on it, because the centre time sets where the response is split.
- The results are then compared, outside this branch, with the previous implementation on many
  inputs.
