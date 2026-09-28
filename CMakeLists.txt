cmake_minimum_required(VERSION 3.25)

# Builds the same artifacts as the makefile: the scripting-addition payload and
# loader, embedded into the yabai binary. The makefile remains the original
# build; this file adds out-of-tree builds and a compile_commands.json that
# language servers can use despite the unity build in src/manifest.m.

# Must precede project() so the compiler checks already use it.
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Minimum macOS version")

project(yabai LANGUAGES C OBJC)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(YABAI_ARCHITECTURES "x86_64;arm64" CACHE STRING "Architectures of the yabai binary")
set(YABAI_OSAX_ARCHITECTURES "x86_64;arm64e" CACHE STRING "Architectures of the scripting-addition payload and loader")
set(YABAI_SANITIZE "" CACHE STRING "Sanitizers for the yabai binary: empty, address or thread")
set(YABAI_CODESIGN_IDENTITY "" CACHE STRING "Code signing identity applied to the yabai binary after linking")
option(YABAI_LINK_COMPILE_COMMANDS "Point compile_commands.json in the source tree at this build directory" ON)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(OSAX_OUT ${CMAKE_BINARY_DIR}/src/osax)
set(PRIVATE_FRAMEWORKS /System/Library/PrivateFrameworks)

# Scripting-addition payload, injected into Dock.app. Always optimised, like the makefile.
add_library(payload SHARED ${SRC}/osax/payload.m)
set_target_properties(payload PROPERTIES
    PREFIX "" SUFFIX "" OUTPUT_NAME payload
    LIBRARY_OUTPUT_DIRECTORY ${OSAX_OUT}
    OSX_ARCHITECTURES "${YABAI_OSAX_ARCHITECTURES}")
target_compile_options(payload PRIVATE -O3)
target_link_options(payload PRIVATE -F${PRIVATE_FRAMEWORKS})
target_link_libraries(payload PRIVATE "-framework SkyLight" "-framework AppKit" "-framework QuartzCore" "-framework Carbon")

add_executable(loader ${SRC}/osax/loader.m)
set_target_properties(loader PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${OSAX_OUT}
    OSX_ARCHITECTURES "${YABAI_OSAX_ARCHITECTURES}")
target_compile_options(loader PRIVATE -O3)
target_link_libraries(loader PRIVATE "-framework Cocoa")

# Embed both as C arrays. xxd derives the symbol names from the path, so it runs
# from the build directory with the same relative paths as the makefile.
foreach(name payload loader)
    add_custom_command(
        OUTPUT ${OSAX_OUT}/${name}_bin.c
        COMMAND xxd -i -a ./src/osax/${name} ./src/osax/${name}_bin.c
        DEPENDS ${name}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        VERBATIM)
endforeach()

add_executable(yabai ${SRC}/manifest.m ${OSAX_OUT}/payload_bin.c ${OSAX_OUT}/loader_bin.c)
set_target_properties(yabai PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    OSX_ARCHITECTURES "${YABAI_ARCHITECTURES}"
    C_STANDARD 11 C_EXTENSIONS OFF
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/Info.plist)
target_compile_options(yabai PRIVATE
    -std=c11 -Wall -Wextra -fvisibility=hidden -fno-objc-arc
    $<$<CONFIG:Debug>:-O0>)
target_link_options(yabai PRIVATE
    -F${PRIVATE_FRAMEWORKS}
    LINKER:-sectcreate,__TEXT,__info_plist,${CMAKE_CURRENT_SOURCE_DIR}/assets/Info.plist)
target_link_libraries(yabai PRIVATE
    "-framework Carbon" "-framework Cocoa" "-framework CoreServices" "-framework CoreVideo" "-framework SkyLight"
    "-weak_framework ScreenCaptureKit" "-framework QuartzCore")

if(YABAI_SANITIZE STREQUAL "address")
    set(YABAI_SANITIZE_FLAGS -fsanitize=address,undefined)
