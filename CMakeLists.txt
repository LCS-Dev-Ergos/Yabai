cmake_minimum_required(VERSION 3.25)

# Builds the same artifacts as the makefile: the scripting-addition payload and
# loader, embedded into the yabai binary. The makefile remains the original
# build; this file adds out-of-tree builds and a compile_commands.json that
# language servers can use for the daemon's area translation units.

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
option(YABAI_ALLOW_UNSIGNED_LOCAL "Allow same-UID unsigned socket clients in an explicit local development build" OFF)
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
target_compile_definitions(payload PRIVATE YABAI_ALLOW_UNSIGNED_LOCAL=$<BOOL:${YABAI_ALLOW_UNSIGNED_LOCAL}>)
target_link_libraries(payload PRIVATE "-framework SkyLight" "-framework AppKit" "-framework QuartzCore" "-framework Carbon" "-framework Security" "-lbsm.0")

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

add_executable(yabai
    ${SRC}/yabai_main.m
    ${SRC}/navigation/navigation_effects.m
    ${SRC}/sa/sa_unity.m
    ${SRC}/displays/displays.m
    ${SRC}/applications/applications.m
    ${SRC}/spaces/layout_unity.m
    ${SRC}/events/events.m
    ${SRC}/ipc/ipc.m
    ${OSAX_OUT}/payload_bin.c
    ${OSAX_OUT}/loader_bin.c)
set_target_properties(yabai PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    OSX_ARCHITECTURES "${YABAI_ARCHITECTURES}"
    C_STANDARD 11 C_EXTENSIONS OFF
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/Info.plist)
target_compile_options(yabai PRIVATE
    -std=c11 -Wall -Wextra -fvisibility=hidden -fno-objc-arc
    $<$<CONFIG:Debug>:-O0>)
target_compile_definitions(yabai PRIVATE YABAI_ALLOW_UNSIGNED_LOCAL=$<BOOL:${YABAI_ALLOW_UNSIGNED_LOCAL}>)
target_link_options(yabai PRIVATE
    -F${PRIVATE_FRAMEWORKS}
    LINKER:-sectcreate,__TEXT,__info_plist,${CMAKE_CURRENT_SOURCE_DIR}/assets/Info.plist)
target_link_libraries(yabai PRIVATE
    "-framework Carbon" "-framework Cocoa" "-framework CoreServices" "-framework CoreVideo" "-framework SkyLight"
    "-weak_framework ScreenCaptureKit" "-framework QuartzCore" "-framework Security" "-lbsm.0")

if(YABAI_SANITIZE STREQUAL "address")
    set(YABAI_SANITIZE_FLAGS -fsanitize=address,undefined)
elseif(YABAI_SANITIZE STREQUAL "thread")
    set(YABAI_SANITIZE_FLAGS -fsanitize=thread,undefined)
elseif(NOT YABAI_SANITIZE STREQUAL "")
    message(FATAL_ERROR "YABAI_SANITIZE must be empty, address or thread")
endif()
target_compile_options(yabai PRIVATE ${YABAI_SANITIZE_FLAGS})
target_link_options(yabai PRIVATE ${YABAI_SANITIZE_FLAGS})

# The client on its own, see src/client/yabai_msg.c. It embeds yabai's
# Info.plist, so it signs with yabai's identifier and the daemon accepts it.
add_executable(yabai-msg ${SRC}/client/yabai_msg.c)
set_target_properties(yabai-msg PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    OSX_ARCHITECTURES "${YABAI_ARCHITECTURES}"
    C_STANDARD 11 C_EXTENSIONS OFF
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/Info.plist)
target_compile_options(yabai-msg PRIVATE
    -std=c11 -Wall -Wextra -fvisibility=hidden ${YABAI_SANITIZE_FLAGS}
    $<$<CONFIG:Debug>:-O0>)
target_link_options(yabai-msg PRIVATE
    LINKER:-sectcreate,__TEXT,__info_plist,${CMAKE_CURRENT_SOURCE_DIR}/assets/Info.plist
    ${YABAI_SANITIZE_FLAGS})

if(YABAI_CODESIGN_IDENTITY)
    foreach(target yabai yabai-msg)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND codesign -fs ${YABAI_CODESIGN_IDENTITY} $<TARGET_FILE:${target}>
            VERBATIM)
    endforeach()
