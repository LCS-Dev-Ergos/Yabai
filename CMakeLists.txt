cmake_minimum_required(VERSION 3.25)

# Builds the same artifacts as the makefile: the scripting-addition payload and
# loader, embedded into the yabai binary. The makefile remains the upstream
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
target_link_libraries(payload PRIVATE "-framework SkyLight" "-framework Foundation" "-framework Carbon")

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
    "-framework Carbon" "-framework Cocoa" "-framework CoreServices" "-framework CoreVideo" "-framework SkyLight")

if(YABAI_SANITIZE STREQUAL "address")
    target_compile_options(yabai PRIVATE -fsanitize=address,undefined)
    target_link_options(yabai PRIVATE -fsanitize=address,undefined)
elseif(YABAI_SANITIZE STREQUAL "thread")
    target_compile_options(yabai PRIVATE -fsanitize=thread,undefined)
    target_link_options(yabai PRIVATE -fsanitize=thread,undefined)
elseif(NOT YABAI_SANITIZE STREQUAL "")
    message(FATAL_ERROR "YABAI_SANITIZE must be empty, address or thread")
endif()

if(YABAI_CODESIGN_IDENTITY)
    add_custom_command(TARGET yabai POST_BUILD
        COMMAND codesign -fs ${YABAI_CODESIGN_IDENTITY} $<TARGET_FILE:yabai>
        VERBATIM)
endif()

# Language server support. Each source included by a unity translation unit is
# parsed with everything its unity file includes before it, which is exactly
# the context the compiler sees. The target is never built.
function(yabai_unity_sources unity_file target)
    file(STRINGS ${unity_file} lines)
    get_filename_component(unity_dir ${unity_file} DIRECTORY)
    set(prelude "")
    foreach(line IN LISTS lines)
        if(line MATCHES "^#include \"([^\"]+\\.[cm])\"$")
            set(source ${unity_dir}/${CMAKE_MATCH_1})
            get_filename_component(source ${source} ABSOLUTE)
            file(RELATIVE_PATH id ${SRC} ${source})
            string(MAKE_C_IDENTIFIER ${id} id)
            set(header ${CMAKE_BINARY_DIR}/unity/${id}.h)
            file(CONFIGURE OUTPUT ${header} CONTENT "${prelude}" @ONLY)
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

add_library(yabai_unity OBJECT EXCLUDE_FROM_ALL)
target_include_directories(yabai_unity PRIVATE ${SRC} ${OSAX_OUT})
target_compile_options(yabai_unity PRIVATE -std=c11 -fno-objc-arc)
yabai_unity_sources(${SRC}/manifest.m yabai_unity)
yabai_unity_sources(${SRC}/osax/payload.m yabai_unity)
set_property(SOURCE ${SRC}/osax/x64_payload.m APPEND PROPERTY COMPILE_OPTIONS -arch x86_64)
