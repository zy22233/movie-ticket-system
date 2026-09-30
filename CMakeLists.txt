cmake_minimum_required(VERSION 3.28)

set(CMAKE_C_COMPILER "/usr/local/arm/5.4.0/usr/bin/arm-linux-gcc")
set(CMAKE_CXX_COMPILER "/usr/local/arm/5.4.0/usr/bin/arm-linux-g++")

project(buymovie_arm)

include_directories(.)
include_directories(./lvgl)
include_directories(./lv_drivers)
include_directories(./UI)

add_subdirectory(lvgl)
add_subdirectory(lv_drivers)
add_subdirectory(UI)

add_executable(${PROJECT_NAME} main.c  simfang.c ./UI/ui.c )

target_link_libraries(${PROJECT_NAME} PRIVATE lvgl lvgl::examples lvgl::demos lvgl::drivers ui pthread)
