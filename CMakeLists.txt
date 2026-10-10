cmake_minimum_required(VERSION 3.22)

project(dng-converter
  VERSION 0.1.0
  DESCRIPTION "Converts camera raw files (Canon CR2 and others) to DNG"
  LANGUAGES CXX)

# Options. When this project is part of a bigger one (add_subdirectory or
# FetchContent), the app and the tests are off by default.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(DNGCONV_TOP_LEVEL ON)
else()
  set(DNGCONV_TOP_LEVEL OFF)
endif()
option(DNGCONV_BUILD_APP "Build the dng-convert command line tool" ${DNGCONV_TOP_LEVEL})
option(DNGCONV_BUILD_TESTS "Build the tests" ${DNGCONV_TOP_LEVEL})
option(DNGCONV_INSTALL "Generate the install target and the CMake package" ${DNGCONV_TOP_LEVEL})

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)
find_package(Threads REQUIRED)

# LibRaw decodes the raw files. Its thread-safe flavour, libraw_r, as the
# converter decodes several files at once.
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBRAW REQUIRED IMPORTED_TARGET libraw_r>=0.20)

configure_file(include/dngconv/version.hpp.in ${PROJECT_BINARY_DIR}/include/dngconv/version.hpp @ONLY)

add_library(dngconv
  src/bad_pixels.cpp
  src/camera_table.cpp
  src/converter.cpp
  src/dng_writer.cpp
  src/file_store.cpp
  src/lj92_encoder.cpp
  src/raw_reader.cpp
  src/thumbnail.cpp
  src/tiff/tiff_reader.cpp
  src/tiff/tiff_writer.cpp)
add_library(dngconv::dngconv ALIAS dngconv)

target_compile_features(dngconv PUBLIC cxx_std_17)
set_target_properties(dngconv PROPERTIES
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR})
target_include_directories(dngconv
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src)
# LibRaw is an implementation detail: no public header includes it.
target_link_libraries(dngconv PRIVATE PkgConfig::LIBRAW Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(dngconv PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(DNGCONV_BUILD_APP)
  add_subdirectory(apps/dng-convert)
endif()

if(DNGCONV_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(DNGCONV_INSTALL)
  include(CMakePackageConfigHelpers)
  install(TARGETS dngconv EXPORT dngconvTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(DIRECTORY include/dngconv DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.hpp")
  install(FILES ${PROJECT_BINARY_DIR}/include/dngconv/version.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dngconv)
  install(EXPORT dngconvTargets NAMESPACE dngconv:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dngconv)
  configure_package_config_file(cmake/dngconvConfig.cmake.in ${PROJECT_BINARY_DIR}/dngconvConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dngconv)
  write_basic_package_version_file(${PROJECT_BINARY_DIR}/dngconvConfigVersion.cmake
    COMPATIBILITY SameMinorVersion)
  install(FILES ${PROJECT_BINARY_DIR}/dngconvConfig.cmake ${PROJECT_BINARY_DIR}/dngconvConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dngconv)
endif()
