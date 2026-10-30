# ZENITH LANCER -- a vertical scrolling shooter with a campaign (a workshop between the missions, front and rear guns
# with eleven power levels, sidekicks, generator and shield, log crystals, secret missions) and an arcade mode.
# C++20 on raylib (fetched at configure time), like Sunstreak and Dark Concordance.
cmake_minimum_required( VERSION 3.22 )

# raylib 5.5 and its GLFW still ask for old CMake versions; CMake 4 refuses those without this floor.
set( CMAKE_POLICY_VERSION_MINIMUM 3.5 )
set( CMAKE_POLICY_DEFAULT_CMP0091 NEW )
set( CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" )

project( ZenithLancer VERSION 1.0.0 LANGUAGES C CXX )

set( CMAKE_CXX_STANDARD 20 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_CXX_EXTENSIONS OFF )

set( ZENITH_BIN_DIR "" CACHE PATH "where the game is copied after each build (bin/<preset>)" )

include( FetchContent )
set( ZENITH_EXTRACT_TIMESTAMP "" )
if( CMAKE_VERSION VERSION_GREATER_EQUAL 3.24 )
    set( ZENITH_EXTRACT_TIMESTAMP DOWNLOAD_EXTRACT_TIMESTAMP TRUE )
endif()
set( BUILD_EXAMPLES OFF CACHE BOOL "" FORCE )
set( BUILD_GAMES OFF CACHE BOOL "" FORCE )
set( BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE )
FetchContent_Declare( raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
    ${ZENITH_EXTRACT_TIMESTAMP} )
FetchContent_MakeAvailable( raylib )

file( GLOB ZENITH_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp ${CMAKE_SOURCE_DIR}/src/*.h )
add_executable( ZenithLancer WIN32 ${ZENITH_SOURCES} )
target_link_libraries( ZenithLancer PRIVATE raylib )
# The data (models, enemies, weapons, missions, texts) and the assets are read from beside the program, else from
# the source tree; --data DIR points elsewhere.
target_compile_definitions( ZenithLancer PRIVATE ZENITH_ROOT="${CMAKE_SOURCE_DIR}"
                                                 ZENITH_VERSION="${PROJECT_VERSION}" )
if( MSVC )
    if( EXISTS ${CMAKE_SOURCE_DIR}/res/zenithlancer.rc )
        target_sources( ZenithLancer PRIVATE ${CMAKE_SOURCE_DIR}/res/zenithlancer.rc )
    endif()
    target_compile_options( ZenithLancer PRIVATE /W4 /permissive- /utf-8 /wd4244 /wd4267 /wd4305 /wd4100 )
    target_compile_definitions( ZenithLancer PRIVATE _CRT_SECURE_NO_WARNINGS )
    # A window program without a console that still starts in main().
    target_link_options( ZenithLancer PRIVATE /ENTRY:mainCRTStartup )
else()
    # -fwrapv: an int that overflows wraps round as with MSVC, so a seed plays the same everywhere.
    target_compile_options( ZenithLancer PRIVATE -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter
                                                 -fwrapv )
    target_link_options( ZenithLancer PRIVATE -static-libstdc++ -static-libgcc )
endif()

if( ZENITH_BIN_DIR )
    add_custom_command( TARGET ZenithLancer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ZENITH_BIN_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ZenithLancer> "${ZENITH_BIN_DIR}" )
endif()

enable_testing()
# Every data file parses without an error and every mission can be flown to its end by the autopilot.
add_test( NAME data COMMAND ZenithLancer --validate )
add_test( NAME missions COMMAND ZenithLancer --simall --difficulty 2 )
# The screens played through by scripts (a hidden window; the saves go to a test folder in the build).
foreach( flow campaign arcade boss )
    add_test( NAME app_${flow} COMMAND ZenithLancer --apptest ${CMAKE_SOURCE_DIR}/tools/apptest_${flow}.txt )
    set_tests_properties( app_${flow} PROPERTIES ENVIRONMENT "ZENITH_HOME=${CMAKE_BINARY_DIR}/testhome_${flow}" )
endforeach()
