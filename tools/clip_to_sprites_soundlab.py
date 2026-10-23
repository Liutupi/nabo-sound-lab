"""NABO 声音实验室 · Grok 视频 → 精灵帧（选段、重定时、抠绿、根锚、64 色、RLE+差分、逐帧回解）

python tools/clip_to_sprites_soundlab.py            # 处理全部 38 段
python tools/clip_to_sprites_soundlab.py S-IDLE     # 只处理一段

输入：game-art/soundlab/CLIP_SELECTION.json（选片权威：源文件路径、SHA256、候选区间、目标尺寸、是否循环）
      时长按 ART_BRIEF.md 第 6 节；S-ROPE-FALL 只用原生第 6–13 帧（Codex 验收条件）
输出：game-art/soundlab/sprites/<ID>/<尺寸>/frames.bin（FNBO v1）、sheet.png（透明精灵表）
      game-art/soundlab/sprites/manifest.json（源帧映射、重定时、根锚处理、循环跨接、回解与色差、字节数）
      game-art/soundlab/previews/<ID>.gif（#0A0716 底，3 倍放大连播）与 overview.png
"""
import hashlib, json, os, struct, subprocess, sys
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART = os.path.join(ROOT, 'game-art', 'soundlab')
OUT = os.path.join(ART, 'sprites')
PREV = os.path.join(ART, 'previews')
BG = np.array([0x0A, 0x07, 0x16], np.float32)     # 屏幕底色 C_BG，半透明边缘预先混合到这个颜色
OUT_FPS = 12
HOLD = 30   # 颜色差（RGB 三通道绝对差之和）不超过此值的像素沿用上一帧（去视频闪烁）

# 目标时长（秒，ART_BRIEF 第 6 节）；跳跃去根使用外部人工腰髋注册数据，只改确认离地阶段。
SPEC = {
    'S-IDLE': 3, 'S-WELCOME': 2.5, 'S-MIC-IDLE': 2, 'S-LISTEN': 2, 'S-TALK': 2, 'S-LOWBAT': 3, 'S-SHOUT': 1.5,
    'S-RUN-R': 0.7, 'S-JUMP-R': 1, 'S-FALL-R': 1, 'S-OOPS': 1.2, 'S-CAKE-WAIT': 2, 'S-BLOW': 1, 'S-CHEER': 1.5,
    'S-SING-BDAY': 2, 'S-THUMB': 1.5, 'S-ROPE-READY': 2, 'S-ROPE-WALK': 1, 'S-ROPE-WOBBLE': 1, 'S-ROPE-FALL': 1,
    'S-ROPE-BOW': 1.5, 'S-SING-LOW': 2, 'S-SING-HIGH': 2, 'S-DJ': 2, 'S-FLOAT': 3, 'S-UMBRELLA': 2, 'S-HEADPHONE': 2,
    'S-SLEEP': 3, 'S-EARS': 1.5, 'S-SHUSH': 1.5, 'S-GUITAR-IDLE': 3, 'S-GUITAR-STRUM': 0.6, 'S-GUITAR-TUNE': 2,
    'S-OK-SIGN': 1, 'S-THINK': 2, 'S-WOW': 1.2, 'S-NOD': 1, 'S-DRUM': 0.4,
}
ROOT_REGISTERED = {'S-JUMP-R', 'S-ROPE-BOW'}
ROOT_REGISTRATION = os.path.join(ART, 'root-registration', 'body-root-registration-v1.json')
FULL_BODY = {'S-RUN-R', 'S-JUMP-R', 'S-FALL-R', 'S-ROPE-READY', 'S-ROPE-WALK', 'S-ROPE-WOBBLE', 'S-ROPE-FALL', 'S-ROPE-BOW', 'S-UMBRELLA'}
# 固件额外要的尺寸（nabo() 找不到会缩放其他尺寸，这里直接给好）
EXTRA_SIZES = {'S-GUITAR-IDLE': [56], 'S-GUITAR-STRUM': [56], 'S-OOPS': [56], 'S-TALK': [76], 'S-LOWBAT': [76], 'S-LISTEN': [76], 'S-MIC-IDLE': [76]}
DECODE = 360   # 源 720 先缩到 360 处理（目标最大 120，足够）


