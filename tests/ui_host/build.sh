#!/bin/sh
# 宿主机编译 UI 共享库（ASan/UBSan）。用法：sh build.sh
cd "$(dirname "$0")"; F=../../firmware/main
gcc -std=c11 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined -fPIC -shared -o libslhost.so sl_host.c \
  $F/ui/*.c $F/gfx/fc_gfx.c $F/gfx/fc_res.c $F/mic/nabo_mic.c $F/voice/nabo_voice.c -I$F/gfx -lm
