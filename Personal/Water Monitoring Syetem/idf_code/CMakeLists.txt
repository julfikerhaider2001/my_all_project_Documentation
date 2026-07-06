# Southern Water Monitor — ESP-IDF FreeRTOS Implementation
# Root CMakeLists.txt

cmake_minimum_required(VERSION 3.16)

project(southern_water_monitor
    VERSION 4.5.1
    DESCRIPTION "Water level monitoring with RainMaker + FreeRTOS"
    LANGUAGES C CXX)

# Require IDF v5.0 or later
set(IDF_VERSION_REQUIRED 5.0)

# Include the idf cmake tools
include($ENV{IDF_PATH}/tools/cmake/idf.cmake)

# Add main component
idf_build_process(${CMAKE_CURRENT_SOURCE_DIR})