elseif(YABAI_SANITIZE STREQUAL "thread")
    set(YABAI_SANITIZE_FLAGS -fsanitize=thread,undefined)
elseif(NOT YABAI_SANITIZE STREQUAL "")
    message(FATAL_ERROR "YABAI_SANITIZE must be empty, address or thread")
endif()
target_compile_options(yabai PRIVATE ${YABAI_SANITIZE_FLAGS})
target_link_options(yabai PRIVATE ${YABAI_SANITIZE_FLAGS})

if(YABAI_CODESIGN_IDENTITY)
    add_custom_command(TARGET yabai POST_BUILD
        COMMAND codesign -fs ${YABAI_CODESIGN_IDENTITY} $<TARGET_FILE:yabai>
        VERBATIM)
endif()

# Unit tests of the core. tests.m includes the whole unity build with -DTESTS
# and stubs the embedded payload and loader.
enable_testing()
add_executable(yabai_tests tests/src/tests.m)
set_target_properties(yabai_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    C_STANDARD 11 C_EXTENSIONS OFF)
target_compile_definitions(yabai_tests PRIVATE TESTS PROFILE=1)
target_compile_options(yabai_tests PRIVATE -std=c11 -fno-objc-arc ${YABAI_SANITIZE_FLAGS})
target_link_options(yabai_tests PRIVATE -F${PRIVATE_FRAMEWORKS} ${YABAI_SANITIZE_FLAGS})
target_link_libraries(yabai_tests PRIVATE
    "-framework Carbon" "-framework Cocoa" "-framework CoreServices" "-framework CoreVideo" "-framework SkyLight"
    "-weak_framework ScreenCaptureKit" "-framework QuartzCore")
add_test(NAME yabai_tests COMMAND yabai_tests)

# Exercise the actual navigation implementation with isolated OS dependencies.
add_executable(navigation_tests tests/navigation/navigation.c)
target_compile_options(navigation_tests PRIVATE -UNDEBUG ${YABAI_SANITIZE_FLAGS})
target_link_options(navigation_tests PRIVATE ${YABAI_SANITIZE_FLAGS})
add_test(NAME navigation_tests COMMAND navigation_tests)

add_executable(navigation_snapshot_tests tests/navigation/snapshot.m)
target_compile_options(navigation_snapshot_tests PRIVATE -UNDEBUG -fno-objc-arc ${YABAI_SANITIZE_FLAGS})
target_link_options(navigation_snapshot_tests PRIVATE ${YABAI_SANITIZE_FLAGS})
target_link_libraries(navigation_snapshot_tests PRIVATE "-framework Cocoa" "-framework QuartzCore" "-weak_framework ScreenCaptureKit")
add_test(NAME navigation_snapshot_tests COMMAND navigation_snapshot_tests)

add_executable(navigation_schedule_tests tests/navigation/schedule.c)
target_compile_options(navigation_schedule_tests PRIVATE -UNDEBUG ${YABAI_SANITIZE_FLAGS})
target_link_options(navigation_schedule_tests PRIVATE ${YABAI_SANITIZE_FLAGS})
add_test(NAME navigation_schedule_tests COMMAND navigation_schedule_tests)

add_executable(navigation_queue_tests tests/navigation/queue.c)
target_compile_options(navigation_queue_tests PRIVATE -UNDEBUG ${YABAI_SANITIZE_FLAGS})
target_link_options(navigation_queue_tests PRIVATE ${YABAI_SANITIZE_FLAGS})
add_test(NAME navigation_queue_tests COMMAND navigation_queue_tests)

add_executable(navigation_spaces_tests tests/navigation/spaces.c)
target_compile_options(navigation_spaces_tests PRIVATE -UNDEBUG ${YABAI_SANITIZE_FLAGS})
target_link_options(navigation_spaces_tests PRIVATE ${YABAI_SANITIZE_FLAGS})
target_link_libraries(navigation_spaces_tests PRIVATE "-framework CoreFoundation")
add_test(NAME navigation_spaces_tests COMMAND navigation_spaces_tests)

