# Specification: `iceberg::calibration::filter_bandpass`

This specification describes the zero-phase band-pass filter of the room calibration. It
contains no source code. The implementation is written from this document alone, without access
to the ITA-Toolbox source, the Iceberg MATLAB `native_*.m` files, or any earlier implementation
of this function (including its git history).

## Interface

Add the declaration to `src/calibration/bandpass.h` and the definition to
`src/calibration/bandpass.cpp`, after the functions already there. Other code calls it exactly as
declared, so the declaration must not change:

```
namespace iceberg::calibration {
Eigen::VectorXd filter_bandpass(const Eigen::VectorXd& x, double fs, double f_low,
                                double f_high, int order = 10);
}
```

The same files already hold `butterworth_bandpass_sos` (the digital Butterworth band-pass as
second-order sections, MATLAB's `butter`) and `sosfilt` (MATLAB's `sosfilt`); you may read and
use them. `rfft` and `irfft` come from `src/native/fft.h` (pocketfft with MATLAB's scaling: no
scale forward, `1/n` inverse); you may read `src/native/fft.h` and `src/native/fft.cpp`.

## Behaviour

The filter applies the magnitude of a Butterworth band-pass to the signal with zero phase,
through its spectrum. Let `n` be the length of `x`.

1. `P` is the smallest power of two that is `>= n` (1 when `n <= 1`), and `Nf = min(2^15, P)`.
2. If `n < Nf`, throw `std::invalid_argument`, with a message saying that below `2^15` samples
   the length must be a power of two. (Every caller passes `2^15` samples or more.)
3. Impulse response: an impulse of `Nf` samples (1 at index 0, the rest 0) is filtered by
   `sosfilt(butterworth_bandpass_sos(order, f_low, f_high, fs), impulse)`. The result is
   zero-padded to `n` samples.
4. `H` is the magnitude of `rfft` of that padded response (`n/2 + 1` values).
5. Each bin of `rfft(x)` is multiplied by `H` of the same bin, as a complex product with a
   factor whose imaginary part is 0.
6. The result is `irfft` of that spectrum with length `n`.

`butterworth_bandpass_sos` throws for an odd or non-positive `order` or for band edges outside
`0 < f_low < f_high < fs/2`; those exceptions pass through.

## Acceptance

- The library and the tests build, and the whole suite passes against the MATLAB reference data
  (`-DICEBERG_REQUIRE_FIXTURES=ON`); `CalibrationDsp.*` compares this filter with MATLAB.
- The outputs are then compared, outside this branch, with the previous implementation on many
  inputs.
