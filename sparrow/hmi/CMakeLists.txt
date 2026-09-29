add_library(sparrow_hmi STATIC
    sp_button.c
    sp_gauge.c
    sp_notification_list.c
    sp_panel.c
    sp_pill.c
    sp_theme.c
    sp_trend.c)
target_include_directories(sparrow_hmi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sparrow_hmi PUBLIC lvgl sparrow_core)
