# Cross toolchain for 64-bit ARM Linux boards (NXP i.MX 8 class and similar).
#
# Uses the distribution cross compiler by default. To build against a vendor
# SDK, point SPARROW_SYSROOT at its sysroot and SPARROW_TOOLCHAIN_PREFIX at its
# compiler prefix, e.g. -DSPARROW_TOOLCHAIN_PREFIX=aarch64-poky-linux-.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT SPARROW_TOOLCHAIN_PREFIX)
    set(SPARROW_TOOLCHAIN_PREFIX aarch64-linux-gnu-)
endif()

set(CMAKE_C_COMPILER ${SPARROW_TOOLCHAIN_PREFIX}gcc)
# LVGL's CMake project enables C++ although the parts Sparrow builds are C.
set(CMAKE_CXX_COMPILER ${SPARROW_TOOLCHAIN_PREFIX}g++)

if(SPARROW_SYSROOT)
    set(CMAKE_SYSROOT ${SPARROW_SYSROOT})
    set(CMAKE_FIND_ROOT_PATH ${SPARROW_SYSROOT})
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
endif()
