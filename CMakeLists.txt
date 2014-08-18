cmake_minimum_required(VERSION 2.8)

project(IOTOP C)
file(GLOB SRC src/*)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall --pedantic --std=c99")

add_executable(iotop ${SRC})
target_link_libraries(iotop procps ncurses)
