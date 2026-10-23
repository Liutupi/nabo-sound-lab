# NABO 声音实验室 · Grok 图生视频提示词

**用法**：输入图是 Codex 第 3 轮出的 `grok-start/<ID>.png`；导出的视频命名为 `grok-clips/<ID>.mp4`。
每段视频都附上下面这段通用约束：

> Locked static camera, no zoom, no pan, no camera shake. Keep the character's face, glasses, hairstyle, white cap, the "NABO" text on the cap, the headphones and the outfit exactly the same in every frame; never mirror the cap text; the headphones never cover the cap text. Flat cel-shaded animation style with dark outlines, no motion blur, no lighting changes. Plain solid background, unchanged throughout. No text, no particles. Any handheld prop stays in the hands and keeps its exact shape.

> 中文说明：镜头完全固定；人物、耳机、服装不能漂移；不要动态模糊；背景纯色不变；手里的道具不能变形，也不能飞出手。

每段建议生成 2–3 个版本，选人物最稳的一版。时长比清单多留 1 s 余量，抽帧时再裁剪。先做 P0。

---

## 开机与通用

| ID | 时长 | 提示词 |
|---|---|---|
| **S-IDLE** | 4 s | Wearing big headphones, the character gently bobs the head to an unheard beat, blinks twice, smiling. Returns to the exact starting pose. Seamless loop. |
| **S-WELCOME** | 3.5 s | The headphones start around the neck. The character lifts them onto the ears with both hands, does a quick two-handed DJ scratch motion in front of the chest (no turntable visible), then waves at the camera with a big smile and lowers the hand. |
| **S-WOW** | 2 s | Eyes widen with delight, both hands fly up in celebration, then come back down with a big smile. |
| **S-NOD** | 1.5 s | Smiles and nods twice approvingly, then returns to the starting pose. |
| **S-OOPS** | 2 s | Scratches the back of the head with one hand, sticks the tongue out a little and gives an embarrassed grin, then lowers the hand. |

## 复读 NABO

| ID | 时长 | 提示词 |
|---|---|---|
| **S-MIC-IDLE** | 3 s | Holding a microphone near the mouth, the character tilts the head curiously, waiting, blinking once. Seamless loop. |
| **S-LISTEN** | 3 s | One hand cupped behind the headphone ear cup, the character leans forward to listen closely, eyes wide and focused, with tiny nods. Seamless loop. |
| **S-TALK** | 3 s | Holding the microphone, the character talks into it with clear, exaggerated mouth movements, head bobbing slightly with expression. Seamless loop. |
| **S-LOWBAT** | 4 s | Holding the microphone and talking, the character gets slower and droopier, eyelids falling, shoulders sagging, and finally slumps forward onto the arms as if out of battery. |

## 喊叫 NABO（侧面朝右）

| ID | 时长 | 提示词 |
|---|---|---|
| **S-SHOUT** | 2.5 s | Both hands cupped around the mouth like a megaphone, the character shouts with the mouth wide open, leaning forward a little. Seamless loop. |
| **S-RUN-R** | 2 s | Side view facing right, full body. The character jogs in place with lively arm swings, feet staying on the same ground line. Seamless loop. |
| **S-JUMP-R** | 2 s | Side view facing right, full body. The character crouches, springs up, tucks the legs in mid-air, then lands back on the same spot with a small squash. |
| **S-FALL-R** | 2 s | Side view facing right, full body. The character flails arms and legs in panic as if falling, eyes and mouth wide open. The body stays in the same place in frame. Seamless loop. |

## 吹蜡烛

| ID | 时长 | 提示词 |
|---|---|---|
| **S-CAKE-WAIT** | 3 s | Wearing a small party hat beside the white cap, the character rests the chin on both hands, eyes sparkling, looking down to the lower right, swaying gently. Seamless loop. |
| **S-BLOW** | 2 s | The character puffs out the cheeks and blows hard toward the lower right, then takes a quick breath and blows again. Seamless loop. |
| **S-CHEER** | 2.5 s | The character claps happily a few times, then throws both hands up in a cheer and returns to the starting pose. |
| **S-SING-BDAY** | 3 s | Eyes closed, the character sways the head side to side, singing happily. Seamless loop. |
| **S-THUMB** | 2.5 s | The character glances at the wrist as if checking a watch, then gives a thumbs-up with a confident smile. |

