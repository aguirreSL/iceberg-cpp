cmake_minimum_required(VERSION 3.20)
project(iceberg_cpp VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(FetchContent)

# --- System deps (brew install eigen) --------------------------------------
find_package(Eigen3 REQUIRED NO_MODULE)

# --- Fetched deps ----------------------------------------------------------
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# pocketfft (Reinecke) — single-header C++ rfft/irfft. The canonical
# C++ source lives at gitlab.mpcdf.mpg.de; the github mirror is the C
# version, NOT the C++ header-only flavour we need. License must be
# BSD-3-Clause (verified at configure time in
# cmake/check_pocketfft_license.cmake).
FetchContent_Declare(
    pocketfft
    GIT_REPOSITORY https://gitlab.mpcdf.mpg.de/mtr/pocketfft.git
    GIT_TAG        4d865cba24359b05b37da4e75aee24498c3b78e7   # branch cpp
)
FetchContent_MakeAvailable(pocketfft)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_pocketfft_license.cmake")
if(NOT TARGET pocketfft_hdr)
    add_library(pocketfft_hdr INTERFACE)
    target_include_directories(pocketfft_hdr SYSTEM INTERFACE ${pocketfft_SOURCE_DIR})
endif()

# --- Library target --------------------------------------------------------
add_library(iceberg_core STATIC
    src/native/normalize_dat.cpp
    src/native/add.cpp
    src/native/time_shift.cpp
    src/native/time_crop.cpp
    src/native/time_window.cpp
    src/native/hann.cpp
    src/native/convolve.cpp
    src/native/center_time.cpp
    src/native/fft.cpp
    src/native/pchip.cpp
    src/native/linspace.cpp
    src/native/start_ir.cpp
    src/native/resample.cpp
    src/native/spline.cpp
    src/native/realtime_fft.cpp
    src/native/partitioned_convolver.cpp
    src/io/wav_loader.cpp
    src/io/wav_writer.cpp
    src/io/calibration_loader.cpp
    src/io/layout_loader.cpp
    src/rendering/calibrate_ambisonics.cpp
    src/rendering/calibrate_vbap.cpp
    src/rendering/vbap_render.cpp
    src/rendering/ambisonics_decoder.cpp
    src/rendering/iceberg_core.cpp
    src/rendering/iceberg.cpp
    src/rendering/input_handling.cpp
    src/rendering/live.cpp
    src/rendering/trajectory.cpp
    src/experiment/experiment_loader.cpp
    src/experiment/slot_config.cpp
    src/calibration/signals.cpp
    src/calibration/bandpass.cpp
    src/calibration/spectrum.cpp
    src/calibration/measure.cpp
)
target_include_directories(iceberg_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(iceberg_core PUBLIC
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    pocketfft_hdr
)

# --- Tests -----------------------------------------------------------------
enable_testing()
add_subdirectory(tests)

# --- Optional GUI app (JUCE standalone) ------------------------------------
# Off by default so anyone consuming `iceberg_core` as a pure library
# doesn't pay the JUCE build cost. See `apps/iceberg-juce/README.md`.
option(ICEBERG_BUILD_GUI "Build the JUCE standalone GUI" OFF)
if(ICEBERG_BUILD_GUI)
    # Apply local JUCE source patches before configuring the submodule.
    # Idempotent. Mac-only: the only patch today is a macOS Tahoe +
    # AppleClang 21 fix; the .sh script also self-skips on non-Darwin
    # but Windows runners don't have bash, so we gate the
    # `execute_process` call on `APPLE` here. See
    # `patches/JUCE_PATCHES.md` for rationale.
    if(APPLE)
        execute_process(
            COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/apply_juce_patches.sh"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE _juce_patch_result
            OUTPUT_VARIABLE _juce_patch_output
            ERROR_VARIABLE  _juce_patch_output
        )
        if(NOT _juce_patch_result EQUAL 0)
            message(FATAL_ERROR
                "Failed to apply JUCE patches:\n${_juce_patch_output}\n"
                "See patches/JUCE_PATCHES.md.")
        endif()
        message(STATUS "JUCE patches: ${_juce_patch_output}")
    else()
        message(STATUS "JUCE patches skipped (non-Apple platform)")
    endif()

    add_subdirectory(third_party/JUCE)
    add_subdirectory(apps/iceberg-juce)

    # ---- CPack / Windows installer ---------------------------------------
    # NSIS-driven next-next-next installer for Windows. The install rules
    # below pull the JUCE-built `.exe` (and any sibling DLLs) from the
    # artefacts dir into the staged install tree, with the app's licence
    # and the third-party notices; CPack then wraps that tree into a
    # self-extracting NSIS installer.
    if(WIN32)
        install(DIRECTORY
            "${CMAKE_BINARY_DIR}/apps/iceberg-juce/iceberg-juce_artefacts/Release/"
            DESTINATION .
            FILES_MATCHING
                PATTERN "*.exe"
                PATTERN "*.dll"
        )
        install(FILES "${CMAKE_SOURCE_DIR}/apps/iceberg-juce/LICENSE"
                DESTINATION . RENAME LICENSE.txt)
        install(FILES "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt" DESTINATION .)
        set(CPACK_GENERATOR "NSIS")
        set(CPACK_PACKAGE_NAME "Iceberg")
        set(CPACK_PACKAGE_VENDOR "Aguirre")
        set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
        set(CPACK_PACKAGE_INSTALL_DIRECTORY "Iceberg")
        set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
            "Iceberg auralisation tool")
        set(CPACK_NSIS_DISPLAY_NAME "Iceberg")
        set(CPACK_NSIS_PACKAGE_NAME "Iceberg")
        set(CPACK_NSIS_MODIFY_PATH OFF)
        set(CPACK_PACKAGE_EXECUTABLES "Iceberg" "Iceberg")
        set(CPACK_CREATE_DESKTOP_LINKS "Iceberg")
        include(CPack)
    endif()
endif()
