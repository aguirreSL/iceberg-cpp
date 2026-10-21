# Specification: `iceberg::calibration::measure_tf`

This specification describes the transfer-function measurement of the room calibration. It
contains no source code. The implementation is written from this document alone, without access
to the ITA-Toolbox source, the Iceberg MATLAB `native_*.m` files, or any earlier implementation
of this function (including its git history).

## Interface

Add the declaration, with a `///` doc comment, to `src/calibration/measure.h`, and the
definition to `src/calibration/measure.cpp`, in `namespace iceberg::calibration`. Other code calls
it exactly as declared, so the declaration must not change:

```
Eigen::VectorXd measure_tf(const Eigen::VectorXd& excitation, double fs, double f_low,
                           double f_high, int averages,
                           const std::function<Eigen::VectorXd(const Eigen::VectorXd&)>& play,
                           const BandSpectrum* eq = nullptr);
```

Place the declaration before `get_tf` in the header, after the `MeasurementError` class.

`measure.cpp` already holds, in its anonymous namespace, two helpers you may use:
- `bins(n, fs)`: the `n/2 + 1` bin frequencies `i * (fs / n)`;
- `apply_one_sided(x, h)`: `rfft(x)` with each bin multiplied by the real `h` of the same bin
  (as a complex factor with a zero imaginary part), then `irfft` with the length of `x`.

Other functions you may call:
- `iceberg::native::pchip` (`src/native/pchip.h`, MATLAB's `pchip`, extrapolating with the end
  pieces);
- `iceberg::native::rfft` and `irfft` (`src/native/fft.h`);
- `invert_spk_regularization` (`src/calibration/spectrum.h`; read only its declaration);
- the type `BandSpectrum` (`freq_vector`, the band centres; `freq`, the band values).

You may read `measure.h` and `measure.cpp` in full, and the `pchip` and `fft` files.

## Behaviour

A swept-sine measurement with deconvolution by a regularised inverse:

1. `n` is the length of `excitation`. The stimulus is `excitation`, unless `eq` is given. Then
   it is `apply_one_sided(excitation, pchip(eq->freq_vector, eq->freq, bins(n, fs)))`: the
   excitation pre-equalised by the band filter, interpolated onto the bins with `pchip`.
2. The compensation spectrum is `rfft(invert_spk_regularization(excitation, fs, f_low, f_high))`,
   the inverse of the excitation itself (not of the pre-equalised stimulus), with the default
   regularisation.
3. Play the stimulus `averages` times: each call `play(stimulus)` returns a recording. If a
   recording's length is not `n`, throw `std::runtime_error` with a message that gives both
   lengths. Sum the recordings element by element, in the order they arrive, starting from
   zeros, and divide the sum by `averages` (as a double).
4. The result is `irfft` with length `n` of `rfft(average)`, multiplied bin by bin by the
   compensation spectrum (a complex product).

Exceptions thrown by `play` pass through (the app uses them to cancel a measurement).

## Acceptance

- The library and the tests build, and the whole suite passes against the MATLAB reference data
  (`-DICEBERG_REQUIRE_FIXTURES=ON`). `CalibrationMeasure.*` runs the whole measurement chain
  against simulated devices and compares it with MATLAB at 1e-10.
- The outputs are then compared, outside this branch, with the previous implementation.
