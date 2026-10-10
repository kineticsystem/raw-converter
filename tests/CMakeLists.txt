find_package(GTest REQUIRED)
include(GoogleTest)

# Folders of raw files for the end-to-end tests, colon separated: the
# samples of bin/fetch-samples.sh by default.
set(DNGCONV_SAMPLE_DIRS "${PROJECT_SOURCE_DIR}/samples" CACHE STRING
    "Folders of raw files the end-to-end tests convert, colon separated")

add_executable(dngconv_tests
  test_adobe_reference.cpp
  test_bad_pixels.cpp
  test_cli_options.cpp
  test_converter.cpp
  test_dng_writer.cpp
  test_samples.cpp)
# The tests read the DNG files back with LibRaw, and use internal headers.
target_include_directories(dngconv_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(dngconv_tests PRIVATE dngconv::dngconv dng-convert-cli PkgConfig::LIBRAW GTest::gtest_main)
# Built in rather than passed in the environment: gtest_discover_tests lists
# the tests by running the binary at build time, and a sample folder that is
# not known then has no tests. The DNGCONV_SAMPLE_DIRS environment variable
# still overrides it.
target_compile_definitions(dngconv_tests PRIVATE DNGCONV_SAMPLE_DIRS="${DNGCONV_SAMPLE_DIRS}"
                                                 DNGCONV_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

gtest_discover_tests(dngconv_tests DISCOVERY_TIMEOUT 60)

# The command line tool on the samples, its files checked by exiftool's
# validator, when exiftool is installed.
find_program(EXIFTOOL exiftool)
if(EXIFTOOL AND TARGET dng-convert)
  add_test(NAME dng_convert_exiftool_validate
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/validate_with_exiftool.sh $<TARGET_FILE:dng-convert> ${EXIFTOOL}
            ${CMAKE_CURRENT_BINARY_DIR}/validate "${DNGCONV_SAMPLE_DIRS}")
endif()
