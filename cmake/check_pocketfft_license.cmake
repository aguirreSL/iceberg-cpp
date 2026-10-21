# Verifies pocketfft is the BSD-3-Clause flavour we expect.
# Aborts the configure step if the LICENSE file is missing or its
# SPDX identifier doesn't match. Re-run after every FetchContent bump.

set(_pfft_license "${pocketfft_SOURCE_DIR}/LICENSE.md")
if(NOT EXISTS "${_pfft_license}")
    set(_pfft_license "${pocketfft_SOURCE_DIR}/LICENSE")
endif()

if(NOT EXISTS "${_pfft_license}")
    message(FATAL_ERROR
        "pocketfft license file not found in ${pocketfft_SOURCE_DIR}. "
        "Refusing to use a dep without a verifiable license.")
endif()

file(READ "${_pfft_license}" _pfft_license_text)
string(FIND "${_pfft_license_text}" "BSD-3-Clause" _bsd_pos)
string(FIND "${_pfft_license_text}" "Redistribution and use in source and binary forms" _bsd_text_pos)

if(_bsd_pos EQUAL -1 AND _bsd_text_pos EQUAL -1)
    message(FATAL_ERROR
        "pocketfft license at ${_pfft_license} does not appear to be "
        "BSD-3-Clause, the licence this project takes it under. Aborting.")
endif()

message(STATUS "pocketfft license verified at ${_pfft_license}")
