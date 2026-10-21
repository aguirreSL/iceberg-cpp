# JUCE patch

`juce-tahoe-sdk-fix.patch` lets JUCE 8.0.12 build with AppleClang 21 (macOS Tahoe). The
CMake configure step applies it to `third_party/JUCE` through
`scripts/apply_juce_patches.sh`, on macOS only, and skips it when it is already applied.

## What it changes

With AppleClang 21, `::kill` and `::sigaction` are not found from inside
`namespace juce`, although `<signal.h>` declares them. The patch is additive and does not
change any logic:

- `modules/juce_core/native/juce_SharedCode_posix.h` and
  `modules/juce_core/system/juce_SystemStats.cpp` declare `kill` and `sigaction` with
  `extern "C"` at file scope, outside `namespace juce`.
- `modules/juce_core/system/juce_PlatformDefs.h` calls `kill` instead of `::kill` in
  `JUCE_BREAK_IN_DEBUGGER`.

## When it can go

After a JUCE update, build once without it:

```sh
git -C third_party/JUCE checkout .
cmake -S . -B build -DICEBERG_BUILD_GUI=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target iceberg-juce -j 2>&1 | grep -E "::kill|::sigaction"
```

If nothing is printed, delete the patch, this file and the patch step in the top-level
`CMakeLists.txt`. Otherwise make the same edits on the new JUCE tree and write the patch
again with `git -C third_party/JUCE diff > patches/juce-tahoe-sdk-fix.patch`.