add_executable(navigation_focus_tests tests/navigation/focus.c)
target_compile_options(navigation_focus_tests PRIVATE -UNDEBUG ${YABAI_SANITIZE_FLAGS})
target_link_options(navigation_focus_tests PRIVATE ${YABAI_SANITIZE_FLAGS})
add_test(NAME navigation_focus_tests COMMAND navigation_focus_tests)

add_executable(focus_tests tests/focus/events.c)
target_compile_options(focus_tests PRIVATE -UNDEBUG ${YABAI_SANITIZE_FLAGS})
target_link_options(focus_tests PRIVATE ${YABAI_SANITIZE_FLAGS})
add_test(NAME focus_tests COMMAND focus_tests)

add_executable(fade_tests tests/fade/timing.c)
target_compile_options(fade_tests PRIVATE -UNDEBUG ${YABAI_SANITIZE_FLAGS})
target_link_options(fade_tests PRIVATE ${YABAI_SANITIZE_FLAGS})
target_link_libraries(fade_tests PRIVATE "-framework CoreFoundation" "-framework CoreGraphics")
add_test(NAME fade_tests COMMAND fade_tests)
set_tests_properties(fade_tests PROPERTIES TIMEOUT 15)

# libFuzzer targets for the two socket protocols, see docs/testing.md. They need
# a clang that ships libFuzzer, which Apple's does not. Each also runs as a
# short CTest test, writing new inputs to the build directory.
option(YABAI_FUZZ "Build the libFuzzer targets and run them as tests" OFF)
set(YABAI_FUZZ_SECONDS 30 CACHE STRING "Seconds each fuzz target runs as a test")