def probe(path):
    p = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries',
                                            'stream=width,height,r_frame_rate,nb_frames', '-of', 'json', path]))['streams'][0]
    n, d = p['r_frame_rate'].split('/')
    return p['width'], p['height'], int(n) / int(d)


def decode(path):
    cmd = ['ffmpeg', '-v', 'error', '-i', path, '-map', '0:v:0', '-vf', 'scale=%d:%d:flags=area' % (DECODE, DECODE),
           '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-']
    raw = subprocess.check_output(cmd)
    return np.frombuffer(raw, np.uint8).reshape(-1, DECODE, DECODE, 3).astype(np.float32)


def key_green(frames):
    """估计绿底颜色（四边像素中位数），按色距生成软遮罩，再去绿边。青色耳机灯圈 G≈B，不会被当成绿幕。"""
    edge = np.concatenate([frames[:, :4].reshape(-1, 3), frames[:, -4:].reshape(-1, 3),
                           frames[:, :, :4].reshape(-1, 3), frames[:, :, -4:].reshape(-1, 3)])
    g = np.median(edge, axis=0)
    d = np.sqrt(((frames - g) ** 2).sum(-1))
    dom = frames[..., 1] - np.maximum(frames[..., 0], frames[..., 2])
    alpha = np.clip((d - 60) / 70, 0, 1)
    alpha = np.where(dom > 70, 0.0, alpha)
    spill = (alpha < 1) | (dom > 18)
    rgb = frames.copy()
    lim = np.maximum(rgb[..., 0], rgb[..., 2]) + 8
    rgb[..., 1] = np.where(spill, np.minimum(rgb[..., 1], lim), rgb[..., 1])
    return rgb, alpha.astype(np.float32), g


def feet_row(a):
    """脚底行：alpha>0.5 且这一行至少 3 个像素的最低行（处理分辨率坐标）。"""
    rows = np.nonzero((a > 0.5).sum(1) >= 3)[0]
    return int(rows[-1]) if len(rows) else a.shape[0] - 1


def shift_v(img, dy):
    out = np.zeros_like(img)
    if dy > 0: out[dy:] = img[:-dy]
    elif dy < 0: out[:dy] = img[-dy:]
    else: out[:] = img
    return out


def shift_xy(img, dx, dy):
    """整数平移，不缩放、不重采样；仅用于人工注册记录指定的离地帧。"""
    out = np.zeros_like(img)
    h, w = img.shape[:2]
    if abs(dx) >= w or abs(dy) >= h:
        return out
    sx, sy = max(0, -dx), max(0, -dy)
    tx, ty = max(0, dx), max(0, dy)
    width, height = w - abs(dx), h - abs(dy)
    out[ty:ty + height, tx:tx + width] = img[sy:sy + height, sx:sx + width]
    return out


def register_body_root(cid, sha, picks, rgb, alpha, target_sizes):
    """执行经源 SHA/源帧号绑定的人工腰髋轨迹，不从轮廓或帽色估计人体根。"""
    raw = open(ROOT_REGISTRATION, 'rb').read()
    registration = json.loads(raw.decode('utf-8'))
    assert registration['version'] == 1
    assert registration['coordinate_canvas'] == [DECODE, DECODE]
    clip = registration['clips'][cid]
    assert clip['source_sha256'] == sha, (cid, 'manual registration source changed')
    records = clip['frames']
    assert [r['native_frame'] for r in records] == picks, (cid, 'manual registration frame map changed')
    assert [r['output_frame'] for r in records] == list(range(len(picks)))
    allowed = set(clip['airborne_output_frames'])
    shifts = []
    for r in records:
        if r['output_frame'] in allowed:
            assert r['phase'] == 'airborne'
            observed = r['waist_root_source360']
            reference = r['pose_reference_root_source360']
            displacement = [int(round(reference[i] - observed[i])) for i in range(2)]
        else:
            assert r['phase'] == 'ground_pose_preserved'
            displacement = [0, 0]
        shifts.append(displacement)
    corrected_rgb = [shift_xy(r, *s) for r, s in zip(rgb, shifts)]
    corrected_alpha = [shift_xy(a, *s) for a, s in zip(alpha, shifts)]
    # A translation may not silently crop a hand, pole, head, or foot.
    for i, (before, after) in enumerate(zip(alpha, corrected_alpha)):
        assert np.isclose(before.sum(), after.sum(), atol=0.02), (cid, i, 'registration would crop foreground')
    info = dict(method='manual waist/pelvis registration; remove translation only during confirmed airborne frames',
                registration_file=os.path.relpath(ROOT_REGISTRATION, ART).replace(os.sep, '/'),
                registration_sha256=hashlib.sha256(raw).hexdigest(), source_sha256=sha,
                landmark=clip['landmark'], coordinate_canvas=[DECODE, DECODE],
                airborne_output_frames=clip['airborne_output_frames'], shifts_source360_xy=shifts,
                authored_root_motion_source360_xy=[[-dx, -dy] for dx, dy in shifts],
                logical_anchor_source360=[DECODE / 2, 928 / 1024 * DECODE],
                logical_anchor_note='Fixed engine anchor; not a per-frame shoe-bottom measurement. Crouch, leg tuck and bow remain in the pose.',
                manual_landmark_uncertainty_source_px=clip['manual_landmark_uncertainty_source_px'],
                manual_landmark_uncertainty_target_px={str(s): round(clip['manual_landmark_uncertainty_source_px'] * s / DECODE, 3) for s in target_sizes},
                ground_frames_preserved=[r['output_frame'] for r in records if r['output_frame'] not in allowed],
                ground_frames_translation_zero=True)
    return corrected_rgb, corrected_alpha, info


