include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# The MATLAB reference data (scripts/get_fixtures.sh downloads them), passed to
# test_helpers.h as a compile definition. Default: a sibling of this repository,
#   iceberg-cpp/
#   iceberg-cpp-fixtures/
# -DICEBERG_FIXTURES_DIR=/path/to/fixtures points elsewhere.
set(ICEBERG_FIXTURES_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../iceberg-cpp-fixtures"
    CACHE PATH "Read-only directory holding the MATLAB-generated fixtures")

# Without the reference data the tests that need it are skipped (GTEST_SKIP) and the
# others run. ICEBERG_REQUIRE_FIXTURES=ON makes them fail instead, for a machine that
# must run the full comparison.
option(ICEBERG_REQUIRE_FIXTURES
    "Fixture-dependent tests fail if fixtures missing (OFF = skip)" OFF)

if(NOT EXISTS "${ICEBERG_FIXTURES_DIR}")
    if(ICEBERG_REQUIRE_FIXTURES)
        message(WARNING
            "ICEBERG_FIXTURES_DIR='${ICEBERG_FIXTURES_DIR}' does not exist and "
            "ICEBERG_REQUIRE_FIXTURES is ON: the tests that compare with MATLAB will "
            "fail. scripts/get_fixtures.sh downloads the reference data.")
    else()
        message(STATUS
            "No MATLAB reference data at '${ICEBERG_FIXTURES_DIR}': the tests that "
            "compare with MATLAB will be skipped. scripts/get_fixtures.sh downloads them.")
    endif()
endif()

# Helper: register a gtest target with iceberg_core + gtest_main + fixtures.
function(iceberg_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE iceberg_core GTest::gtest_main)
    target_compile_definitions(${name} PRIVATE
        ICEBERG_FIXTURES_DIR="${ICEBERG_FIXTURES_DIR}"
        ICEBERG_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples"
        $<$<BOOL:${ICEBERG_REQUIRE_FIXTURES}>:ICEBERG_REQUIRE_FIXTURES=1>
    )
    gtest_discover_tests(${name})
endfunction()

iceberg_add_test(test_normalize_dat  test_normalize_dat.cpp)
iceberg_add_test(test_add            test_add.cpp)
iceberg_add_test(test_time_shift     test_time_shift.cpp)
iceberg_add_test(test_time_crop      test_time_crop.cpp)
iceberg_add_test(test_time_window    test_time_window.cpp)
iceberg_add_test(test_convolve       test_convolve.cpp)
iceberg_add_test(test_center_time    test_center_time.cpp)
iceberg_add_test(test_fft            test_fft.cpp)
iceberg_add_test(test_calibration_loader test_calibration_loader.cpp)
iceberg_add_test(test_pchip          test_pchip.cpp)
iceberg_add_test(test_resample       test_resample.cpp)
iceberg_add_test(test_input_handling test_input_handling.cpp)
iceberg_add_test(test_calibrate_ambisonics test_calibrate_ambisonics.cpp)
iceberg_add_test(test_calibrate_vbap test_calibrate_vbap.cpp)
iceberg_add_test(test_vbap_render    test_vbap_render.cpp)
iceberg_add_test(test_ambisonics_decoder test_ambisonics_decoder.cpp)
iceberg_add_test(test_iceberg_core    test_iceberg_core.cpp)
iceberg_add_test(test_iceberg         test_iceberg.cpp)
iceberg_add_test(test_wav_writer      test_wav_writer.cpp)
iceberg_add_test(test_wav_loader      test_wav_loader.cpp)
iceberg_add_test(test_layout_loader   test_layout_loader.cpp)
iceberg_add_test(test_experiment_loader test_experiment_loader.cpp)
iceberg_add_test(test_slot_config     test_slot_config.cpp)
iceberg_add_test(test_spline          test_spline.cpp)
iceberg_add_test(test_calibration_dsp test_calibration_dsp.cpp)
iceberg_add_test(test_calibration_measure test_calibration_measure.cpp)
iceberg_add_test(test_live            test_live.cpp)
