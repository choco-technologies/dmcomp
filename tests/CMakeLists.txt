# =====================================================================
#               Tests
# =====================================================================
# Every test is a program that returns the number of the failed checks
function(dmcomp_add_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE dmcomp)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(DEFINED DMCOMP_WARNINGS)
        target_compile_options(${name} PRIVATE ${DMCOMP_WARNINGS})
    endif()
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 600)
endfunction()

# The Huffman code of the encoder, tested from the inside: its source is part of the test, so it is not linked to the library
add_executable(test_huffman test_huffman.c)
target_include_directories(test_huffman PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/include)
if(DEFINED DMCOMP_SANITIZE_FLAGS)
    target_compile_options(test_huffman PRIVATE ${DMCOMP_SANITIZE_FLAGS})
    target_link_options(test_huffman PRIVATE ${DMCOMP_SANITIZE_FLAGS})
endif()
if(DEFINED DMCOMP_WARNINGS)
    target_compile_options(test_huffman PRIVATE ${DMCOMP_WARNINGS})
endif()
add_test(NAME test_huffman COMMAND test_huffman)

dmcomp_add_test(test_vectors)
dmcomp_add_test(test_roundtrip)
dmcomp_add_test(test_corrupt)

# zlib is the reference: what it writes is read here, and what is written here, it reads
if(DMCOMP_WITH_ZLIB)
    find_package(ZLIB)
    if(ZLIB_FOUND)
        dmcomp_add_test(test_zlib)
        target_link_libraries(test_zlib PRIVATE ZLIB::ZLIB)
    else()
        message(STATUS "zlib not found - the compatibility test is not built")
    endif()
endif()
