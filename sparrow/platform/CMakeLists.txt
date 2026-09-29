set(SPARROW_PLATFORM_SOURCES
    sp_display.c
    sp_display_fbdev.c
    sp_display_headless.c
    sp_display_sdl.c
    sp_snapshot.c
    sp_udp.c)

add_library(sparrow_platform STATIC ${SPARROW_PLATFORM_SOURCES})
target_include_directories(sparrow_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sparrow_platform PUBLIC lvgl sparrow_core)
target_compile_definitions(sparrow_platform PRIVATE _GNU_SOURCE)
