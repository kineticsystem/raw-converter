# The command line parsing is a library of its own, so that the tests can
# check it without running the program.
add_library(raw-convert-cli STATIC cli_options.cpp)
target_include_directories(raw-convert-cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(raw-convert-cli PUBLIC rawconv::rawconv)

add_executable(raw-convert main.cpp)
target_link_libraries(raw-convert PRIVATE raw-convert-cli)

if(RAWCONV_INSTALL)
  install(TARGETS raw-convert RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
