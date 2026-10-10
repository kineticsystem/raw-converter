# The command line parsing is a library of its own, so that the tests can
# check it without running the program.
add_library(dng-convert-cli STATIC cli_options.cpp)
target_include_directories(dng-convert-cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dng-convert-cli PUBLIC dngconv::dngconv)

add_executable(dng-convert main.cpp)
target_link_libraries(dng-convert PRIVATE dng-convert-cli)

if(DNGCONV_INSTALL)
  install(TARGETS dng-convert RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
