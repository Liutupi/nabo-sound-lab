# NABO 声音实验室独立固件

ESP32-C3 / AI Passport / 8 MB Flash / 240×320 / ES8311，ESP-IDF 5.5.3。全部玩法离线运行。当前源码版本 **0.1.2**；实际烧录和验收状态以对应 dist/soundlab-0.1.2/DEVICE_TEST.md 为准。

首页在进入约1.4秒后播放「NABO，陪你玩音乐」。使用与赛博算命机相同的「阳光小男孩」音色；离开首页立即停止语音。16kHz 单声道 PCM16 直接从 Flash 读取，播报时其他声音降低到四分之一。

20个场景包含首页、菜单、设置、声音玩具、可视化、音乐与吉他工具。0.1.2补齐哼歌飞行、拍手节奏、采样琴；规则与内存约束见 ../docs/NEW_GAMES_0_1_2.md。主要中文、操作提示16px，菜单六行；OK确认，双击▲返回。

## 构建与素材

在项目根目录运行：

```powershell
python tools/gen_fonts_soundlab.py
python tools/pack_props_soundlab.py
python tools/pack_assets_soundlab.py
& .\firmware\tools\build.ps1 -Action all
```

字体生成需要Pillow和fontTools；Windows终端建议 PYTHONIOENCODING=utf-8。构建脚本复用本机IDF5.5.3和已缓存的button、esp_codec_dev组件。

地址：bootloader 0x0，分区表0x8000，NVS 0x9000，app 0x10000（上限0x1f0000字节），assets 0x200000（6MiB）。已安装0.1.1且分区相同的设备，升级只写新app和assets，保留NVS；必须同时更新两个文件。新安装使用匹配版本完整包。每次写入前重新确认USB/MAC，备份当前Flash，写后回读核对。原0.1.1二进制保持原样。

## 串口诊断

USB Serial/JTAG，每行一条：

```text
status        版本、场景、音量、音高、BPM、A4、内存、缓存与新游戏状态
perf          帧耗时、分析耗时；读取后清零统计
go flight     跳到指定场景
key o 1       OK按下；key o 0松开；u/d对应▲/▼
calm 1        减弱闪烁并保存；calm 0关闭
sens 0        本次运行的麦克风灵敏度
capture       下一帧完整240×320 RGB565HEX截图
```

场景ID：boot menu settings echo shout candle rope flight clap sampler range spec water scope chroma db tuner guitar beat pitch。

截图由SL_CAPTURE_BEGIN、320行SLPIX y hex、SL_CAPTURE_END组成，只使用约1KB行缓冲。USB传输会暂停界面推进，因此截图帧不纳入正常渲染性能统计。性能采样避免截图、实体按键和切换场景，清零后在同一场景至少记录十秒。

## 验证边界

新游戏测试：python tests/newgames_host/test_newgames.py；覆盖规则、C/JS对拍和实际采样混音。tests/ui_host驱动真实UI、DSP与渲染器，比较增量和完整重绘。0.1.2额外图像、动画、缓存和语音证据在dist/soundlab-0.1.2/offline-review。

宿主测试和编译通过不能代替实机验收。喇叭听感、实体键、音高精度、新游戏、内存恢复、休眠唤醒均须写入设备报告。0.1.1原始实测记录保留在其自身dist目录。
