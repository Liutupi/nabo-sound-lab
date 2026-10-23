# 当前交接

最新预览发布为v0.2.0-preview.1，使用原样冻结的0.2.0-dev内部镜像；入口见docs/RELEASE_v0.2.0-preview.1.md与docs/FLASHING_0_2_0_PREVIEW.md。正式稳定版仍为v0.1.2。预览版不将用户暂缓的实体手感、视听同步和距离验收标记为完成。

首次公开版本v0.1.2，冻结实机修订r4。Release镜像与本机通过回读的app/assets逐byte一致，未因发布重编；完整包boot测试边界见docs/VALIDATION_v0.1.2.md。

优先阅读README、docs/FLASHING.md、docs/VALIDATION_v0.1.2.md和docs/BUILD.md。固件职责：main负责硬件/持久化/音频线程，ui负责场景与声音，mic负责DSP，voice负责12音色，gfx负责直绘与资源。runtime素材在game-art/soundlab/sprites及firmware/assets。

音量独立NVS键vol；旧SET保持10B且不解释旧填充。目标音量通过音频线程应用，0%送全零PCM。静音不能把麦克风永久标成busy。首次变声表在实时音频任务前初始化。不要用重编二进制替换既有实机接受镜像的身份。

后续优先处理已知音高子谐波、历史异常复位原因及用户变声听感，再补重负载UI优化。公开摘要保留证据SHA，本机原始日志/环境拾音/备份不入库。公开源代码不含作者私有设备自动操作队列。

0.2.0-dev开发：用户选择两工牌无线对战，并确认可轮流接USB。先读docs/WIRELESS_DUEL.md和docs/VALIDATION_0_2_0_DEV.json。prototype/duel-rules.js与firmware/main/ui/duel_rules.c须同步；duel_model.c为44B协议和有界状态机，link/sd_radio.c只管平台。sc_duel.c负责画面/动作/状态，实体键通过sl_ui_key_at保留事件时间。无线入口只在SOUNDLAB_WIRELESS_DUEL构建启用，使用firmware/tools/build-duel.ps1；build-duel/sdkconfig.duel与原build/sdkconfig隔离。默认普通构建不启无线。退出的离开包尽力发送，丢失后对方8秒超时作废。旧0.1.2 Release/tag和社区项目1538保持原样。双机射频与实体时序须按开发验收记录确认；宿主网络仿真不算双机验收。

2026-10-24：当前冻结修订为fragmented-sprite-cache-v3，app SHA256为5d8440c6494d2464b379c49f0af31e12e658e4e2c80ace6e058325c1308c4d24。A已仅写app并完整8MB回读，保留原启动区、分区、NVS和素材。前一V2在退出无线后largest=13824，无法分配14400B的120px NABO，已撤回；V3优先连续缓存，失败时用两段行缓存，保留原像素。A无线进出10次后，复读120px和三人合唱120+76+76px正常，7张直接帧缓冲截屏逐行验收通过，见docs/images/duel-device-A-v3-overview.png。宿主旧玩法11613帧三种分配条件逐像素一致。

B原运行Muse，生效分区表在0x10000；0x8000另有历史旧表，不能只凭它判断当前布局。已完成两份一致8MB原机备份，换入配套V3 boot/table/app/assets，并在首次启动前完整回读，全部非目标扇区保留。原Muse活动NVS/PHY落在新app范围，已覆盖但可由完整备份恢复，不可声称设置原地保留。B新启动程序、应用、无线初始化及退出后的120px复读画面已过。

两块同时接USB后，真实ESP-NOW完成12局36轮（含连续10局、双方轮换房主、三轮静音得分与赢/输/平结算）。72个设备轮性能窗口30.298–30.313FPS，最大渲染27ms，无>33ms帧、音频错误、意外重启或正常对局RF drop/fail。另做A主动进ROM的硬断联，B在命令起约8.281秒/ROM确认后约7.578秒显示作废，作废文字区与冻结参考逐像素一致；A随后正常重启，设置值保留。断联期间预期出现的ESP_FAIL不混入正常对局统计。总览见docs/images/duel-device-real-rf-overview.png。以上为串口代按；用户选择“现在先不试玩”，实体手感/音效同步/距离尚待验，私有监听已停止，不替换正式v0.1.2。
