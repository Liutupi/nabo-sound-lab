"""生成宿主对拍用的固定测试信号（16 kHz、float32、-1..1），写到 signals/*.npy。"""
import numpy as np, pathlib
SR = 16000
rng = np.random.default_rng(20261023)
out = pathlib.Path(__file__).parent / 'signals'; out.mkdir(exist_ok=True)

def tone(f, dur, amp=0.3, harm=(1, .5, .33, .2), vib=0.0, vib_hz=5.5):
    t = np.arange(int(SR * dur)) / SR
    finst = f * 2 ** (vib / 1200 * np.sin(2 * np.pi * vib_hz * t))   # 瞬时频率
    ph = 2 * np.pi * np.cumsum(finst) / SR
    x = sum(a * np.sin(ph * (k + 1)) for k, a in enumerate(harm))
    env = np.minimum(1, np.minimum(t / 0.02, (dur - t) / 0.02))
    return amp * x / sum(harm) * env

def sil(d): return np.zeros(int(SR * d))

def kick(dur=0.3):
    t = np.arange(int(SR * dur)) / SR
    f = 150 * (45 / 150) ** np.minimum(t / 0.12, 1)
    return 0.8 * np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t / 0.08)

def beats(bpm, secs):
    x = np.zeros(int(SR * secs)); step = 60 / bpm
    for i in range(int(secs / step)):
        k = kick(); s = int(i * step * SR); x[s:s + len(k)] += k[:len(x) - s]
        h = int((i + 0.5) * step * SR)
        if h + 800 < len(x): x[h:h + 800] += 0.15 * rng.standard_normal(800) * np.exp(-np.arange(800) / 200)
    return x

def chord(midis, dur=2.0):
    t = np.arange(int(SR * dur)) / SR; x = np.zeros_like(t)
    for m in midis:
        f = 440 * 2 ** ((m - 69) / 12)
        for k, a in enumerate((1, .6, .35, .2)): x += a * np.sin(2 * np.pi * f * (k + 1) * t) * np.exp(-t * (0.6 + k * 0.5))
    return 0.25 * x / len(midis)

def soft_hits(secs=10):
    # 背景噪声上一串逐渐变响的轻击：有的刚好过起音阈值，有的刚好不过
    n = int(SR * secs); x = 0.02 * rng.standard_normal(n)
    for i in range(int(secs / 0.25)):
        s = int(i * 0.25 * SR); amp = 0.004 * 1.12 ** i; L = 1200
        hit = amp * rng.standard_normal(L) * np.exp(-np.arange(L) / 250)
        x[s:s + L] += hit[:n - s]
    return x

def breathy(secs_each=1.5):
    # 单音加越来越大的噪声：YIN 的 CMND 会落在 0.15 附近
    parts = []
    for f in (196, 261.63, 392):
        for snr in (20, 12, 8, 5, 3, 1):
            t = tone(f, secs_each, amp=0.2); p = np.sqrt(np.mean(t ** 2))
            parts.append(t + p / (10 ** (snr / 20)) * rng.standard_normal(len(t)))
    return np.concatenate(parts)

def quiet_tones():
    # 很轻的单音，RMS 从 -56 dB 升到 -46 dB：卡住音高检测的静音门槛（-52 dB）
    parts = []
    for db in (-56, -54, -52.5, -51.5, -50, -48, -46):
        t = tone(261.63, 1.0, amp=1.0); t *= 10 ** (db / 20) / np.sqrt(np.mean(t ** 2)); parts += [t, sil(0.2)]
    return np.concatenate(parts)

sig = {
    'quiet': quiet_tones(),
    'soft_hits': soft_hits(),
    'breathy': breathy(),
    'tones': np.concatenate([np.concatenate([tone(f, 0.7), sil(0.25)]) for f in [82.41, 110, 146.83, 196, 261.63, 329.63, 440, 659.25, 880]]),
    'voice': np.concatenate([tone(220, 2.5, vib=30), sil(0.3), tone(330, 2.0, amp=0.15, vib=15)]) + 0.003 * rng.standard_normal(int(SR * 4.8)),
    'beats': np.concatenate([beats(100, 9), beats(120, 9)]),
    'chords': np.concatenate([chord(c) for c in ([48, 60, 64, 67], [45, 57, 60, 64], [41, 53, 57, 60], [43, 55, 59, 62])]),
    'noise': np.concatenate([0.03 * rng.standard_normal(SR), sil(1.0), 0.3 * rng.standard_normal(SR // 2), sil(0.5)]),
}
for k, v in sig.items():
    v = np.clip(v, -0.99, 0.99).astype(np.float32)
    n = len(v) // 320 * 320
    np.save(out / f'{k}.npy', v[:n]); print(k, n / SR, 's')
