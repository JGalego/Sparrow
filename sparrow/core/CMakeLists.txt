add_library(sparrow_core STATIC sp_alarm.c sp_frame.c sp_persist.c sp_pi.c sp_ring.c)
target_include_directories(sparrow_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sparrow_core PUBLIC m)
set_target_properties(sparrow_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
