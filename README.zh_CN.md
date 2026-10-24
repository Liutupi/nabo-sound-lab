<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# NABO 声音实验室

戴上耳机，和 NABO 一起玩音乐。用于 FoloToy AI Passport 工牌的独立离线固件，ESP32-C3 / 8MB Flash / 240×320 / ES8311。

**当前预览版：v0.2.0-preview.2 · 复读音质与按键反馈优化。** 内部版本仍显示0.2.0-dev，发布的是用户工牌已验收的冻结应用：小声复读更响、变声更清晰，录放中可换声音/模式，OK停播，音量可双向调整。NABO造型和配色保持。单机离线，对战内临时启用ESP-NOW；不包含小智聊天功能。

[下载preview.2](https://github.com/Liutupi/nabo-sound-lab/releases/tag/v0.2.0-preview.2) · [安装说明](docs/FLASHING_0_2_0_PREVIEW_2.zh_CN.md) · [发布说明](docs/RELEASE_v0.2.0-preview.2.zh_CN.md) · [稳定版v0.1.2](https://github.com/Liutupi/nabo-sound-lab/releases/tag/v0.1.2) · [构建说明](docs/BUILD.zh_CN.md)

见[操作检查](tests/interaction_host/README.zh_CN.md)、[音质检查](tests/voice_host/README.zh_CN.md)及[音高抗误判](tests/mic_host/README.zh_CN.md)。新应用完成单工牌实测并获用户接受；整合镜像安装及本版双机RF/距离回归待验，复杂复读动画仍有慢帧。下方历史冻结版保留各自验证范围。

开发分支新增「玩 → 无线对战」：两块工牌直连，同一谱面，四拍预备后看拍点按OK，三轮比节奏。不唱歌、不辨音高，静音也能玩。[可玩双屏原型](prototype/wireless-duel.html) · [规则与开发版验收](docs/WIRELESS_DUEL.md)。0.2.0-dev与正式v0.1.2分开，设备验证状态见[开发记录](docs/VALIDATION_0_2_0_DEV.json)。

2026-10-24已在两块真实C3完成12局、36轮受控联机测试，含连续10局、轮换房主和静音得分，约30FPS；突然断联后正确作废。[双机实测总览](docs/images/duel-device-real-rf-overview.png)。实体按键手感、音效同步和距离测试待验，正式发布仍为v0.1.2。

![NABO 音乐人](docs/images/nabo-musician.png)

## 可以玩什么

| 分组 | 内容 |
|---|---|
| 双机对战（预览） | 四位房号直连、看拍点按OK、三轮累计、音效可关、断联作废 |
| 玩声音 | 12种变声复读、喊叫跑酷、吹蜡烛、长音走钢丝、声部卡、哼歌飞行、拍手节奏、采样琴 |
| 看声音 | 频谱仪、声纹瀑布、示波器、和弦环、分贝计 |
| 练音乐 | 调音器、吉他工具箱、节拍检测、音准曲线 |
| 设置 | 音量、A4基准、减弱闪烁、安静阈值、dB校准、频谱灵敏度 |

首页使用与 NABO 赛博算命机相同的“阳光小男孩”音色，说“那波，陪你玩音乐！”。音量0–100%，每档10%，0%静音，默认80%，自动保存。▲▼选择，OK操作，双击▲返回；每页底栏显示当前玩法操作。采样琴中三键用于弹奏，长按OK采样。当前源码中，音量行按OK进入调整，▲减、▼加，OK完成；双击▲先退出音量调整，再返回设置上级。

“NABO 音乐人”造型和全部道具沿用项目定稿；主要中文操作提示16px，设置数值22px。约五分钟无操作后休眠，实体OK唤醒。

## 固件下载与安装

下载v0.2.0-preview.2完整ZIP，按[安装说明](docs/FLASHING_0_2_0_PREVIEW_2.zh_CN.md)操作。兼容preview.1设备只需在0x10000更新app即可保留设置和纪录；较旧素材可能还需更新。整合镜像从0x0安装，会覆盖该范围内存储数据。内部显示0.2.0-dev为正常现象。

### 稳定版v0.1.2

首次安装下载 **nabo-soundlab-0.1.2-merged.bin**，从0x0写入，选择ESP32-C3和8MB Flash。此安装会替换原有玩法，先保存需保留的数据。已有相同分区的声音实验室0.1.1/0.1.2，按刷机说明只升级app和assets以保留设置。不要把其他玩法的分区地址直接套用到本项目。

Release 同时提供完整ZIP、分立镜像、manifest.json和SHA256SUMS.txt。发布的是已验证的冻结二进制，未为发布重新编译。当前实物验证覆盖r4 app和assets；实物保留旧boot，完整包包含匹配构建输出的新boot，未另做整包首次安装的端到端实测。

## 稳定版v0.1.2的实测范围

0/40/80%经三次受控芯片重启后读取一致；12种变声录放完成，音频API读写错误0；首录处理峰值0.680ms。四个正式性能窗口30.30FPS、慢帧0；实际变声演出仍有慢帧。

仍开放：含噪440Hz输入曾误判为约147Hz；旧修订出现过一次无人操作异常返回/重启，原因未定位，后续观察未复现；新版变声是否还存在爆破声，尚待实物听感确认。本固件不作专业调音或声级测量精度承诺。详情及数据边界见[验证记录](docs/VALIDATION_v0.1.2.md)。

![实际C3设置页九状态总览](docs/images/settings-overview.png)

以上为C3实际送屏缓冲，原尺寸240×320，非实物面板光学照片；总览底色#0A0716，各帧采集时点不同。

## 源码结构

| 目录 | 内容 |
|---|---|
| firmware/ | 应用、BSP、配置、已冻结素材包 |
| prototype/ | 浏览器原型；音量交互已同步，旧占位造型不作为固件美术验收依据 |
| game-art/soundlab/ | 定稿造型、道具、起始图、38段源视频、运行时精灵及字体 |
| tools/、tests/ | 素材工具和宿主测试源码 |
| docs/ | 刷机、构建、实测、发布说明与原尺寸截图 |

本机设备备份、NVS、环境录音、原始设备日志、生成服务任务元数据和缓存未上传。源码MIT；NABO形象和美术不随MIT授权，见[NOTICE](NOTICE.md)及[第三方说明](THIRD_PARTY_NOTICES.md)。本项目与FoloToy官方无关联。

## 社区展示材料

[五张宣传图与玩法介绍](docs/community/v0.1.2/README.md)。已提交AI Passport玩法社区审核，状态与固件验证范围见材料说明。
