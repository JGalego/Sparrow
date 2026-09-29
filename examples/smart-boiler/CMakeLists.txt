set(BOILER_GENERATED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/generated)

add_library(boiler_model STATIC generated/boiler_model.c)
target_include_directories(boiler_model PUBLIC ${BOILER_GENERATED_DIR})
set_target_properties(boiler_model PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(boiler_controller STATIC
    controller/boiler_controller.c
    controller/boiler_faults.c
    controller/boiler_measure.c
    controller/boiler_sequence.c)
target_include_directories(boiler_controller PUBLIC controller)
target_link_libraries(boiler_controller PUBLIC boiler_model sparrow_core)
set_target_properties(boiler_controller PROPERTIES POSITION_INDEPENDENT_CODE ON)

# HMI application state, view model and actions. No LVGL dependency.
add_library(boiler_hmi_model STATIC
    hmi/boiler_actions.c
    hmi/boiler_app.c
    hmi/boiler_notify.c
    hmi/boiler_view.c)
target_include_directories(boiler_hmi_model PUBLIC hmi ${CMAKE_SOURCE_DIR}/sparrow/hmi)
target_link_libraries(boiler_hmi_model PUBLIC boiler_model sparrow_core)

# Shared library loaded by the Python simulator through ctypes.
add_library(boiler_controller_shared SHARED controller/boiler_capi.c)
target_link_libraries(boiler_controller_shared PRIVATE boiler_controller)
set_target_properties(boiler_controller_shared PROPERTIES OUTPUT_NAME boiler_controller)

if(SPARROW_BUILD_TESTS)
    add_subdirectory(tests/c)
endif()
