# Specification: `normalize_dat`, `add`, `time_crop`, `time_shift`

This specification describes what four small signal operations of the render do. It contains no
source code. The implementation is written from this document alone, without access to the
ITA-Toolbox source, the Iceberg MATLAB `native_*.m` files, or any earlier implementation of
these functions (including their git history).

All four live in `namespace iceberg::native`, one header and one source file each, under
`src/native/`. Other code calls them exactly as declared below, so the declarations (names,
parameter types, defaults, the enum and the struct) must not change. `iceberg::Signal` is
declared in `src/iceberg_signal.h`: `Eigen::MatrixXf samples` (samples by channels) and
`double sample_rate`.

## 1. `normalize_dat` (`src/native/normalize_dat.h`, `.cpp`)

```
Eigen::MatrixXf normalize_dat(const Eigen::MatrixXf& signal);
```

Scales a multichannel buffer so that its largest absolute sample, over all channels together,
becomes 1.

- An empty buffer (no rows or no columns) is returned unchanged.
- `g` is the largest absolute value over every element, in float.
- If `g > 0`, every element is divided by `g`: a true float division of each element by `g`,
  not a multiplication by `1/g` (the two differ in the last bit). Otherwise the buffer is
  returned unchanged (an all-zero buffer stays all zero).

## 2. `add` (`src/native/add.h`, `.cpp`; the header includes `iceberg_signal.h`)

```
Signal add(const Signal& a, const Signal& b);
```

Sums two signals after padding both with zeros to a common shape.

- If the two sample rates differ (exact comparison), throw `std::invalid_argument` with a message
  that names both rates.
- The result has `max(rows)` rows and `max(columns)` columns and the sample rate of `a`. Each
  element is the sum of the elements of `a` and `b` at that position, where a position outside an
  operand counts as 0. In float, the sum is `a + b` where both exist, otherwise the one that
  exists, otherwise 0.
- Empty operands are valid and contribute nothing.

## 3. `time_crop` (`src/native/time_crop.h`, `.cpp`; includes `iceberg_signal.h`)

```
Signal time_crop(const Signal& s, double start_sec, double end_sec);
```

Cuts a time range out of a signal with the semantics the render relies on, including an
"inverted range" that removes a block instead of keeping it. Let `N` be the number of samples
and `fs` the sample rate. Indices in this section are 1-based, as in the original
definition; convert carefully.

1. `i1 = R(start_sec * fs) + 1` and `i2 = R(end_sec * fs) + 1`, where `R` rounds to the nearest
   integer with halves away from zero (`std::lround`).
2. Even length: if `i2 - i1` is positive and even, decrease `i2` by one. (A forward range then
   always keeps an even number of samples.)
3. Forward range, `i2 > i1`: if `i1 < 1` or `i2 > N`, throw `std::invalid_argument`. Otherwise
   the result is samples `i1` to `i2` inclusive, all channels.
4. Otherwise (`i2 <= i1`, an inverted or empty range): the result is the head, samples `1` to
   `i2 - 1`, followed by the tail, samples `i1 + 1` to `N`. The head has `max(i2 - 1, 0)`
   samples and the tail `max(N - i1, 0)`. Throw `std::invalid_argument` if the head would be
   longer than `N` or if `i1 < 0`. For the range `[t, 0]` this keeps everything after sample
   `i1`, so the result starts one sample later than "from `t` to the end".
5. The result keeps the sample rate and the channel count of `s`.

## 4. `time_shift` (`src/native/time_shift.h`, `.cpp`; includes `iceberg_signal.h`)

```
enum class ShiftMode { Auto, Samples, Time };

struct TimeShiftResult {
    Signal signal;
    int shift_samples = 0;   // the shift applied, signed
};

TimeShiftResult time_shift(const Signal& s, ShiftMode mode, double amount = 0.0);
```

A circular shift of every channel by the same number of samples `k`.

- `Auto`: `k = -min(onsets)`, where `onsets` is `start_ir(s.samples cast to double, 20.0)`
  (`src/native/start_ir.h`, one 0-based onset per channel). This brings the earliest onset to
  sample 0. When the signal has no rows or no columns, `k = 0`.
- `Samples`: `k = round(amount)`, rounding halves away from zero (`std::round`), as `int`.
- `Time`: `k = round(amount * sample_rate)`, the same rounding.
- The shift is circular along the samples: a positive `k` moves each sample to a later index,
  `out[(r + k) mod N] = in[r]` for every channel, with the modulo taken into `[0, N)` for
  negative `k` too. An empty signal is returned unchanged.
- `shift_samples` is `k` as computed, not reduced modulo `N`. The sample rate is kept.

You may read `src/native/start_ir.h` and `src/native/start_ir.cpp` (a clean reimplementation
written from its own specification) to call it.

## Acceptance

- The library and the tests build, and the whole suite passes against the MATLAB reference data
  (`-DICEBERG_REQUIRE_FIXTURES=ON`).
- The outputs are then compared, outside this branch, with the previous implementations on many
  inputs.