endif()

# Unit tests of the core. tests.m includes the full manifest with -DTESTS
# and stubs the embedded payload and loader.
enable_testing()
add_executable(yabai_tests tests/src/tests.m)
set_target_properties(yabai_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    C_STANDARD 11 C_EXTENSIONS OFF)
target_compile_definitions(yabai_tests PRIVATE TESTS PROFILE=1)
target_compile_options(yabai_tests PRIVATE -std=c11 -fno-objc-arc ${YABAI_SANITIZE_FLAGS})
target_compile_definitions(yabai_tests PRIVATE YABAI_ALLOW_UNSIGNED_LOCAL=1)
target_link_options(yabai_tests PRIVATE -F${PRIVATE_FRAMEWORKS} ${YABAI_SANITIZE_FLAGS})
target_link_libraries(yabai_tests PRIVATE
    "-framework Carbon" "-framework Cocoa" "-framework CoreServices" "-framework CoreVideo" "-framework SkyLight"
    "-weak_framework ScreenCaptureKit" "-framework QuartzCore" "-framework Security" "-lbsm.0")
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

add_executable(navigation_captured_schedule_tests tests/navigation/captured_schedule.c)
target_compile_options(navigation_captured_schedule_tests PRIVATE -UNDEBUG ${YABAI_SANITIZE_FLAGS})
target_link_options(navigation_captured_schedule_tests PRIVATE ${YABAI_SANITIZE_FLAGS})
add_test(NAME navigation_captured_schedule_tests COMMAND navigation_captured_schedule_tests)

# Real ingress, command parser and schedule with deterministic host services.
add_executable(navigation_ingress_tests tests/navigation/ingress.m)
target_compile_options(navigation_ingress_tests PRIVATE -UNDEBUG -fno-objc-arc ${YABAI_SANITIZE_FLAGS})
target_link_options(navigation_ingress_tests PRIVATE -Wl,-dead_strip ${YABAI_SANITIZE_FLAGS})
target_link_libraries(navigation_ingress_tests PRIVATE "-framework Cocoa" "-framework Carbon")
add_test(NAME navigation_ingress_tests COMMAND navigation_ingress_tests)

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

add_executable(event_queue_tests tests/event_loop/queue.c)
target_compile_options(event_queue_tests PRIVATE -UNDEBUG ${YABAI_SANITIZE_FLAGS})
target_link_options(event_queue_tests PRIVATE ${YABAI_SANITIZE_FLAGS})
add_test(NAME event_queue_tests COMMAND event_queue_tests)

# yabai-msg exists to start without the daemon's frameworks.
add_test(NAME yabai_msg_frameworks
    COMMAND sh -c "! otool -L \"$1\" | grep -q framework" sh $<TARGET_FILE:yabai-msg>)

add_executable(socket_identity_tests tests/identity/peer.c)
target_compile_options(socket_identity_tests PRIVATE -UNDEBUG ${YABAI_SANITIZE_FLAGS})
target_link_options(socket_identity_tests PRIVATE ${YABAI_SANITIZE_FLAGS})
target_link_libraries(socket_identity_tests PRIVATE "-framework CoreFoundation" "-framework Security" "-lbsm.0")
add_test(NAME socket_identity_tests COMMAND socket_identity_tests)
set_tests_properties(socket_identity_tests PROPERTIES TIMEOUT 30)

add_executable(fade_tests tests/fade/timing.c)
target_compile_options(fade_tests PRIVATE -UNDEBUG ${YABAI_SANITIZE_FLAGS})
target_link_options(fade_tests PRIVATE ${YABAI_SANITIZE_FLAGS})
target_link_libraries(fade_tests PRIVATE "-framework CoreFoundation" "-framework CoreGraphics")
add_test(NAME fade_tests COMMAND fade_tests)
set_tests_properties(fade_tests PROPERTIES TIMEOUT 15)

