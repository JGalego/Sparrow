# Runtime services with no display dependency: timing, watchdog, UDP.
add_library(sparrow_rt STATIC sp_period.c sp_udp.c sp_watchdog.c)
target_include_directories(sparrow_rt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sparrow_rt PUBLIC sparrow_core)
target_compile_definitions(sparrow_rt PRIVATE _GNU_SOURCE)

if(SPARROW_BUILD_HMI)
    add_library(sparrow_platform STATIC
        sp_display.c
        sp_display_fbdev.c
        sp_display_headless.c
        sp_display_sdl.c
        sp_snapshot.c)
    target_include_directories(sparrow_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(sparrow_platform PUBLIC lvgl sparrow_rt)
    target_compile_definitions(sparrow_platform PRIVATE _GNU_SOURCE)
endif()
