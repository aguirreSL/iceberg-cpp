# Specification: the spectral helpers of the room calibration

This specification describes eight functions of `src/calibration/spectrum.{h,cpp}`. It contains
no source code. The implementation is written from this document alone, without access to the
ITA-Toolbox source, the Iceberg MATLAB `native_*.m` files, or any earlier implementation of these
functions (including their git history).

## Where they go

Add the eight declarations, each with a `///` doc comment, to `src/calibration/spectrum.h`, and
the definitions to `src/calibration/spectrum.cpp`, in `namespace iceberg::calibration`. Other code
calls them exactly as declared below, so the declarations must not change. The files already
hold, and keep:

- the types `BandMethod { Added, Averaged }`, `CenterFrequencies { nominal, exact }` and
  `BandSpectrum { freq_vector, freq }`;
- the microphone-factor functions and the calibrator check;
- two private helpers in the anonymous namespace that you may use:
  - `bin_frequencies(n_bins, n, fs)`: bin `i` at `i * (fs / n)`;
  - `interp1_linear(x, y, xq)`: MATLAB's linear `interp1`, NaN outside `[x0, x_last]`.

You may read those two files in full. You may also read and use `rfft` and `irfft` from
`src/native/fft.h` (pocketfft with MATLAB's scaling: no scale forward, `1/n` inverse), `hann`
from `src/native/hann.h` (MATLAB's symmetric `hann`), and `spline` from `src/native/spline.h`
(MATLAB's not-a-knot `interp1(..., 'spline')`), with their `.cpp` files.

## Conventions

- `n` is a signal length; a one-sided spectrum has `n_bins = n/2 + 1` bins (integer division),
  bin `i` at frequency `f[i] = i * (fs / n)`.
- `|z|^2` is `std::norm(z)`. Sums run in increasing index order. Expressions are evaluated in
  the order written.
- `round` is `std::round` (halves away from zero) and `lround` is `std::lround`.

## 1. `one_sided_scale`

```
Eigen::VectorXd one_sided_scale(Eigen::Index n);
```

The scale that turns `rfft` bins into the one-sided amplitude convention of a power signal.
It returns `n_bins` values:
- every value is first `sqrt(2.0) / n`;
- then the first value (DC) is set to `1.0 / n`;
- then the last value is set to `sqrt(2.0) / (2.0 * n)` when `n` is even and to `sqrt(2.0) / n`
  when `n` is odd. This step comes last, so it overrides DC when there is a single bin.

Here `n` is converted to double.

## 2. `one_sided_spectrum`

```
Eigen::VectorXcd one_sided_spectrum(const Eigen::VectorXd& x);
```

`rfft(x)`, each bin multiplied by `one_sided_scale(x.size())` of the same bin, as a complex
product with a factor whose imaginary part is 0.

## 3. `spk2level`

```
double spk2level(const Eigen::VectorXd& x, BandMethod method);
```

With `S = one_sided_spectrum(x)` and `P` the sum of `|S[i]|^2` over all bins: `Added` returns
`sqrt(P)`, and `Averaged` returns `sqrt(P / n_bins)`.

## 4. `ansi_center_frequencies`

```
CenterFrequencies ansi_center_frequencies(double f_low, double f_high, int bands_per_octave,
                                          double fs);
```

The base-2 fractional-octave band centres (IEC 61260 / DIN EN 61260, `1000 * 2^(k/B)`) between
two limits, with nominal labels. Let `B` be `bands_per_octave` as a double.

1. `hi = min(f_high, fs / 2.0 * 2^(-1.0 / B / 2.0))`, which keeps the top band half a band below
   Nyquist.
2. `kmax = lround(B * log2(hi / 1000.0) + 1.0)` and `kmin = lround(B * log2(1000.0 / f_low) + 1.0)`.
3. For each integer `k` from `-kmin` to `kmax` inclusive, in increasing order:
   - `ex = 1000.0 * 2^(k / B)`, with `k` converted to double before the division;
   - keep the band only if `ex > f_low * 2^(-0.5 / B)` and `ex < hi * 2^(0.5 / B)`;
   - nominal label: with `g1 = SD(ex, 5.0)` and `g2 = SD(ex, 100.0)`, the label is `g2` if
     `|100.0 * (1.0 - g1 / g2)| < 4.0`, otherwise `g1`;
   - append the label to `nominal` and `ex` to `exact`.
4. `SD(a, m)`, rounding to three significant digits on a grid of `1/m` of the last digit:
   - if `a == 0`, the result is 0;
   - `d = ceil(log10(|a|))`, and if `|a| - 10^d == 0`, add 1 to `d`;
   - `u = 10^(3 - d) / m`;
   - the result is `1.0 / u * round(u * a)`.

## 5. `invert_spk_regularization`

```
Eigen::VectorXd invert_spk_regularization(const Eigen::VectorXd& x, double fs, double f_low,
                                          double f_high, double beta = 1e-10);
```

The regularised inverse of `x` (Kirkeby and Nelson; Farina), with a small regularisation inside
`[f_low, f_high]` and a large one outside, joined by Hann crossfades.

1. `n = x.size()`, `n_bins`, `f[i]` as above, and `X = rfft(x)`.
2. Crossfade `XF(a, b, f0, f1)`, for two vectors of `n_bins` values and two frequencies. It
   returns a vector that equals `a` up to `f0`, equals `b` from `f1`, and blends the two in
   between:
   - `f1 = min(f1, f[n_bins - 1])`;
   - `d = f[1] - f[0]`, `j0 = round(f0 / d)` and `j1 = round(f1 / d)` (as integers), and
     `m = j1 - j0`;
   - if `m != 0`, `w` is `hann(2m + 1)`; `up` is its first `m + 1` values and `down` its last
     `m + 1` values. If `m == 0`, `up` and `down` are both the single value 0.5;
   - in a copy of `a`, multiply elements `j0 .. j0 + m` by `down`, element by element, and set
     every element after index `j1` to 0;
   - in a copy of `b`, set elements `0 .. j0 - 1` to 0 and multiply elements `j0 .. j0 + m` by
     `up`;
   - the result is the sum of the two copies.
3. Regularisation:
   - `E = XF(ones, beta * ones, f_low / sqrt(2.0), f_low)`;
   - `top = min(f_high * sqrt(2.0), fs / 2.0)`. If `f_high < top`, `E = XF(E, ones, f_high, top)`;
   - `p` is the largest `|X[i]|`, and each `E[i]` becomes `E[i]^2 * (p * p * 50.0 / 100.0)`;
   - if `E[0] > 0`, `E[0]` is doubled.
4. With `s = one_sided_scale(n)`, the inverse spectrum is
   `R[i] = (conj(X[i]) / (|X[i]|^2 + E[i])) / s[i]`. Each division is of a complex number by a
   real one.
5. The result is `irfft(R, n)`.

## 6. `normalize_spk`

```
Eigen::VectorXcd normalize_spk(const Eigen::VectorXcd& freq_data);
```

Every bin divided by the largest bin magnitude, a complex number divided by a real one.

## 7. `spk2frequencybands`

```
BandSpectrum spk2frequencybands(const Eigen::VectorXcd& freq_data, Eigen::Index n, double fs,
                                int bands_per_octave, double f_low, double f_high,
                                BandMethod method);
```

The band spectrum of a one-sided spectrum of an `n`-sample signal.

1. If `freq_data.size() != n/2 + 1`, throw `std::invalid_argument`.
2. `f` are the bin frequencies; `lo = max(f_low, f[0])` and `hi = min(f_high, f[n_bins - 1])`.
3. `C = ansi_center_frequencies(lo, hi, bands_per_octave, fs)`; `freq_vector` is `C.nominal`.
4. With `e = 2^(1.0 / (2.0 * bands_per_octave))`, band `b` spans `C.exact[b] / e` (lower) to
   `C.exact[b] * e` (upper):
   - `first` is the first bin with `f >= lower`;
   - `last` is the last bin with `f < upper`;
   - if `last < first`, the band value is NaN;
   - otherwise `P` is the sum of `|freq_data[i]|^2` for `i = first .. last`. For `Averaged`,
     `P` is divided by `last - first + 1`. The band value is `sqrt(P)`.

## 8. `smooth_frequency`

```
Eigen::VectorXcd smooth_frequency(const Eigen::VectorXcd& freq_data, Eigen::Index n, double fs,
                                  double window_width = 1.0);
```

Fractional-octave smoothing of the magnitude on a logarithmic frequency grid, with the phase
kept.

1. If `freq_data.size() != n/2 + 1`, throw `std::invalid_argument`.
2. `f` are the bin frequencies, and `mag[i] = |freq_data[i]|`.
3. Log grid, one point per cent over the 20 octaves below Nyquist:
   - `fmin = (fs / 2.0) * 2^(-20.0)`;
   - 24001 points, `g[c] = fmin * 2^(c / 1200.0)` for `c = 0 .. 24000`.
4. `y = interp1_linear(f, mag, g)`.
5. Moving average with width `w0 = round(window_width * 1200.0)` points:
   - make the width odd: `w = w0 - 1 + (w0 % 2)`, then `w = min(w, L)`, where `L` is the length
     of `y`;
   - if `w == 1`, the result is `y`;
   - otherwise the result has `L` values, built in three parts, in this order:
     1. Rising edge: keep a running sum `r` of `y[0], y[1], ..., y[w - 3]`. After adding `y[i]`,
        when `i` is even, append `r / (i + 1)`. This gives `(w - 1) / 2` values, the means of
        the first 1, 3, 5, ... points.
     2. Full windows: with `q = 1.0 / w`, for each `k` from `w - 1` to `L - 1`, start
        `z = q * y[k - w + 1]`, then for `j` from `w - 2` down to 0 set `z = q * y[k - j] + z`;
        append `z`. (Each output adds its terms from the oldest sample to the newest.)
     3. Falling edge: running sums from the end, `t[i] = y[L-1] + y[L-2] + ... + y[L-1-i]` for
        `i = 0 .. w - 3`, accumulated in that order. Then for `i = w - 3, w - 5, ..., down to 0`,
        append `t[i] / (i + 1)`. This gives `(w - 1) / 2` values, the means of the last 5, 3, 1
        points, ending with the last point alone when `w == 3`.
6. Back to the bins: `out_mag[0] = mag[0]`. For bins `1 .. n_bins - 1`, `out_mag` is
   `spline(g[1..], smoothed[1..], f[1..])`, that is the not-a-knot spline through the grid
   points from index 1 on, evaluated at the bin frequencies from bin 1 on.
7. The result is `out_mag[i] * exp(i * arg(freq_data[i]))`, a real magnitude times
   `std::exp` of a purely imaginary `std::complex<double>`.

## Acceptance

- The library and the tests build, and the whole suite passes against the MATLAB reference data
  (`-DICEBERG_REQUIRE_FIXTURES=ON`). `CalibrationDsp.*` and `CalibrationMeasure.*` compare these
  functions, and the measurement chain built on them, with MATLAB at 1e-10.
- The outputs are then compared, outside this branch, with the previous implementation on many
  inputs.
