# Fetches LVGL at a pinned release. Included before the project-wide warning
# flags are applied so third-party code is built with its own settings.
#
# Offline builds: -DFETCHCONTENT_SOURCE_DIR_LVGL=/path/to/lvgl-v9.2.2
include(FetchContent)

set(LV_CONF_PATH ${CMAKE_SOURCE_DIR}/sparrow/hmi/lv_conf.h CACHE PATH "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.2.2
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(lvgl)
# LVGL's Linux drivers (fbdev, evdev) need POSIX declarations hidden by strict ISO C.
set_target_properties(lvgl PROPERTIES C_EXTENSIONS ON)

if("sdl" IN_LIST SPARROW_DISPLAY_BACKENDS)
    find_package(SDL2 REQUIRED)
    target_compile_definitions(lvgl PUBLIC SPARROW_WITH_SDL=1)
    target_link_libraries(lvgl PUBLIC SDL2::SDL2)
endif()
if("fbdev" IN_LIST SPARROW_DISPLAY_BACKENDS)
    target_compile_definitions(lvgl PUBLIC SPARROW_WITH_FBDEV=1)
endif()
