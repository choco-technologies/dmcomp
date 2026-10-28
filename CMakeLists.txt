# =====================================================================
#               dmcomp - DMOD compression (DEFLATE)
# =====================================================================
cmake_minimum_required(VERSION 3.16)
project(dmcomp VERSION 0.1.0 DESCRIPTION "DMOD compression" LANGUAGES C)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(DMCOMP_IS_TOP_LEVEL ON)
else()
    set(DMCOMP_IS_TOP_LEVEL OFF)
endif()

option(DMCOMP_BUILD_ENCODER "Build the encoder (dmcomp_deflate, needs a heap); OFF: the decoder only" ON)
option(DMCOMP_BUILD_TESTS   "Build the tests"                                   ${DMCOMP_IS_TOP_LEVEL})
option(DMCOMP_BUILD_TOOLS   "Build the command line tool"                       ${DMCOMP_IS_TOP_LEVEL})
option(DMCOMP_WITH_ZLIB     "Test against zlib when it is installed"            ON)
option(DMCOMP_SANITIZE      "Build with AddressSanitizer and UBSanitizer"       OFF)
option(DMCOMP_WERROR        "Warnings are errors"                               OFF)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(DMCOMP_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ======================================================================
#               The library
# ======================================================================
add_library(dmcomp STATIC
    src/dmcomp_inflate.c
    src/dmcomp_strerror.c
)
if(DMCOMP_BUILD_ENCODER)
    target_sources(dmcomp PRIVATE src/dmcomp_deflate.c)
else()
    target_compile_definitions(dmcomp PUBLIC DMCOMP_NO_ENCODER)
endif()
target_include_directories(dmcomp PUBLIC include)
target_include_directories(dmcomp PRIVATE src)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(DMCOMP_WARNINGS -Wall -Wextra -Wshadow -Wpedantic -Wstrict-prototypes -Wmissing-prototypes)
    if(DMCOMP_WERROR)
        list(APPEND DMCOMP_WARNINGS -Werror)
    endif()
    target_compile_options(dmcomp PRIVATE ${DMCOMP_WARNINGS})
    if(DMCOMP_SANITIZE)
        set(DMCOMP_SANITIZE_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
        target_compile_options(dmcomp PUBLIC ${DMCOMP_SANITIZE_FLAGS})
        target_link_options(dmcomp PUBLIC ${DMCOMP_SANITIZE_FLAGS})
    endif()
endif()

# ======================================================================
#               Tool and tests
# ======================================================================
if(DMCOMP_BUILD_TOOLS AND DMCOMP_BUILD_ENCODER)
    add_executable(dmcomp_tool tools/dmcomp_tool.c)
    target_link_libraries(dmcomp_tool PRIVATE dmcomp)
    if(DEFINED DMCOMP_WARNINGS)
        target_compile_options(dmcomp_tool PRIVATE ${DMCOMP_WARNINGS})
    endif()
endif()

if(DMCOMP_BUILD_TESTS AND DMCOMP_BUILD_ENCODER)
    enable_testing()
    add_subdirectory(tests)
endif()
