# 《NABO 声音实验室》应用：界面（ui）+ 图形引擎（gfx，沿用赛博算命机）+ 麦克风分析（mic）+ 变声（voice）
file(GLOB UI_SRCS "ui/*.c")
idf_component_register(
    SRCS "main.c" "mic/nabo_mic.c" "voice/nabo_voice.c" "gfx/fc_gfx.c" "gfx/fc_res.c" ${UI_SRCS}
    INCLUDE_DIRS "." "mic" "voice" "gfx" "ui"
    REQUIRES bsp esp_lcd esp_timer nvs_flash esp_driver_usb_serial_jtag vfs esp_partition spi_flash
)
# 热点代码（DSP、合成、送屏合成）用 -O2，其余默认 -Os
set_source_files_properties(mic/nabo_mic.c voice/nabo_voice.c gfx/fc_gfx.c ui/sl_audio.c ui/sl_sampler.c PROPERTIES COMPILE_OPTIONS "-O2")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function -Wno-format-truncation)
# idf.py flash 时把素材包一起烧进 assets 分区
esptool_py_flash_to_partition(flash "assets" "${CMAKE_CURRENT_LIST_DIR}/../assets/soundlab_assets.bin")