if(YABAI_FUZZ)
    set(YABAI_FUZZ_FLAGS -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined)

    add_executable(fuzz_daemon_message tests/fuzz/daemon_message.m)
    target_compile_definitions(fuzz_daemon_message PRIVATE TESTS)
    target_link_libraries(fuzz_daemon_message PRIVATE
        "-framework Carbon" "-framework Cocoa" "-framework CoreServices" "-framework CoreVideo" "-framework SkyLight"
        "-weak_framework ScreenCaptureKit" "-framework QuartzCore")

    add_executable(fuzz_payload_message tests/fuzz/payload_message.m)
    target_include_directories(fuzz_payload_message PRIVATE ${SRC}/osax)
    target_link_libraries(fuzz_payload_message PRIVATE
        "-framework AppKit" "-framework QuartzCore" "-framework Carbon" "-framework CoreGraphics")

    foreach(target daemon_message payload_message)
        set_target_properties(fuzz_${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/fuzz
            C_STANDARD 11 C_EXTENSIONS OFF)
        target_compile_options(fuzz_${target} PRIVATE -std=c11 -fno-objc-arc -g -Wno-deprecated-declarations ${YABAI_FUZZ_FLAGS})
        target_link_options(fuzz_${target} PRIVATE -F${PRIVATE_FRAMEWORKS} ${YABAI_FUZZ_FLAGS})

        set(corpus ${CMAKE_BINARY_DIR}/fuzz/corpus/${target})
        file(MAKE_DIRECTORY ${corpus})
        add_test(NAME fuzz_${target}
            COMMAND fuzz_${target} -max_total_time=${YABAI_FUZZ_SECONDS} -timeout=10 -print_final_stats=1
                -artifact_prefix=${CMAKE_BINARY_DIR}/fuzz/
                ${corpus} ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/${target})
    endforeach()
endif()

# Tools that keep the scripting addition working across macOS releases, see
# docs/osax.md. They inspect the running system, so they build for the host.
option(YABAI_BUILD_TOOLS "Build the scripting-addition tools and their test" ON)

if(YABAI_BUILD_TOOLS AND CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
    add_executable(pattern_check tools/osax/pattern_check.m)
    target_include_directories(pattern_check PRIVATE ${SRC}/osax)
    target_link_libraries(pattern_check PRIVATE "-framework Foundation")

    add_executable(sa_client tools/osax/sa_client.c)
    target_include_directories(sa_client PRIVATE ${SRC}/osax)
    target_link_libraries(sa_client PRIVATE "-framework CoreFoundation" "-framework SkyLight")

    add_executable(window_sub_level tools/skylight/window_sub_level.c)
    target_link_libraries(window_sub_level PRIVATE "-framework CoreFoundation" "-framework CoreGraphics" "-framework SkyLight")

    foreach(tool pattern_check sa_client window_sub_level)
        set_target_properties(${tool} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
            OSX_ARCHITECTURES arm64)
        target_compile_options(${tool} PRIVATE -Wall -Wextra -fno-objc-arc)
        target_link_options(${tool} PRIVATE -F${PRIVATE_FRAMEWORKS})
    endforeach()

    # Resolves every payload lookup in the Dock of the machine running the test.
    add_test(NAME osax_patterns COMMAND pattern_check)
endif()

# Language server support. Each source included by a unity translation unit is
# parsed with everything its unity file includes before it, which is exactly
# the context the compiler sees. The target is never built.
function(yabai_unity_sources unity_file target)
    # A renamed or removed source would otherwise stay in the build graph,
    # and Ninja stops at the missing file until CMake runs again.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${unity_file})
    file(STRINGS ${unity_file} lines)
    get_filename_component(unity_dir ${unity_file} DIRECTORY)
    set(prelude "")
    # A source included inside a conditional, such as one payload per
    # architecture, would leave its prelude unterminated; the prelude closes
    # the conditionals still open.
    set(depth 0)
    foreach(line IN LISTS lines)
        if(line MATCHES "^[ \t]*#[ \t]*if")
            math(EXPR depth "${depth} + 1")
        elseif(line MATCHES "^[ \t]*#[ \t]*endif")
            math(EXPR depth "${depth} - 1")
        endif()

        if(line MATCHES "^#include \"([^\"]+\\.[cm])\"$")
            set(source ${unity_dir}/${CMAKE_MATCH_1})
            get_filename_component(source ${source} ABSOLUTE)
            file(RELATIVE_PATH id ${SRC} ${source})
            string(MAKE_C_IDENTIFIER ${id} id)
            set(header ${CMAKE_BINARY_DIR}/unity/${id}.h)
            string(REPEAT "#endif\n" ${depth} closing)
            file(CONFIGURE OUTPUT ${header} CONTENT "${prelude}${closing}" @ONLY)
            target_sources(${target} PRIVATE ${source})
            set_source_files_properties(${source} PROPERTIES
                LANGUAGE OBJC
                COMPILE_OPTIONS "-include;${header}")
            string(APPEND prelude "#include \"${source}\"\n")
        else()
            string(REGEX REPLACE "^#include \"([^\"]+)\"" "#include \"${unity_dir}/\\1\"" line "${line}")
            string(APPEND prelude "${line}\n")
        endif()
    endforeach()
endfunction()

# Editors that only look for compile_commands.json in the workspace root follow
# the most recently configured build directory.
if(YABAI_LINK_COMPILE_COMMANDS AND NOT CMAKE_BINARY_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    file(CREATE_LINK ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json SYMBOLIC)
endif()

# A source compiled alone sees the static prototypes of functions defined in
# later sources, which the unity build defines.
add_library(yabai_unity OBJECT EXCLUDE_FROM_ALL)
target_include_directories(yabai_unity PRIVATE ${SRC} ${OSAX_OUT})
target_compile_options(yabai_unity PRIVATE -std=c11 -fno-objc-arc -Wno-undefined-internal)
yabai_unity_sources(${SRC}/manifest.m yabai_unity)
yabai_unity_sources(${SRC}/osax/payload.m yabai_unity)
set_property(SOURCE ${SRC}/osax/x64_payload.m APPEND PROPERTY COMPILE_OPTIONS -arch x86_64)
