# 无线对战预览版安装

适用 FoloToy AI Passport：ESP32-C3、8MB Flash、240×320。发布标签为 **v0.2.0-preview.1**，设备内部版本仍显示 **0.2.0-dev**：这是两块实物已测试的冻结镜像，没有为发布重新编译。两块必须安装同一版本。

先下载同一 Release 的完整 ZIP 或分立镜像，并按 SHA256SUMS.txt 校验。原设备完整备份留在自己电脑；不要上传备份，也不要使用别人的整片 Flash。

## 已安装声音实验室：保留设置升级

只适用于正在运行声音实验室、启动程序使用分区表偏移 **0x8000**、分区表 SHA256 为 `43311cbc95711332aa26b5e5002a58a9099b1a3be0b394408bc7b2b5dd986d86` 的工牌。

本次应用和字体素材均更新，**app 与 assets 必须一起升级**。不要使用其他玩法的地址；尤其不能凭 0x8000 恰好残留一张有效旧表就推断当前布局。Muse 等固件可能使用 0x10000 的另一张生效表。

下面为 esptool 4.x 示例。COMx 必须替换成实时核对芯片、MAC 和容量后的串口：

```powershell
python -m esptool --chip esp32c3 --port COMx flash_id
python -m esptool --chip esp32c3 --port COMx read_flash 0 0x800000 my-passport-backup.bin
Get-FileHash my-passport-backup.bin -Algorithm SHA256
python -m esptool --chip esp32c3 --port COMx read_flash 0x8000 0xC00 device-partition-table.bin
Get-FileHash device-partition-table.bin -Algorithm SHA256
# 确认当前正在运行声音实验室且布局相同后：
python -m esptool --chip esp32c3 --port COMx write_flash 0x10000 nabo-soundlab-passport.bin 0x200000 soundlab_assets.bin
```

升级只写 app/assets，不擦整片，也不写 boot、分区表、NVS 或 PHY。完成后回读目标区并逐 byte 或 SHA256 核对。

## 从 Muse、小智或其他玩法安装

使用 `nabo-soundlab-0.2.0-preview.1-merged.bin` 从 **0x0** 安装，选择 ESP32-C3、8MB。先备份并确认安全启动及 Flash 加密未启用；如已启用，不要套用这些通用镜像。

```powershell
python -m esptool --chip esp32c3 --port COMx flash_id
python -m esptool --chip esp32c3 --port COMx get_security_info
python -m esptool --chip esp32c3 --port COMx read_flash 0 0x800000 my-passport-backup.bin
Get-FileHash my-passport-backup.bin -Algorithm SHA256
python -m esptool --chip esp32c3 --port COMx write_flash 0x0 nabo-soundlab-0.2.0-preview.1-merged.bin
```

此安装替换旧玩法及该范围内的设置；合并镜像的 0x9000–0xFFFF 是空白填充，不能用它做“保留设置升级”。无需额外整片擦除。

新布局：boot 0x0，分区表 0x8000，NVS 0x9000/24KB，PHY 0xF000/4KB，app 0x10000/0x1F0000，assets 0x200000/0x600000。

实测覆盖：一块在相同布局升级 app，另一块从 Muse 通过分立 boot/table/app/assets 迁移，均完整 8MB 回读，后者在首启前验证非目标扇区。合并文件的组件和空白区已离线逐 byte 校验，**未将整个合并文件一次写入实物测试**；其验证范围与分立迁移分开记录。

## 两块怎么玩

1. 两块进入「玩 → 无线对战」。A 创建房间，B 寻找并选择相同四位房号。
2. A 按 OK 接受申请；看到准备提示后，双方按 OK。
3. 先数四拍，再看拍点到判定线按 OK。三轮为 90/105/120 BPM，累计分高者胜；每轮结束双方按 OK 继续。
4. ▼切换音效；静音仍可看画面跟拍。双击▲退出，断联约八秒本局作废，不自动判对手输。

无需路由器、互联网、账号、唱歌或绝对音感。对战内临时开启 ESP-NOW 信道6；退出后释放无线。单机玩法仍离线，设置里可调总音量。

12局36轮真实双机串口受控测试已通过，含连续10局、轮换房主、静音、赢/输/平和主动断联。实体按键手感、视听同步及距离测试待验，因此本次为预览版。既有含噪音高子谐波、历史异常复位原因、变声爆音主观听感仍未关闭。
