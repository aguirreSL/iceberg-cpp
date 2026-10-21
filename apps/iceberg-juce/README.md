# iceberg-juce

The standalone Iceberg app, built with JUCE on top of the `iceberg_core` library. What
it does and how to use it is in the [repository README](../../README.md).

## Build

From the repository root:

```sh
cmake -S . -B build -DICEBERG_BUILD_GUI=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target iceberg-juce -j
open build/apps/iceberg-juce/iceberg-juce_artefacts/Release/Iceberg.app
```

The library tests (`ctest --test-dir build`) run with the app on or off. On macOS the build
patches JUCE first, as described in [JUCE_PATCHES.md](../../patches/JUCE_PATCHES.md).
