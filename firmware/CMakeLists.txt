cmake_minimum_required(VERSION 3.16)
# 普通构建不带 Wi-Fi；独立对战构建通过 main 的依赖引入 ESP-NOW。
option(SOUNDLAB_WIRELESS_DUEL "Build the isolated experimental wireless duel" OFF)
# IDF discovers component requirements in a separate `cmake -P` process. Its
# early pass does not inherit this cache variable, but does inherit the process
# environment. Set OFF as well, so an ordinary configure cannot inherit ON.
set(ENV{SOUNDLAB_WIRELESS_DUEL} "${SOUNDLAB_WIRELESS_DUEL}")
if(SOUNDLAB_WIRELESS_DUEL)
    set(PROJECT_VER "0.2.0-dev")
endif()
set(COMPONENTS main cmake_utilities)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nabo-soundlab-passport)
