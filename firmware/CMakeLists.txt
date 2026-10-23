cmake_minimum_required(VERSION 3.16)
# 只编需要的组件（不带 Wi-Fi / 蓝牙 / LVGL），编译快、固件小
set(COMPONENTS main cmake_utilities)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nabo-soundlab-passport)
