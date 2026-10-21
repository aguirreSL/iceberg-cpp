# Specification: `iceberg::native::convolve`

This specification describes the render's channel-wise linear convolution. It contains no
source code. The implementation is written from this document alone, without access to the
ITA-Toolbox source, the Iceberg MATLAB `native_*.m` files, or any earlier implementation of this
function (including its git history).

## Interface

Files: `src/native/convolve.h` (includes `iceberg_signal.h`) and `src/native/convolve.cpp`.
Other code calls it exactly as declared, so the declaration must not change.

```
namespace iceberg::native {
enum class ConvolveMethod { FFT, Direct };   // FFT first: it is the default
Signal convolve(const Signal& signal, const Signal& ir,
                ConvolveMethod method = ConvolveMethod::FFT);
}
```

`iceberg::Signal` (`src/iceberg_signal.h`) holds `Eigen::MatrixXf samples` (samples by channels)
and `double sample_rate`. For the FFT method use `rfft` and `irfft` from `src/native/fft.h`
(a pocketfft wrapper with MATLAB's scaling: no scale forward, `1/n` inverse); you may read
`src/native/fft.h` and `src/native/fft.cpp` and call them.

## Behaviour

- If the two sample rates differ (exact comparison), throw `std::invalid_argument` with a message
  that names both rates.
- Let `na` and `nb` be the numbers of samples of `signal` and `ir`, `cs` and `ci` their numbers
  of channels, and `C = max(cs, ci)`.
- Output length: `nl = na + nb - 1` is the linear-convolution length. The output has `no` rows,
  where `no = 0` if `nl <= 0`, otherwise `nl` rounded up to the next even number (an odd `nl`
  gets one more row, which stays 0). The output has `C` columns and the sample rate of
  `signal`, and it starts as all zeros.
- If `na`, `nb`, `cs` or `ci` is 0, return that all-zero output.
- Channel pairing: output channel `k` (0-based) is the linear convolution of signal column
  `min(k, cs - 1)` with IR column `min(k, ci - 1)`. The operand with fewer channels reuses its
  last column.
- Rows `0 .. nl - 1` of each output column hold the linear convolution
  `y[n] = sum over i of a[i] * b[n - i]`.

### `FFT` method (the default)

- Transform size `M`: the smallest integer `>= nl` whose only prime factors are 2, 3 and 5
  (`M = 1` when `nl <= 1`).
- In double: zero-pad each column to `M` samples, take `rfft`, multiply the two spectra bin by
  bin, and take `irfft` with length `M`. The first `nl` values, cast to float, are the output.
- A column's spectrum is computed once and reused when it is paired with several output
  channels.

### `Direct` method

The time-domain sum in float. The products `a[i] * b[j]` are accumulated into `y[i + j]` in
order of increasing `i`, starting from 0. For a fixed `i`, all `j` are added before moving to
`i + 1`, so each output sample receives its terms in increasing `i`.

## Acceptance

- The library and the tests build, and the whole suite passes against the MATLAB reference data
  (`-DICEBERG_REQUIRE_FIXTURES=ON`). The suite has tests for both methods.
- The outputs are then compared, outside this branch, with the previous implementation on many
  inputs.
