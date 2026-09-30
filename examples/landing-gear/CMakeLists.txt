set(GEAR_GENERATED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/generated)

add_library(gear_model STATIC generated/gear_model.c)
target_include_directories(gear_model PUBLIC ${GEAR_GENERATED_DIR})
set_target_properties(gear_model PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Every controller source except the ctypes wrapper. A glob, so that files the
# `sparrow ai code` stage adds are built without editing this file.
file(GLOB GEAR_CONTROLLER_SOURCES CONFIGURE_DEPENDS controller/*.c)
list(REMOVE_ITEM GEAR_CONTROLLER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/controller/gear_capi.c)
add_library(gear_controller STATIC ${GEAR_CONTROLLER_SOURCES})
target_include_directories(gear_controller PUBLIC controller)
target_link_libraries(gear_controller PUBLIC gear_model sparrow_core)
set_target_properties(gear_controller PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Shared library loaded by the Python simulator through ctypes.
add_library(gear_controller_shared SHARED controller/gear_capi.c)
target_link_libraries(gear_controller_shared PRIVATE gear_controller)
set_target_properties(gear_controller_shared PROPERTIES OUTPUT_NAME gear_controller)

if(SPARROW_BUILD_TESTS)
    add_subdirectory(tests/c)
endif()
