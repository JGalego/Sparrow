cmake_minimum_required(VERSION 3.20)
project(Sparrow VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SPARROW_BUILD_TESTS "Build C unit tests" ON)
option(SPARROW_BUILD_HMI "Build the LVGL HMI (needs network on first configure)" ON)
set(SPARROW_SANITIZE "" CACHE STRING "Sanitizers to enable, e.g. address,undefined")
set(SPARROW_DISPLAY_BACKENDS "sdl;headless" CACHE STRING
    "Display backends to compile: sdl, fbdev, headless")

if(SPARROW_BUILD_HMI)
    include(cmake/lvgl.cmake)
endif()

add_library(sparrow_warnings INTERFACE)
target_compile_options(sparrow_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wstrict-prototypes -Wmissing-prototypes)
if(SPARROW_SANITIZE)
    target_compile_options(sparrow_warnings INTERFACE -fsanitize=${SPARROW_SANITIZE} -fno-omit-frame-pointer)
    target_link_options(sparrow_warnings INTERFACE -fsanitize=${SPARROW_SANITIZE})
endif()
link_libraries(sparrow_warnings)

if(SPARROW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(sparrow/testing)
endif()

add_subdirectory(sparrow/core)
add_subdirectory(sparrow/platform)
if(SPARROW_BUILD_HMI)
    add_subdirectory(sparrow/hmi)
endif()
add_subdirectory(examples/smart-boiler)
if(SPARROW_BUILD_TESTS)
    add_subdirectory(tests/core)
endif()
