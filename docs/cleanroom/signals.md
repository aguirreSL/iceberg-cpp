# Specification: the signals of the room calibration

This specification describes `src/calibration/signals.{h,cpp}`: the general time window of the
calibration, peak normalisation, the measurement sweep and pink noise. It contains no source
code. The implementation is written from this document alone, without access to the
ITA-Toolbox source, the Iceberg MATLAB `native_*.m` files, or any earlier implementation of
these functions (including their git history).

## Interface

Write `src/calibration/signals.h` and `src/calibration/signals.cpp`. Other code calls these
exactly as declared, so the declarations must not change:

```
#include "native/time_window.h"   // iceberg::native::WindowType { Rectwin, Hann }

namespace iceberg::calibration {
enum class RangeUnit { Seconds, Samples };

Eigen::VectorXd time_window(const Eigen::VectorXd& x, double fs,
                            const std::vector<double>& range, RangeUnit unit,
                            iceberg::native::WindowType type = iceberg::native::WindowType::Hann,
                            bool symmetric = false);
Eigen::VectorXd normalize_peak(const Eigen::VectorXd& x);
Eigen::VectorXd sweep(double stop_margin, double fs, int fft_degree);
Eigen::VectorXd pink_noise(const Eigen::VectorXd& white, double fs);
}
```

Signals are mono `double` vectors, and every computation is in double. You may read and use:
- `rfft` and `irfft` from `src/native/fft.h` (pocketfft with MATLAB's scaling: no scale forward,
  `1/n` inverse; `irfft(H, n)` is the real signal of a Hermitian half spectrum);
- `hann` from `src/native/hann.h` (MATLAB's symmetric `hann`);
- `src/native/time_window.h` (only for the `WindowType` enum);

with their `.cpp` files. `pi` is `3.14159265358979323846`.

## 1. `time_window`

A window on one or two ranges, with fade-outs and fade-ins. Throw `std::invalid_argument` if
`range` does not hold 2 or 4 values.

### Bounds

The bounds are the values of `range`, kept as doubles. With `RangeUnit::Samples` they are
1-based sample positions as given. With `RangeUnit::Seconds`, with `n` the length of `x` as a
double, convert them in this order:
1. each value `v` becomes `round(v * fs) + 1.0` (`std::round`);
2. if the second value equals `n + 1.0`, subtract 1 from it;
3. each value `v` becomes `v + fmod(v, 2.0)` (`std::fmod`), which moves an odd value up to the
   next even one.

### Applying the bounds, `W(y, bounds, type, symmetric)`

1. With `n` the length as a double: if the largest bound is greater than `n`, every bound
   becomes `min(bound, n)`.
2. If there are 4 bounds, first set `y = W(y, {bounds[2], bounds[3]}, type, false)`: the second
   pair is applied first, without the symmetric option, and goes through this same procedure,
   step 1 included.
3. Then apply the first pair, with `s = (long) bounds[0]` and `e = (long) bounds[1]` (conversion
   toward zero), as described next.

### One pair `s, e` (1-based) on a signal of `N` samples

- If `s < 1` or `e < 1`, throw `std::invalid_argument`.
- If `s == e` and `s == N`, return the signal unchanged.
- Start from a weight vector `w` of `N` ones:
  - `Rectwin`: if `s <= e`, the weights of samples `e + 1 .. N` become 0. Otherwise the weights
    of samples `1 .. e - 1` become 0.
  - `Hann`: let `h = |e - s| + 1` and `H = hann(2h)`.
    - If `s <= e`: the weights of samples `s .. e` become the last `h` values of `H` (the falling
      half), and the weights of samples `e + 1 .. N` become 0.
    - Otherwise: the weights of samples `1 .. e - 1` become 0, and the weights of samples
      `e .. e + h - 1` become the first `h` values of `H` (the rising half).
- If `symmetric`: `w` becomes `w + reverse(w)`, and then the minimum of that sum is subtracted
  from every weight. This gives a fade-in and a fade-out from one pair.
- The result is `y` multiplied by `w`, element by element.

## 2. `normalize_peak`

An empty vector is returned unchanged. Otherwise, with `p` the largest absolute value: if
`p > 0`, every element is divided by `p` (a true division); else the vector is returned
unchanged.

## 3. `sweep`

The measurement sweep, built in the frequency domain with a flat magnitude and a group delay
that rises linearly, then refined by repeated windowing.

1. `n0 = 2^fft_degree`, `gd0 = 0.03`, and `gd1 = n0 / fs - stop_margin`. If `gd1 < gd0`, throw
   `std::invalid_argument` (the stop margin is too big).
2. `n = 2 * n0`, `nb = n/2 + 1`, `k = (gd1 - gd0) / nb` and `d = fs / n` (the bin spacing).
3. Phase: with a running sum `r` starting at 0, for `i = 0 .. nb - 1`, add `-(gd0 + k * i)` to
   `r`, then `phi[i] = r * d * 2.0 * pi`.
4. Wrap correction: `q = fmod(phi[nb - 1], pi)`. Then for every `i`,
   `phi[i] = phi[i] - i / (nb - 1) * q`, with `i` and `nb - 1` as doubles, evaluated as
   `(i / (nb - 1)) * q`.
5. Half spectrum: `H[i] = exp(j * phi[i])`, that is `std::exp` of a purely imaginary
   `std::complex<double>`.
6. Window: `wend = min(gd1, n / (2.0 * fs) - gd0)`, and the 4-value range in seconds is
   `{gd0, 0.0, wend, wend + gd0}`.
7. `s = irfft(H, n)`. Then, 51 times: apply `time_window(s, fs, range, RangeUnit::Seconds)`
   (Hann, not symmetric). After each of the first 50 windowings, project back onto a unit
   magnitude: take `rfft(s)`, replace every bin by `exp(j * arg(bin))`, and set `s` to `irfft`
   of that with length `n`. The 51st windowing is not followed by a projection.
8. The result is `normalize_peak` of the first `n0` samples of `s`.

## 4. `pink_noise`

Pink noise from a white draw that the caller makes (so tests can replay MATLAB's).

1. `n` is the length of `white`, and `X = rfft(white)` (`n/2 + 1` bins).
2. For each bin `i`, with `f = i * (fs / n)`, divide `X[i]` by `sqrt` of the purely imaginary
   number `j * (f * 2.0 * pi)`: a `std::sqrt` of `std::complex<double>(0.0, f * 2.0 * pi)`,
   then a complex division.
3. Set `X[0] = 0`. (The division at DC is by zero; its result is discarded.)
4. The result is `irfft(X, n)`.

## Acceptance

- The library and the tests build, and the whole suite passes against the MATLAB reference data
  (`-DICEBERG_REQUIRE_FIXTURES=ON`). `CalibrationDsp.*` and `CalibrationMeasure.*` compare these
  signals, and the measurement chain built on them, with MATLAB at 1e-10.
- The outputs are then compared, outside this branch, with the previous implementation on many
  inputs.
