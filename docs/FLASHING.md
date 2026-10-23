# v0.1.2 安装与升级

适用：FoloToy AI Passport，ESP32-C3，8MB Flash，240×320竖屏。先下载同一Release的文件并核对SHA256SUMS.txt；不要混用其他版本app/assets。

## 首次安装

下载nabo-soundlab-0.1.2-merged.bin。使用支持ESP32-C3的网页/串口烧录工具，选择实际连接的设备、8MB Flash，将该文件从0x0写入。首次安装会替换现有玩法；先备份自己的设备，不使用别人的整片备份。

包中NVS及PHY区域为空白填充，不含任何实物设备备份。当前实机验证是从相同分区旧版升级app和assets；完整包的新构建boot尚未另做首次安装的端到端实测。

命令行示例使用esptool 4.x语法；COMx必须替换为实时枚举确认的工牌串口。先用flash_id核对芯片、MAC和8MB容量，再读整片备份：

```powershell
python -m esptool --chip esp32c3 --port COMx flash_id
python -m esptool --chip esp32c3 --port COMx read_flash 0 0x800000 my-passport-backup.bin
```

确认备份完整、SHA256已记录后，首次安装命令：

```powershell
python -m esptool --chip esp32c3 --port COMx write_flash 0x0 nabo-soundlab-0.1.2-merged.bin
```

## 相同分区升级

仅适用于已安装声音实验室且分区表SHA256为43311cbc95711332aa26b5e5002a58a9099b1a3be0b394408bc7b2b5dd986d86的工牌。先备份、读取并核对原分区表；不同就不要执行本节升级。

| 区域 | 地址 | 大小 |
|---|---|---|
| NVS | 0x9000 | 0x6000 |
| PHY | 0xF000 | 0x1000 |
| app | 0x10000 | 0x1F0000 |
| assets | 0x200000 | 0x600000 |

```powershell
python -m esptool --chip esp32c3 --port COMx read_flash 0x8000 0xC00 device-partition-table.bin
Get-FileHash device-partition-table.bin -Algorithm SHA256
python -m esptool --chip esp32c3 --port COMx write_flash 0x10000 nabo-soundlab-passport.bin 0x200000 soundlab_assets.bin
```

升级只写app和assets，不写boot、分区表、NVS或PHY；不要整片擦除。升级后回读两份目标区并逐byte/SHA核对，保留原设置。备份和回读都是自己的私有设备资料，不要公开上传。

## 开机与音量

开机首页会说“那波，陪你玩音乐！”。设置第一行音量0–100%，OK每次加10%，100%后回0%；0%真正静音。停留约1秒后保存，重启记忆；默认80%。双击▲返回。约五分钟无操作后休眠，实体OK唤醒。

若画面、声音或按键异常，记录玩法和触发条件，对照本版已知问题；不同分区/旧版镜像不要交叉拼接。
