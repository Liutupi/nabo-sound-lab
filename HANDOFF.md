# 当前交接

首次公开版本v0.1.2，冻结实机修订r4。Release镜像与本机通过回读的app/assets逐byte一致，未因发布重编；完整包boot测试边界见docs/VALIDATION_v0.1.2.md。

优先阅读README、docs/FLASHING.md、docs/VALIDATION_v0.1.2.md和docs/BUILD.md。固件职责：main负责硬件/持久化/音频线程，ui负责场景与声音，mic负责DSP，voice负责12音色，gfx负责直绘与资源。runtime素材在game-art/soundlab/sprites及firmware/assets。

音量独立NVS键vol；旧SET保持10B且不解释旧填充。目标音量通过音频线程应用，0%送全零PCM。静音不能把麦克风永久标成busy。首次变声表在实时音频任务前初始化。不要用重编二进制替换既有实机接受镜像的身份。

后续优先处理已知音高子谐波、历史异常复位原因及用户变声听感，再补重负载UI优化。公开摘要保留证据SHA，本机原始日志/环境拾音/备份不入库。公开源代码不含作者私有设备自动操作队列。