def resize(img_rgb, a, size):
    """预乘 alpha 后面积缩放，避免边缘发绿。"""
    pre = np.dstack([img_rgb * a[..., None], a * 255])
    im = Image.fromarray(np.uint8(np.clip(pre, 0, 255)), 'RGBA').resize((size, size), Image.BOX)
    arr = np.asarray(im, np.float32)
    al = arr[..., 3] / 255
    col = np.where(al[..., None] > 0, arr[..., :3] / np.maximum(al[..., None], 1e-6), 0)
    return col, al


def quantize(frames_rgb, frames_a):
    """整段动作统一 63 色 + 透明（索引 0）。半透明边缘预先混合到屏幕底色。"""
    solid = []
    for col, al in zip(frames_rgb, frames_a):
        mixed = col * al[..., None] + BG * (1 - al[..., None])
        solid.append((np.uint8(np.clip(mixed, 0, 255)), al >= 0.35))
    stack = np.concatenate([s[0][s[1]] for s in solid]).reshape(-1, 1, 3)
    pal_img = Image.fromarray(stack).quantize(colors=63, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    raw = pal_img.getpalette()[:63 * 3]; raw = raw + [0] * (63 * 3 - len(raw))
    pal = np.array(raw, np.uint8).reshape(63, 3)
    out = []
    for rgb, m in solid:
        idx = np.asarray(Image.fromarray(rgb).quantize(palette=pal_img, dither=Image.Dither.NONE), np.uint8) + 1
        idx[~m] = 0
        out.append(idx)
    palf = pal.astype(np.int32)
    for k in range(1, len(out)):
        a, b = out[k - 1], out[k]
        both = (a > 0) & (b > 0) & (a != b)
        d = np.abs(palf[a[both] - 1] - palf[b[both] - 1]).sum(-1)
        hold = np.zeros_like(both); hold[both] = d <= HOLD
        b[hold] = a[hold]
    return pal, out


def rle_row(row):
    out = bytearray(); i = 0; n = len(row)
    while i < n:
        j = i
        if row[i] == 0:
            while j < n and row[j] == 0 and j - i < 128: j += 1
            out.append(0x80 | (j - i - 1))
        else:
            while j < n and row[j] != 0 and j - i < 128: j += 1
            out.append(j - i - 1); out += bytes(row[i:j])
        i = j
    return out


def delta_row(prev, cur):
    out = bytearray(); i = 0; n = len(cur)
    while i < n:
        j = i
        if cur[i] == prev[i]:
            while j < n and cur[j] == prev[j] and j - i < 128: j += 1
            out.append(0x80 | (j - i - 1))
        else:
            while j < n and cur[j] != prev[j] and j - i < 128: j += 1
            out.append(j - i - 1); out += bytes(cur[i:j])
        i = j
    return out


def decode_rows(data, prev, w, h, delta):
    img = prev.copy() if delta else np.zeros((h, w), np.uint8)
    offs = struct.unpack_from('<%dH' % h, data, 0)
    for y in range(h):
        p = offs[y]; x = 0
        while x < w:
            c = data[p]; p += 1; L = (c & 0x7F) + 1
            if c & 0x80:
                if not delta: img[y, x:x + L] = 0
                x += L
            else:
                img[y, x:x + L] = np.frombuffer(bytes(data[p:p + L]), np.uint8); p += L; x += L
    return img


def pack_frames(idx_frames):
    blobs = []
    for k, f in enumerate(idx_frames):
        rows = [rle_row(r) if k == 0 else delta_row(idx_frames[k - 1][y], r) for y, r in enumerate(f)]
        hdr = bytearray(); base = 2 * f.shape[0]; body = bytearray()
        for r in rows:
            hdr += struct.pack('<H', base + len(body)); body += r
        assert base + len(body) < 65536
        blobs.append(bytes(hdr + body))
    return blobs


def rgb565(p):
    return ((int(p[0]) >> 3) << 11) | ((int(p[1]) >> 2) << 5) | (int(p[2]) >> 3)


def container(magic_head, pal, blobs):
    palb = b''.join(struct.pack('<H', rgb565(p)) for p in pal)
    offs = bytearray(); body = bytearray(); base = len(magic_head) + len(palb) + 4 * len(blobs)
    for bl in blobs:
        offs += struct.pack('<I', base + len(body)); body += bl
    return magic_head + palb + bytes(offs) + bytes(body)


def verify(blobs, idx, w, h):
    prev = None
    for k, bl in enumerate(blobs):
        img = decode_rows(bl, prev, w, h, k > 0)
        if not np.array_equal(img, idx[k]): return False
        prev = img
    return True


def small(f, a):
    m = f * a[..., None] + BG * (1 - a[..., None])
    return np.asarray(Image.fromarray(np.uint8(np.clip(m, 0, 255))).resize((60, 60), Image.BOX), np.float32)


def pick_times(clip, fps_src, n_src, sm=None):
    """返回要取的源帧号列表和说明。循环：[a,b) 等分 N 帧（末帧接回首帧）；一次性：[a,b] 含两端等分。"""
    cid = clip['id']; T = SPEC[cid]; N = max(2, round(T * OUT_FPS))
    if cid == 'S-ROPE-FALL':   # Codex 条件：只用原生 24fps 第 6–13 帧（含两端），末帧停 4 帧等引擎接管
        frames = list(range(6, 14)) + [13] * 4
        return frames, dict(rule='native frames 6..13 inclusive, then hold 13 x4 (Codex condition)', interval_s=[0.25, 0.541667])
    a, b = clip['candidate_intervals_seconds'][0]
    loop = clip['loop_required']
    extra = {}
    if loop and sm is not None:   # 在候选区间附近找真正接得上的切点：起点 ±0.5 s，长度 ±20%
        L0 = (b - a) * fps_src; best = None
        for i in range(max(0, int(a * fps_src - fps_src / 2)), int(a * fps_src + fps_src / 2) + 1):
            for L in range(int(L0 * 0.8), int(L0 * 1.2) + 1):
                if i + L >= n_src: break
                e = float(np.abs(sm[i] - sm[i + L]).mean())
                if best is None or e < best[0] - 1e-9: best = (e, i, L)
        if best:
            extra = dict(candidate_s=[a, b], searched_cut_native=[best[1], best[1] + best[2]], cut_diff=round(best[0], 2))
            a, b = best[1] / fps_src, (best[1] + best[2]) / fps_src
    if loop: ts = [a + (b - a) * k / N for k in range(N)]
    else: ts = [a + (b - a) * k / (N - 1) for k in range(N)]
    frames = [min(n_src - 1, int(round(t * fps_src))) for t in ts]
    return frames, dict(rule='loop: N evenly spaced in [a,b)' if loop else 'once: N evenly spaced in [a,b] inclusive',
                        interval_s=[round(a, 4), round(b, 4)], speed=round((b - a) / T, 3), **extra)


def run(clip):
    cid = clip['id']
    src = os.path.join(ART, clip['source_file'])
    sha = hashlib.sha256(open(src, 'rb').read()).hexdigest()
    assert sha == clip['source_sha256'], (cid, 'source hash changed')
    W, H, fps = probe(src)
    frames = decode(src)
    rgb, alpha, green = key_green(frames)
    smn = [small(rgb[i], alpha[i]) for i in range(len(frames))] if clip['loop_required'] else None
    picks, how = pick_times(clip, fps, len(frames), smn)
    info = dict(id=cid, file=clip['source_file'], sha256=sha, source=[W, H, round(fps, 3), len(frames)],
                green=[round(float(v), 1) for v in green], loop=bool(clip['loop_required']), src_frames=picks, retime=how,
                frames=len(picks), ms=round(len(picks) * 1000 / OUT_FPS))
    R = [rgb[i] for i in picks]; A = [alpha[i] for i in picks]
    if cid in ROOT_REGISTERED:
        R, A, info['root'] = register_body_root(cid, sha, picks, R, A, clip['target_sizes'])
    if cid in FULL_BODY:
        fa = [feet_row(a) for a in A]
        range_key = 'silhouette_bottom_row_range_target' if cid in ROOT_REGISTERED else 'feet_row_range_target'
        info[range_key] = {str(s): round((max(fa) - min(fa)) * s / DECODE, 2) for s in clip['target_sizes']}
        # 横向：脚底以上 10% 身高带内的像素中心
        xs = []
        for a, f in zip(A, fa):
            band = a[max(0, f - 12):f + 1] > 0.5
            cols = np.nonzero(band.any(0))[0]
            xs.append(float(cols.mean()) if len(cols) else DECODE / 2)
        range_key = 'silhouette_bottom_x_range_target' if cid in ROOT_REGISTERED else 'feet_x_range_target'
        info[range_key] = {str(s): round((max(xs) - min(xs)) * s / DECODE, 2) for s in clip['target_sizes']}
        if cid in ROOT_REGISTERED:
            info['silhouette_diagnostic_note'] = 'Lowest shoe outline moves with the leg pose; these ranges are not anatomical root drift or pass/fail thresholds.'
    # 循环跨接：末帧→首帧的变化 与 相邻帧变化中位数之比（≈1 表示接得上）
    sm = [small(r, a) for r, a in zip(R, A)]
    steps = [float(np.abs(sm[k + 1] - sm[k]).mean()) for k in range(len(sm) - 1)]
    seam = float(np.abs(sm[0] - sm[-1]).mean())
    info['seam'] = dict(last_to_first=round(seam, 2), median_step=round(float(np.median(steps)), 2),
                        ratio=round(seam / max(1e-6, float(np.median(steps))), 2), max_step=round(max(steps), 2))
    sizes = sorted(set(clip['target_sizes'] + EXTRA_SIZES.get(cid, [])))
    info['sizes'] = {}
    sheets = {}
    for size in sizes:
        cols, als = zip(*[resize(r, a, size) for r, a in zip(R, A)])
        pal, idx = quantize(cols, als)
        blobs = pack_frames(idx)
        ok = verify(blobs, idx, size, size)
        err = []
        for k in range(len(idx)):
            m = idx[k] > 0
            src_mix = cols[k] * als[k][..., None] + BG * (1 - als[k][..., None])
            if m.any(): err.append(float(np.abs(pal[idx[k][m] - 1].astype(np.float32) - src_mix[m]).mean()))
        d = os.path.join(OUT, cid, str(size)); os.makedirs(d, exist_ok=True)
        ph = os.path.join(d, 'PLACEHOLDER')
        if os.path.exists(ph): os.replace(ph, os.path.join(d, '.placeholder-replaced'))
        data = container(struct.pack('<4sHHHB', b'FNBO', 1, size, len(blobs), 1 if info['loop'] else 0), pal, blobs)
        open(os.path.join(d, 'frames.bin'), 'wb').write(data)
        cols_n = min(len(idx), 12); rows_n = (len(idx) + cols_n - 1) // cols_n
        sheet = np.zeros((rows_n * size, cols_n * size, 4), np.uint8)
        rgba_frames = []
        for k, f in enumerate(idx):
            rgba = np.zeros((size, size, 4), np.uint8); m = f > 0
            rgba[m, :3] = pal[f[m] - 1]; rgba[m, 3] = 255
            y0, x0 = (k // cols_n) * size, (k % cols_n) * size
            sheet[y0:y0 + size, x0:x0 + size] = rgba
            rgba_frames.append(rgba)
        Image.fromarray(sheet, 'RGBA').save(os.path.join(d, 'sheet.png'), optimize=True)
        sheets[size] = rgba_frames
        top = [int(np.nonzero((f > 0).any(1))[0][0]) if (f > 0).any() else size for f in idx]
        info['sizes'][str(size)] = dict(bytes=len(data), colors=int(len({tuple(p) for p in pal})), roundtrip_ok=ok,
                                        mean_color_err=round(float(np.mean(err)), 2), max_frame_err=round(float(np.max(err)), 2),
                                        min_top_margin_px=min(top),
                                        foot_anchor=[round(size / 2, 2), round(928 / 1024 * size, 2)])
    # 预览：最大尺寸，3 倍，#0A0716 底；循环片段多放一轮看跨接
    size = max(sizes); sc = 3
    os.makedirs(PREV, exist_ok=True)
    fr = []
    for rgba in sheets[size] * (2 if info['loop'] else 1):
        bg = Image.new('RGBA', (size, size), (10, 7, 22, 255)); bg.alpha_composite(Image.fromarray(rgba, 'RGBA'))
        fr.append(bg.convert('RGB').resize((size * sc, size * sc), Image.NEAREST))
    fr[0].save(os.path.join(PREV, cid + '.gif'), save_all=True, append_images=fr[1:], duration=round(1000 / OUT_FPS), loop=0)
    return info, sheets


def overview(all_sheets):
    """总览：每段一行，目标尺寸原大 ×2，最多 36 帧。"""
    rows = []
    for cid, sheets in all_sheets:
        size = max(sheets); fr = sheets[size][:36]
        row = Image.new('RGB', (12 + len(fr) * (size * 2 + 2), size * 2 + 16), (10, 7, 22))
        for k, rgba in enumerate(fr):
            im = Image.fromarray(rgba, 'RGBA').resize((size * 2, size * 2), Image.NEAREST)
            row.paste(im, (12 + k * (size * 2 + 2), 14), im)
        from PIL import ImageDraw
        ImageDraw.Draw(row).text((4, 1), '%s  %dpx  %d frames' % (cid, size, len(sheets[size])), fill=(120, 230, 220))
        rows.append(row)
    W = min(2400, max(r.width for r in rows)); Hh = sum(r.height for r in rows)
    ov = Image.new('RGB', (W, Hh), (10, 7, 22)); y = 0
    for r in rows: ov.paste(r.crop((0, 0, W, r.height)), (0, y)); y += r.height
    ov.save(os.path.join(PREV, 'overview.png'))


def main():
    sel = json.load(open(os.path.join(ART, 'CLIP_SELECTION.json'), encoding='utf-8'))
    clips = {c['id']: c for c in sel['clips']}
    ids = sys.argv[1:] or list(clips)
    mf = os.path.join(OUT, 'manifest.json')
    man = json.load(open(mf, encoding='utf-8')) if os.path.exists(mf) else {}
    allsh = []
    for cid in ids:
        info, sheets = run(clips[cid]); man[cid] = info; allsh.append((cid, sheets))
        s = ', '.join(f"{k}px {v['bytes'] // 1024}KB 回解{'✓' if v['roundtrip_ok'] else '✗'} 色差{v['mean_color_err']}" for k, v in info['sizes'].items())
        extra = ''
        if 'feet_row_range_target' in info: extra = f" 脚底纵{info['feet_row_range_target']} 横{info['feet_x_range_target']}"
        print(f"{cid}: {info['frames']}帧 {info['ms']}ms {'循环 跨接比 ' + str(info['seam']['ratio']) if info['loop'] else '一次'} | {s}{extra}", flush=True)
    man['_note'] = ('由 tools/clip_to_sprites_soundlab.py 生成；frames.bin 为 FNBO v1：头、RGB565 调色板(63，索引 0 透明)、帧偏移、'
                    '每帧行偏移+RLE（第0帧关键帧，其余差分）；处理分辨率 360，逻辑脚锚 = (size/2, 928/1024*size)；'
                    'JUMP/BOW仅在确认离地帧使用源SHA绑定的人工腰髋注册，最低鞋轮廓统计不是人体根误差')
    json.dump(man, open(mf, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    if len(ids) == len(clips): overview(allsh)
    tot = sum(v['bytes'] for k, i in man.items() if not k.startswith('_') for v in i['sizes'].values())
    print('动画总计', tot // 1024, 'KB')


if __name__ == '__main__':
    main()
