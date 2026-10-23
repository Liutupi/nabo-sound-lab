# NABO 声音实验室 v0.1.2 · 音乐人

首次公开固件，保留当前实机版本号0.1.2，修订r4。适用于FoloToy AI Passport（ESP32-C3 / 8MB / 240×320），独立离线运行。

- 12种变声、喊叫跑酷、吹蜡烛、走钢丝、声部卡，以及哼歌飞行、拍手节奏、采样琴。
- 声音可视化、调音器、吉他工具、节拍检测与音准曲线。
- 首页同赛博算命机音色：“那波，陪你玩音乐！”；0–100%可保存音量，0%静音，默认80%。
- 定稿NABO音乐人造型，主要中文提示16px、设置数值22px。

## 下载

第一次安装：nabo-soundlab-0.1.2-merged.bin，从0x0写入；或下载完整ZIP阅读说明。已有同分区声音实验室的升级只写app0x10000和assets0x200000以保留设置。先备份自己的工牌，核对SHA256SUMS.txt，详见[刷机说明](https://github.com/Liutupi/nabo-sound-lab/blob/v0.1.2/docs/FLASHING.md)。

本次发布复用冻结二进制，未重新编译。r4 app/assets实机写入及8MB回读完全一致；音量0/40/80重启保存和12种变声录放通过。实际旧boot保留，merged内构建boot未另做首次安装端到端实测。

## 已知问题

- 含噪440Hz曾被误判为约147Hz，不能作为专业调音精度保证。
- 历史r1无人返回菜单/重启原因仍未定位；后续观察未复现不等于修复。
- 变声演出部分UI慢帧；新版是否仍有主观爆破音尚待实听反馈。
- 宿主/串口检查不代替实体听感和完整场景体验，具体范围见[验证记录](https://github.com/Liutupi/nabo-sound-lab/blob/v0.1.2/docs/VALIDATION_v0.1.2.md)。

app SHA256：`fa93210ee93d5e569289c8ed4e87615ca339b3e63266678e0acb5f2480f39869`。

![实际C3设置页总览](https://github.com/Liutupi/nabo-sound-lab/raw/v0.1.2/docs/images/settings-overview.png)

非官方社区项目，与FoloToy官方无关联。源码MIT，NABO形象/美术/动画/语音不随MIT授权；不含备份、NVS、环境录音或设备日志。
