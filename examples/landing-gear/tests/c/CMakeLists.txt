# sparrow_add_c_test(test_gear_<area> SOURCES test_gear_<area>.c LIBRARIES gear_controller)
sparrow_add_c_test(test_gear_initialization SOURCES test_gear_initialization.c LIBRARIES gear_controller)
sparrow_add_c_test(test_gear_handle_lock SOURCES test_gear_handle_lock.c LIBRARIES gear_controller)
sparrow_add_c_test(test_gear_retraction_interlock
    SOURCES test_gear_retraction_interlock.c
    LIBRARIES gear_controller)
sparrow_add_c_test(test_gear_normal_extension
    SOURCES test_gear_normal_extension.c
    LIBRARIES gear_controller)
sparrow_add_c_test(test_gear_normal_retraction
    SOURCES test_gear_normal_retraction.c
    LIBRARIES gear_controller)
sparrow_add_c_test(test_gear_selector_exclusion
    SOURCES test_gear_selector_exclusion.c
    LIBRARIES gear_controller)
sparrow_add_c_test(test_gear_normal_transit
    SOURCES test_gear_normal_transit.c
    LIBRARIES gear_controller)
sparrow_add_c_test(test_gear_disagree_reset
    SOURCES test_gear_disagree_reset.c
    LIBRARIES gear_controller)
sparrow_add_c_test(test_gear_alternate_extension
    SOURCES test_gear_alternate_extension.c
    LIBRARIES gear_controller)
sparrow_add_c_test(test_gear_too_low_warning
    SOURCES test_gear_too_low_warning.c
    LIBRARIES gear_controller)
sparrow_add_c_test(test_gear_horn_mute
    SOURCES test_gear_horn_mute.c
    LIBRARIES gear_controller)
sparrow_add_c_test(test_gear_warning_arming
    SOURCES test_gear_warning_arming.c
    LIBRARIES gear_controller)
