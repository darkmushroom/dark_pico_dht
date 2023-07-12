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

target_include_directories(pico_temp_tracker PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
)

# pico_stdlib
# Many useful functions here, including our printf and queue
# https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#pico_stdlib
#
# pico_cyw43_arch_lwip_poll
# this library gives us a TCP/IP stack using lwip, compiled
# specifically for our cyw43 chip.
# https://github.com/raspberrypi/pico-sdk/releases/tag/1.4.0
# https://www.nongnu.org/lwip/2_1_x/index.html
# 
# pico_multicore
# Handles everything about initializing, setting up the
# stack, and running code on the second pi pico cpu core
# https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#pico_multicore
target_link_libraries(pico_temp_tracker
  pico_stdlib
  pico_cyw43_arch_lwip_poll
  pico_multicore
)

# Treat USB as serial so we can reset + reload on build
pico_enable_stdio_usb(pico_temp_tracker 1)
pico_enable_stdio_uart(pico_temp_tracker 0)

pico_add_extra_outputs(pico_temp_tracker)
