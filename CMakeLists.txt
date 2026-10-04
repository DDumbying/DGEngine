cmake_minimum_required(VERSION 3.16)
project(slime VERSION 0.6.0 LANGUAGES C)

# Tests, benchmark and build type defaults only apply when slime is the project being built, not when a game
# pulls it in with add_subdirectory.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(SLIME_TOP_LEVEL ON)
else()
    set(SLIME_TOP_LEVEL OFF)
endif()

option(SLIME_BUILD_TESTS "Build tests and benchmark" ${SLIME_TOP_LEVEL})
option(SLIME_BUILD_DEMO "Build the raylib demo" OFF)
option(SLIME_PROFILE "Time each solver phase, for slime_bench --phases" OFF)
option(SLIME_INSTALL "Generate install rules" ${SLIME_TOP_LEVEL})

if(SLIME_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# The header's version must match the project's, since sl_version() reports the header's.
file(STRINGS include/slime/slime.h header_version REGEX "^#define SLIME_VERSION_(MAJOR|MINOR|PATCH) ")
string(REGEX REPLACE ".*MAJOR ([0-9]+).*MINOR ([0-9]+).*PATCH ([0-9]+).*" "\\1.\\2.\\3" header_version "${header_version}")
if(NOT header_version VERSION_EQUAL PROJECT_VERSION)
    message(FATAL_ERROR "slime.h says ${header_version} but the project is ${PROJECT_VERSION}")
endif()

# STATIC or SHARED follows BUILD_SHARED_LIBS.
add_library(slime src/world.c src/grid.c src/solve.c src/collide.c src/objects.c src/threads.c src/extras.c src/snapshot.c)
add_library(slime::slime ALIAS slime)
find_package(Threads REQUIRED)
target_link_libraries(slime PRIVATE Threads::Threads)
target_include_directories(slime PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
if(SLIME_PROFILE)
    target_compile_definitions(slime PUBLIC SLIME_PROFILE)
endif()
get_target_property(slime_type slime TYPE)
if(slime_type STREQUAL "SHARED_LIBRARY")
    target_compile_definitions(slime PUBLIC SLIME_SHARED PRIVATE SLIME_BUILDING)
endif()
set_target_properties(slime PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON
    C_VISIBILITY_PRESET hidden
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})
if(MSVC)
    target_compile_options(slime PRIVATE /W4)
else()
    target_compile_options(slime PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(slime PUBLIC m)
endif()

if(SLIME_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
    install(TARGETS slime EXPORT slimeTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES include/slime/slime.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/slime)
    install(EXPORT slimeTargets NAMESPACE slime:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/slime)
    configure_package_config_file(cmake/slimeConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/slimeConfig.cmake
                                  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/slime)
    # Before 1.0 every minor version may change behavior, so only the same minor version is compatible.
    write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/slimeConfigVersion.cmake COMPATIBILITY SameMinorVersion)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/slimeConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/slimeConfigVersion.cmake
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/slime)
    configure_file(cmake/slime.pc.in ${CMAKE_CURRENT_BINARY_DIR}/slime.pc @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/slime.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()

if(SLIME_BUILD_TESTS)
    enable_testing()
    add_executable(slime_tests tests/test_main.c)
    target_link_libraries(slime_tests PRIVATE slime)
    add_test(NAME slime_tests COMMAND slime_tests)
    if(CMAKE_NM AND NOT MSVC AND NOT EMSCRIPTEN)
        add_test(NAME slime_symbols COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DLIB=$<TARGET_FILE:slime>
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/symbols.cmake)
    endif()
    add_executable(slime_bench tests/bench.c)
    target_link_libraries(slime_bench PRIVATE slime)

    # The README example is compiled and run as a test, so it cannot go stale.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS README.md)
    file(READ README.md readme)
    string(FIND "${readme}" "```c\n" example_start)
    if(example_start EQUAL -1)
        message(FATAL_ERROR "README.md has no C example")
    endif()
    math(EXPR example_start "${example_start} + 5")
    string(SUBSTRING "${readme}" ${example_start} -1 example)
    string(FIND "${example}" "```" example_end)
    string(SUBSTRING "${example}" 0 ${example_end} example)
    set(example_file ${CMAKE_CURRENT_BINARY_DIR}/readme_example.c)
    set(old_example "")
    if(EXISTS ${example_file})
        file(READ ${example_file} old_example)
    endif()
    if(NOT old_example STREQUAL example)
        file(WRITE ${example_file} "${example}")
    endif()
    add_executable(slime_readme_example ${example_file})
    target_link_libraries(slime_readme_example PRIVATE slime)
    add_test(NAME slime_readme_example COMMAND slime_readme_example)
endif()

if(SLIME_BUILD_DEMO)
    find_package(raylib 5.5 QUIET)
    if(NOT raylib_FOUND)
        include(FetchContent)
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(raylib
            GIT_REPOSITORY https://github.com/raysan5/raylib.git
            GIT_TAG 5.5
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(raylib)
    endif()
    add_executable(slime_demo demo/demo.c)
    target_link_libraries(slime_demo PRIVATE slime raylib)
    target_compile_definitions(slime_demo PRIVATE SLIME_DEMO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/demo")
endif()
