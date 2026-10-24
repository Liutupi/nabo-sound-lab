<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# 麦克风 DSP 宿主测试

在仓库根目录运行音高回归测试。需要 Python 3.10+、NumPy、Node.js 和原生
C 编译器（GCC 或 Clang）：

```sh
python -m pip install numpy
python tests/mic_host/test_pitch.py
```

入口使用 `CC`（默认 `cc`）和 `NABO_NODE`（默认从 `PATH` 查找 `node`）。
测试将生产 DSP 编译为临时共享库，退出时删除测试输出。在 Linux/GCC 环境可启用
未定义行为检查：

```sh
NM_UBSAN=1 python tests/mic_host/test_pitch.py
```

48 组固定合成信号覆盖 60.5–1046.5 Hz 纯音、多谐波、弱基频、含噪 A4、静音、
直流、白噪声、接近满幅输入和音符/静音切换。先用生成频率检验精度和识别覆盖率，
再与浏览器原型中提取的实际 `yin()` 函数比较 1,625 个音高检测帧。不需要浏览器或
麦克风权限。测试还会故意移除模糊判断，确认含噪 A4 精度检查能抓住错误。

## 含噪输入的子谐波拒绝

固定的 8 dB 信噪比 A4 样本此前在 73 个稳定检测帧中有 6 帧输出错误低音，包括把
440 Hz 识别成约 147 Hz。现在，如果更早的 CMND 局部低谷与已接受低谷的差值不超过
`1966/65536`（约 0.03），且其周期是后者的整数约分周期，就拒绝这次候选；每个短
周期允许一个采样点的取整偏差。该帧的音高和置信度均归零，避免在含噪短周期和具有
丰富谐波的真实低音之间猜测。

修正后，样本中 67/73 个稳定检测帧仍在 440 Hz 附近，不再输出错误低音。这只代表
这组限定合成回归样本，不代表普遍抗噪保证。新增扫描复用原 CMND 数组，不增加内存
分配、状态、浮点 DSP 或采样差值计算。固件与原型采用相同判断。现有 UI 保持时长
（含调音器的 600 ms 保持）不变。

这是尚未发布的源码改动，不改变 v0.1.2 或 v0.2.0-preview.1 的冻结发布镜像及其
设备验收记录。实际麦克风噪声、C3 耗时、实体按键和听感仍需上板确认。这里的测试
不会打开串口或刷写固件。

## 完整 DSP 对拍

旧测试还覆盖音量、频谱、瀑布、起音、BPM 和和弦。先生成合成信号和浏览器参考：

```sh
python tests/mic_host/gen_signals.py
node tests/mic_host/record_golden.js prototype/index.html tests/mic_host/signals tests/mic_host/golden
python tests/mic_host/test_nabo_mic.py
```

生成参考另需 Playwright 和 Chromium，必要时用 `PW_MODULE` 指定 Playwright 包路径，
用 `PW_CHROME` 指定已安装的 Chromium 可执行文件。完整测试在没有信号样本时会明确
报错，空样本目录不会被算作测试通过。生成信号、浏览器参考和共享库均不提交 Git。