# The payload's request handling, compiled outside Dock with SkyLight stubbed.
add_executable(payload_tests tests/payload/requests.m)
target_include_directories(payload_tests PRIVATE ${SRC}/osax)
target_compile_definitions(payload_tests PRIVATE YABAI_ALLOW_UNSIGNED_LOCAL=1)
target_compile_options(payload_tests PRIVATE -UNDEBUG -fno-objc-arc -Wno-deprecated-declarations ${YABAI_SANITIZE_FLAGS})
target_link_options(payload_tests PRIVATE ${YABAI_SANITIZE_FLAGS})
target_link_libraries(payload_tests PRIVATE
    "-framework AppKit" "-framework QuartzCore" "-framework Carbon" "-framework CoreGraphics" "-framework Security" "-lbsm.0")
add_test(NAME payload_tests COMMAND payload_tests)
set_tests_properties(payload_tests PROPERTIES TIMEOUT 15)

# libFuzzer targets for the two socket protocols, see docs/TESTING.md. They need
# a clang that ships libFuzzer, which Apple's does not. Each also runs as a
# short CTest test, writing new inputs to the build directory.
option(YABAI_FUZZ "Build the libFuzzer targets and run them as tests" OFF)
set(YABAI_FUZZ_SECONDS 30 CACHE STRING "Seconds each fuzz target runs as a test")

if(YABAI_FUZZ)
    set(YABAI_FUZZ_FLAGS -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined)

    add_executable(fuzz_daemon_message tests/fuzz/daemon_message.m)
    target_compile_definitions(fuzz_daemon_message PRIVATE TESTS)
    target_compile_definitions(fuzz_daemon_message PRIVATE YABAI_ALLOW_UNSIGNED_LOCAL=1)
    target_link_libraries(fuzz_daemon_message PRIVATE
        "-framework Carbon" "-framework Cocoa" "-framework CoreServices" "-framework CoreVideo" "-framework SkyLight"
        "-weak_framework ScreenCaptureKit" "-framework QuartzCore" "-framework Security" "-lbsm.0")

    add_executable(fuzz_payload_message tests/fuzz/payload_message.m)
    target_include_directories(fuzz_payload_message PRIVATE ${SRC}/osax)
    target_compile_definitions(fuzz_payload_message PRIVATE YABAI_ALLOW_UNSIGNED_LOCAL=1)
    target_link_libraries(fuzz_payload_message PRIVATE
        "-framework AppKit" "-framework QuartzCore" "-framework Carbon" "-framework CoreGraphics" "-framework Security" "-lbsm.0")

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
# docs/OSAX.md. They inspect the running system, so they build for the host.
option(YABAI_BUILD_TOOLS "Build the scripting-addition tools and their test" ON)

