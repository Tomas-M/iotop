cmake_minimum_required(VERSION 2.8)

project(IOTOP)
file(GLOB SRC src/*)

add_executable(iotop ${SRC})
target_link_libraries(iotop procps ncurses)
