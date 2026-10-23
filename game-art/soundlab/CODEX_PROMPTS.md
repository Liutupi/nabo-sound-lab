# NABO 声音实验室 · Codex 提示词（按轮次）

每一轮开头都先贴这段：

> 先读 `game-art/soundlab/ART_BRIEF.md`，严格遵守 NABO 身份规则：以 `reference/nabo-idle.png` 为准，脸、眼镜、发型、白帽和帽标、比例都不能改；帽标不能镜像；耳机不能挡住帽标。画风是平涂加两阶赛璐珞阴影，深色外描边。输出放到 ART_BRIEF 第 8 节规定的目录里。

---

## 第 1 轮 · “NABO 音乐人”造型（用户选定后再进入下一轮）

> 以 `reference/nabo-idle.png` 为基础，按 ART_BRIEF 第 3 节出 3 个方案，存为 `costume/A.png`、`B.png`、`C.png`，都是半身正面、纯色背景、1024×1024，**都戴着头戴式大耳机**（耳罩外侧一圈青色霓虹灯，头梁从帽子后侧绕过，帽标完整可见）。
> - A 街头 DJ：宽松深靛连帽卫衣，胸前小频谱柱图案，品红耳罩。
> - B 摇滚吉他手：黑色短款机车夹克，品红 T 恤，斜挎金色吉他背带。
> - C 录音棚：深紫飞行员夹克，青色袖口罗纹，胸前金色小麦克风徽章。
> 每个方案另出 3 张预览：缩到 120×120、76×76、56×56，放在 #0A0716 底上，文件名加 `-120`、`-76`、`-56`。最后出一张三方案并排的对比图 `costume/costume-comparison.png`。

## 第 2 轮 · 道具（先于起始图，保证一致）

> 按 ART_BRIEF 第 7 节第一行画 6 件手持道具：`props/guitar`、`mic`、`drumsticks`、`pole`、`umbrella`、`party-hat`。每件先画 4 倍尺寸，再缩到在 76 px 角色手里的实际大小，两个版本都交。背景透明，硬边加深色描边，配色用 ART_BRIEF 第 3 节的主色。
> 吉他是小号木吉他（金色面板、品红护板），平衡杆是细长的青色杆子、两端金色配重，派对帽是品红底加金色波点。

## 第 3 轮 · Grok 起始图（分 3 批）

> 用定稿造型 `costume/final.png` 和第 2 轮道具，为 ART_BRIEF 第 6 节的每个片段画起始姿势，存到 `grok-start/<ID>.png`，1024×1024，纯色背景。
> **同一视角的图里，镜头位置和人物大小保持一致**：所有正面半身图头顶到画面上沿的距离相同；所有全身图（走钢丝、伞）脚底在同一高度；所有侧面图（跑、跳、掉）脚底在同一高度、人物朝右。
> - 第 1 批（P0 正面半身）：S-IDLE、S-WELCOME（耳机挂在脖子上）、S-MIC-IDLE、S-LISTEN、S-TALK、S-OOPS、S-CAKE-WAIT（戴派对帽，派对帽在白帽侧边，不挡帽标）、S-CHEER、S-SING-LOW、S-SING-HIGH、S-DJ、S-SLEEP、S-EARS、S-SHUSH、S-GUITAR-IDLE、S-GUITAR-STRUM、S-GUITAR-TUNE、S-OK-SIGN、S-WOW、S-NOD、S-DRUM。
> - 第 2 批（P0 全身和侧面）：S-RUN-R、S-JUMP-R、S-FALL-R（侧面朝右全身）；S-ROPE-READY、S-ROPE-WALK、S-ROPE-FALL（正面全身，双手横握平衡杆）。
> - 第 3 批（P1）：其余片段。
> 每批交付时附一张缩略总览图（全部缩到目标尺寸，放在 #0A0716 底上）。画面里不能出现文字。

## 第 4 轮 · 静态素材

> 按 ART_BRIEF 第 7 节其余各行画静态素材，先画 4 倍再缩到目标尺寸，存到 `props/`。
> 文件名：`title`、`group-play`、`group-see`、`group-practice`、`icon-<项目英文名>`（echo、shout、candle、rope、range、spectrum、waterfall、scope、chord、db、tuner、guitar、beat、pitch、settings）、`head-24`、`ground-top`、`ground-fill`、`rope-post`、`cake`、`candle-a`、`candle-b`、`card-frame`、`card-bass`、`card-baritone`、`card-tenor`、`card-alto`、`card-mezzo`、`card-soprano`、`stamp-1`…`stamp-5`。
> 印章、卡面、图标上都不画文字（文字由引擎绘制），只有 `title` 例外。

## 第 5 轮 · 收尾

> 1. 检查所有素材在 #0A0716 底上的效果，列出不合格的项目并修正。
> 2. 生成 `anchors.json`：S-ROPE-*、S-RUN-R、S-JUMP-R、S-FALL-R 的脚底中心坐标（以目标尺寸为准）。
> 3. 不改动原型和固件代码。
