cmake_minimum_required(VERSION 3.10)
project(i2c_led_client_driver)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Main project sources
add_library(i2c_led_client_driver led-driver/driver.c)

# Enable testing
enable_testing()

# Add tests subdirectory
add_subdirectory(tests)