## 走钢丝（正面全身，握平衡杆）

| ID | 时长 | 提示词 |
|---|---|---|
| **S-ROPE-READY** | 3 s | Full body, holding a long balancing pole horizontally with both hands, the character stands steady and breathes calmly. Feet stay on the same spot. Seamless loop. |
| **S-ROPE-WALK** | 2 s | Full body, holding the pole horizontally, the character takes small careful steps in place, feet staying on one line. Seamless loop. |
| **S-ROPE-WOBBLE** | 2 s | Full body, holding the pole, the character struggles to keep balance, one leg lifted, arms adjusting quickly, mouth open in a nervous grin. Feet anchor stays in place. Seamless loop. |
| **S-ROPE-FALL** | 2 s | Full body, the character loses balance, lets go of the pole with a surprised face and starts to drop. Keep the body in frame. |
| **S-ROPE-BOW** | 2.5 s | Full body, the character hops once happily, then bows politely holding the pole at the side. |

## 声部卡

| ID | 时长 | 提示词 |
|---|---|---|
| **S-SING-LOW** | 3 s | Chin tucked down, one hand on the chest, the character sings a deep low note with a serious, rich expression. Seamless loop. |
| **S-SING-HIGH** | 3 s | Rising onto the toes, head tilted back, one hand raised high, the character belts a high note with full effort. Seamless loop. |

## 可视化

| ID | 时长 | 提示词 |
|---|---|---|
| **S-DJ** | 3 s | Both hands make DJ scratching motions in front of the chest (no turntable visible), head nodding to the beat. Seamless loop. |
| **S-FLOAT** | 4 s | Lying on the back sideways in frame, hands behind the head, the character relaxes and swings one foot lazily. Seamless loop. |
| **S-UMBRELLA** | 3 s | Full body, holding an umbrella overhead, the character looks up curiously, shifting weight slightly. Seamless loop. |
| **S-HEADPHONE** | 3 s | Both hands pressing the headphones, eyes closed, the character listens intently with tiny nods. Seamless loop. |
| **S-SLEEP** | 4 s | The character dozes off, the head slowly drooping and jerking back up, a sleepy smile. Seamless loop. |
| **S-EARS** | 2.5 s | Both hands cover the ears over the headphones, eyes squeezed, frowning, shaking the head slightly. Seamless loop. |
| **S-SHUSH** | 2.5 s | The character puts one index finger to the lips and makes a "shh" face, serious but cute, then lowers the hand. |

## 吉他与节拍

| ID | 时长 | 提示词 |
|---|---|---|
| **S-GUITAR-IDLE** | 4 s | Holding a small acoustic guitar, the character nods along and lightly plucks a string once. Seamless loop. |
| **S-GUITAR-STRUM** | 1.5 s | Holding the guitar, the character does one strong downward strum with a happy face, then returns the hand to the starting position. |
| **S-GUITAR-TUNE** | 3 s | Holding the guitar, the character turns a tuning peg on the headstock while tilting the head to listen. Seamless loop. |
| **S-OK-SIGN** | 2 s | The character makes an OK hand sign next to the face and winks, then lowers the hand. |
| **S-THINK** | 3 s | Chin resting on one hand, eyes looking up, the character thinks hard. Seamless loop. |
| **S-DRUM** | 1.5 s | Holding drumsticks, the character hits once downward toward the bottom edge of the frame (the drum is out of frame), then returns the sticks to the starting position. |

## 验收（抽帧后由引擎检查）

- 逐帧检查人物大小和位置：头部中心漂移不超过 2 px（按目标尺寸）；全身和侧面片段的脚底中心漂移不超过 2 px。
- 循环片段首尾帧差异超过阈值就重做。
- 帽标字样清楚，没有变形或镜像；耳机没挡住帽标。
- 背景纯色不变，抠图后没有残边；道具没有变形。
