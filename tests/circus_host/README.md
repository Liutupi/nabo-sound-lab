# 声音挑战赛宿主测试

```sh
python -m pip install numpy Pillow
CC=gcc NABO_UBSAN=1 python tests/circus_host/test_circus.py
```

需要 Python 3.10+、原生 C 编译器（`CC`，默认 `cc`），C/JS 对拍另需 Node.js（找不到时跳过并说明）。

- `test_model.c`：`circus_model.c` 的规则单元测试，`--trace` 输出两整局的状态快照。
- `trace.js`：用 `prototype/circus-model.js` 跑同一脚本，快照必须与 C 完全相同。
- `test_circus.py`：编译并运行以上内容，检查故意改坏的规则会被拒绝；再把真实 UI、麦克风分析、混音和渲染器编成共享库，用合成声音打完一整局，逐帧检查缺字、文字越界、显示列表容量、增量与整屏像素一致。截图写入被忽略的 `out/circus-host/`。

测试只在宿主机运行，不打开串口，不刷机，不写资源。合成的吹气、哼唱和拍手不能代替真实麦克风验收。规则与待验证项目见 [docs/SOUND_CHALLENGE.md](../../docs/SOUND_CHALLENGE.md)。
