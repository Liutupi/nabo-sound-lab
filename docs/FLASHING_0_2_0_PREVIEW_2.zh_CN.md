<p align="right"><strong>简体中文</strong> · <a href="FLASHING_0_2_0_PREVIEW_2.md">English</a></p>

# 安装v0.2.0-preview.2

目标为ESP32-C3、8MB Flash，内部显示 **0.2.0-dev** 属正常现象。
只使用同一Release的附件，先核对`SHA256SUMS.txt`。
ZIP内含`tools/verify_preview_release.py`，可对完整下载附件目录检查冻结二进制及ZIP内容：

```sh
python tools/verify_preview_release.py /path/to/downloaded-assets
```

## 已安装声音实验室：保留设置升级

确认当前正在运行的启动程序使用下表布局，不能只凭某地址残留有效分区表推断。
`0x8000`的分区表SHA-256应为
`43311cbc95711332aa26b5e5002a58a9099b1a3be0b394408bc7b2b5dd986d86`。
同时确认实时串口、芯片身份和安全配置。以下命令适用esptool 4.x，将`PORT`替换为核实的串口。

preview.1或其他兼容版本，若素材SHA-256为
`5fc895c4ef5ef14a0aa90f40ca027d75806c40f4e7a44860f8538d1d5fa61edb`，
**只更新应用**：

```sh
python -m esptool --chip esp32c3 --port PORT write_flash --flash_mode keep --flash_freq keep --flash_size keep 0x10000 nabo-soundlab-passport.bin
python -m esptool --chip esp32c3 --port PORT verify_flash 0x10000 nabo-soundlab-passport.bin
```

如果生效布局一致但素材较旧（例如稳定版v0.1.2），还需在`0x200000`更新`soundlab_assets.bin`。
保留设置升级不要从`0x0`写整合镜像；两种分段方式均无需整片擦除。
应用和可选素材更新不写NVS/PHY、启动程序及分区表。

用户工牌使用的就是本版只升级应用流程。刷写前后受保护的`0x0–0xFFFF`逐字节一致，
包含当前设置及游戏纪录。刷写不以整份旧固件回读为前提。

## 首次安装或原固件布局不兼容

完整文件为 **nabo-soundlab-0.2.0-preview.2-merged.bin**，从 **0x0** 写入。
它会替换原玩法并覆盖其范围内的存储数据，需保留的数据另行保存。
启用安全启动或Flash加密的设备，需使用对应的安全配置流程，不能直接套用通用镜像。

```sh
python -m esptool --chip esp32c3 --port PORT flash_id
python -m esptool --chip esp32c3 --port PORT get_security_info
python -m esptool --chip esp32c3 --port PORT write_flash 0x0 nabo-soundlab-0.2.0-preview.2-merged.bin
```

整合镜像含空白NVS/PHY填充，组件和布局已经离线校验；本版未做整文件首次安装实测。
已测试的保留设置升级沿用原启动程序；ZIP里的启动程序来自匹配的新构建。

| 组件 | 地址 | 分区大小 |
| --- | --- | --- |
| 启动程序 | 0x0 | 分区表之前 |
| 分区表 | 0x8000 | 镜像0xC00 |
| NVS | 0x9000 | 0x6000 |
| PHY | 0xF000 | 0x1000 |
| 工厂应用 | 0x10000 | 0x1F0000 |
| 素材 | 0x200000 | 0x600000 |

安装后在舒适的主音量下测试复读录音、换声音及OK停止。音量调整时▲减、▼加。
验证范围及限制见[发布说明](RELEASE_v0.2.0-preview.2.zh_CN.md)。