if(YABAI_BUILD_TOOLS AND CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
    add_executable(pattern_check tools/osax/pattern_check.m)
    target_include_directories(pattern_check PRIVATE ${SRC}/osax)
    target_link_libraries(pattern_check PRIVATE "-framework Foundation")

    add_executable(sa_client tools/osax/sa_client.c)
    target_include_directories(sa_client PRIVATE ${SRC}/osax)
    target_link_libraries(sa_client PRIVATE "-framework CoreFoundation" "-framework SkyLight" "-lbsm.0")

    add_executable(window_sub_level tools/skylight/window_sub_level.c)
    target_link_libraries(window_sub_level PRIVATE "-framework CoreFoundation" "-framework CoreGraphics" "-framework SkyLight")

    # Helpers of the live navigation tests in tools/live.
    add_executable(space_poll tools/live/space_poll.c)
    target_link_libraries(space_poll PRIVATE "-framework CoreFoundation" "-framework SkyLight")

    add_executable(ax_focused tools/live/ax_focused.c)
    target_link_libraries(ax_focused PRIVATE "-framework ApplicationServices")

    foreach(tool pattern_check sa_client window_sub_level space_poll ax_focused)
        set_target_properties(${tool} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
            OSX_ARCHITECTURES arm64)
        target_compile_options(${tool} PRIVATE -Wall -Wextra -fno-objc-arc)
        target_link_options(${tool} PRIVATE -F${PRIVATE_FRAMEWORKS})
    endforeach()

    # Resolves every payload lookup in the Dock of the machine running the test.
    add_test(NAME osax_patterns COMMAND pattern_check)
endif()

# Language server support. A source the unity build includes is parsed with
# everything the compiler has seen before it: the prelude of the file that
# includes it, then that file's text up to the #include, with the conditionals
# still open closed at the end. A source that includes further sources is
# their unity file in turn. The target is never built.
#
# The text is handled as one string: a list of lines would split at
# semicolons and merge lines around an unbalanced bracket.
function(yabai_unity_sources unity_file target prelude_var depth)
    # Read before this call sets a variable of the same name for its own
    # sources.
    set(inherited "${${prelude_var}}")
    file(READ ${unity_file} content)
    get_filename_component(unity_dir ${unity_file} DIRECTORY)
    string(REGEX MATCHALL "(^|\n)#include \"[^\"\n]+\\.[cm]\"" includes "${content}")
    if(NOT includes)
        return()
    endif()

    # A renamed or removed source would otherwise stay in the build graph, and
    # Ninja stops at the missing file until CMake runs again; an edit before a
    # nested include changes that include's prelude.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${unity_file})

    foreach(include IN LISTS includes)
        string(REGEX REPLACE "^\n?#include \"([^\"]+)\"$" "\\1" name "${include}")
        string(FIND "\n${content}" "\n#include \"${name}\"" position)
        string(SUBSTRING "${content}" 0 ${position} before)

        # Quoted includes resolve next to the file first; the prelude lives in
        # the build directory, so it names them by absolute path.
        string(REGEX MATCHALL "(^|\n)#include \"[^\"\n]+\"" quoted "${before}")
        foreach(line IN LISTS quoted)
            string(REGEX REPLACE "^\n?#include \"([^\"]+)\"$" "\\1" path "${line}")
            if(EXISTS ${unity_dir}/${path})
                get_filename_component(absolute ${unity_dir}/${path} ABSOLUTE)
                string(REPLACE "#include \"${path}\"" "#include \"${absolute}\"" before "${before}")
            endif()
        endforeach()

        # A source included inside a conditional, such as one payload per
        # architecture, would leave its prelude unterminated.
        string(REGEX MATCHALL "(^|\n)[ \t]*#[ \t]*if" opened "${before}")
        string(REGEX MATCHALL "(^|\n)[ \t]*#[ \t]*endif" closed "${before}")
        list(LENGTH opened opened)
        list(LENGTH closed closed)
        math(EXPR open "${depth} + ${opened} - ${closed}")

        get_filename_component(source ${unity_dir}/${name} ABSOLUTE)
        file(RELATIVE_PATH id ${SRC} ${source})
        string(MAKE_C_IDENTIFIER ${id} id)
        set(header ${CMAKE_BINARY_DIR}/unity/${id}.h)
        set(source_prelude "${inherited}#line 1 \"${unity_file}\"\n${before}")
        string(REPEAT "#endif\n" ${open} closing)
        file(CONFIGURE OUTPUT ${header} CONTENT "${source_prelude}${closing}" @ONLY)
        target_sources(${target} PRIVATE ${source})
        set_source_files_properties(${source} PROPERTIES
            LANGUAGE OBJC
            COMPILE_OPTIONS "-include;${header}")

        yabai_unity_sources(${source} ${target} source_prelude ${open})
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
set(yabai_unity_prelude "")
yabai_unity_sources(${SRC}/yabai_main.m yabai_unity yabai_unity_prelude 0)
yabai_unity_sources(${SRC}/navigation/navigation_effects.m yabai_unity yabai_unity_prelude 0)
yabai_unity_sources(${SRC}/sa/sa_unity.m yabai_unity yabai_unity_prelude 0)
yabai_unity_sources(${SRC}/displays/displays.m yabai_unity yabai_unity_prelude 0)
yabai_unity_sources(${SRC}/applications/applications.m yabai_unity yabai_unity_prelude 0)
yabai_unity_sources(${SRC}/spaces/layout_unity.m yabai_unity yabai_unity_prelude 0)
yabai_unity_sources(${SRC}/events/events.m yabai_unity yabai_unity_prelude 0)
yabai_unity_sources(${SRC}/ipc/ipc.m yabai_unity yabai_unity_prelude 0)
yabai_unity_sources(${SRC}/osax/payload.m yabai_unity yabai_unity_prelude 0)
set_property(SOURCE ${SRC}/osax/x64_payload.m APPEND PROPERTY COMPILE_OPTIONS -arch x86_64)
