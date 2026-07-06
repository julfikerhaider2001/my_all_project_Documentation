cmake_minimum_required(VERSION 3.16)

# Allow multiple definitions for protobuf symbols to avoid duplicate symbol errors
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--allow-multiple-definition")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(southern_water_monitor)
