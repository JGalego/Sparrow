file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/results)

add_library(sparrow_test STATIC sp_test_main.c)
target_include_directories(sparrow_test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sparrow_test PUBLIC m)

# sparrow_add_c_test(<name> SOURCES <files...> LIBRARIES <libs...>)
function(sparrow_add_c_test name)
    cmake_parse_arguments(TEST "" "" "SOURCES;LIBRARIES" ${ARGN})
    add_executable(${name} ${TEST_SOURCES})
    target_compile_definitions(${name} PRIVATE SP_TEST_SUITE="${name}")
    target_link_libraries(${name} PRIVATE sparrow_test ${TEST_LIBRARIES})
    add_test(NAME ${name}
             COMMAND ${name} --junit ${CMAKE_BINARY_DIR}/results/${name}.xml)
endfunction()
