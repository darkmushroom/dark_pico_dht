cmake_minimum_required(VERSION 3.21)

# I can't believe this works.
#
# CMake runs a test before building to make sure the
# C compiler is correctly installed and configured. 
# This does not work for us because the test is
# (presumably) in x86 C and our environment is designed to
# cross-compile to arm
#
# so... we'll just give cmake a wink and a nod that
# everything is A-okay.
set(CMAKE_C_COMPILER_WORKS 1)

include(pico_sdk_import.cmake)

project(pico-projects)

pico_sdk_init()

add_executable(pico_temp_tracker
  pico_temp_tracker.c
)

# TODO: cy43w_arch_none gives us no added utility.
# https://github.com/raspberrypi/pico-sdk/releases
# includes other cyw43 libs that may aid us greatly
# building a server to host/send sensor values
target_link_libraries(pico_temp_tracker
  pico_stdlib
  pico_cyw43_arch_none
)

# Treat USB as serial so we can reset + reload on build
pico_enable_stdio_usb(pico_temp_tracker 1)
pico_enable_stdio_uart(pico_temp_tracker 0)

pico_add_extra_outputs(pico_temp_tracker)
