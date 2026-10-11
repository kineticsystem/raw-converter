cmake_minimum_required(VERSION 3.22)

project(raw-converter
  VERSION 0.1.0
  DESCRIPTION "Converts camera raw files (Canon CR2 and others) to DNG"
  LANGUAGES CXX)

# Options. When this project is part of a bigger one (add_subdirectory or
# FetchContent), the app and the tests are off by default.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(RAWCONV_TOP_LEVEL ON)
else()
  set(RAWCONV_TOP_LEVEL OFF)
endif()
option(RAWCONV_BUILD_APP "Build the raw-convert command line tool" ${RAWCONV_TOP_LEVEL})
option(RAWCONV_BUILD_TESTS "Build the tests" ${RAWCONV_TOP_LEVEL})
option(RAWCONV_INSTALL "Generate the install target and the CMake package" ${RAWCONV_TOP_LEVEL})

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)
find_package(Threads REQUIRED)

# LibRaw decodes the raw files. Its thread-safe flavour, libraw_r, as the
# converter decodes several files at once.
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBRAW REQUIRED IMPORTED_TARGET libraw_r>=0.20)
# Little CMS makes the ICC profiles the TIFF files embed.
pkg_check_modules(LCMS2 REQUIRED IMPORTED_TARGET lcms2)

configure_file(include/rawconv/version.hpp.in ${PROJECT_BINARY_DIR}/include/rawconv/version.hpp @ONLY)

add_library(rawconv
  src/bad_pixels.cpp
  src/camera_table.cpp
  src/converter.cpp
  src/dng_writer.cpp
  src/exif.cpp
  src/file_store.cpp
  src/lj92_encoder.cpp
  src/raw_reader.cpp
  src/renderer.cpp
  src/thumbnail.cpp
  src/tiff/tiff_reader.cpp
  src/tiff/tiff_writer.cpp
  src/tiff_output.cpp)
add_library(rawconv::rawconv ALIAS rawconv)

target_compile_features(rawconv PUBLIC cxx_std_17)
set_target_properties(rawconv PROPERTIES
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR})
target_include_directories(rawconv
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src)
# LibRaw and Little CMS are implementation details: no public header includes
# them.
target_link_libraries(rawconv PRIVATE PkgConfig::LIBRAW PkgConfig::LCMS2 Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rawconv PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(RAWCONV_BUILD_APP)
  add_subdirectory(apps/raw-convert)
endif()

if(RAWCONV_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(RAWCONV_INSTALL)
  include(CMakePackageConfigHelpers)
  install(TARGETS rawconv EXPORT rawconvTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(DIRECTORY include/rawconv DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.hpp")
  install(FILES ${PROJECT_BINARY_DIR}/include/rawconv/version.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rawconv)
  install(EXPORT rawconvTargets NAMESPACE rawconv:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rawconv)
  configure_package_config_file(cmake/rawconvConfig.cmake.in ${PROJECT_BINARY_DIR}/rawconvConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rawconv)
  write_basic_package_version_file(${PROJECT_BINARY_DIR}/rawconvConfigVersion.cmake
    COMPATIBILITY SameMinorVersion)
  install(FILES ${PROJECT_BINARY_DIR}/rawconvConfig.cmake ${PROJECT_BINARY_DIR}/rawconvConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rawconv)
endif()
