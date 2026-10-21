# Specification: `iceberg::native::time_window`

This specification describes what the render's time-domain window does. It contains no source
code. The implementation is written from this document alone, without access to the
ITA-Toolbox source, the Iceberg MATLAB `native_*.m` files, or any earlier implementation of this
function (including its git history).

## Interface

Files: `src/native/time_window.h` (includes `iceberg_signal.h`) and
`src/native/time_window.cpp`. Other code calls it exactly as declared, so the declaration must
not change.

```
namespace iceberg::native {
enum class WindowType { Rectwin, Hann };
Signal time_window(const Signal& s, double start_sec, double end_sec,
                   WindowType wt = WindowType::Hann);
}
```

`iceberg::Signal` (`src/iceberg_signal.h`) holds `Eigen::MatrixXf samples` (samples by channels)
and `double sample_rate`. The window shape comes from `iceberg::native::hann(int n)` in
`src/native/hann.h` (MATLAB's symmetric `hann`, returned as `Eigen::ArrayXd`); you may read
`src/native/hann.h` and `src/native/hann.cpp` and call it.

## Behaviour

The function keeps the start of the signal, fades a range out with the falling half of a Hann
window (or keeps it as is, for `Rectwin`), and zeroes what follows. Let `N` be the number of
samples and `fs` the sample rate. Indices here are 1-based, as in the original definition.

1. Raw bounds: `a = R(start_sec * fs) + 1`. If `end_sec` is exactly `0.0`, it means "to the end
   of the signal" and `b = N + 1`; otherwise `b = R(end_sec * fs) + 1`. `R` rounds to the
   nearest integer with halves away from zero (`std::round`), and the results are integers.
2. Each bound is moved up to the next even number: an odd value gets 1 added, an even value
   stays. Use a modulo that is non-negative for negative values.
3. If the end bound is now exactly `N + 1`, decrease it by one.
4. Clamp both bounds to `[1, N]`.
5. The window length is `L = end - start + 1`. If `L <= 0` or `N == 0`, return a signal with 0
   rows, the same number of columns and the same sample rate.
6. Window values `w[0], ..., w[L-1]`:
   - `Rectwin`: all 1.
   - `Hann`: the last `L` values of `hann(2 * L)`, that is the falling half of a symmetric
     Hann window of length `2L`.
   The values are computed in double and then cast to float once each.
7. Output, the same shape and sample rate as the input, for every channel:
   - samples before `start` are copied unchanged;
   - samples `start` to `end` are multiplied, in float, by `w[k]` (the float value), with `k`
     counted from 0 at `start`;
   - samples after `end` are 0.

## Acceptance

- The library and the tests build, and the whole suite passes against the MATLAB reference data
  (`-DICEBERG_REQUIRE_FIXTURES=ON`).
- The outputs are then compared, outside this branch, with the previous implementation on many
  inputs.
