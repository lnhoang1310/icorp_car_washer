# Chuyen moi file .mp3 trong thu muc thanh .wav dung dinh dang tu rua xe doc duoc:
# 16 kHz, 16-bit, mono, chuan hoa am luong, cat bot khoang lang dau/cuoi.
#     python3 -m pip install -U miniaudio
#     python3 mp3_to_wav.py                 (chuyen moi .mp3 trong thu muc hien tai)
#     python3 mp3_to_wav.py D:\duong\dan     (chuyen moi .mp3 trong thu muc chi dinh)
# File .wav ghi canh file .mp3, cung ten (scan_qr.mp3 -> scan_qr.wav).
import array, glob, os, sys, wave
import miniaudio

SR = 16000
LEAD_MS, TAIL_MS = 120, 200  # im lang them vao dau / cuoi


def convert(mp3):
    pcm = miniaudio.decode_file(mp3, output_format=miniaudio.SampleFormat.SIGNED16,
                                nchannels=1, sample_rate=SR)
    s = array.array("h", pcm.samples)
    if not s:
        raise ValueError("file rong")

    # cat khoang lang dau/cuoi (nguong 2% bien do lon nhat)
    peak = max(abs(x) for x in s) or 1
    th = peak * 0.02
    first = next(i for i, x in enumerate(s) if abs(x) > th)
    last = len(s) - next(i for i, x in enumerate(reversed(s)) if abs(x) > th)
    s = s[max(0, first - SR // 100):min(len(s), last + SR // 20)]

    # chuan hoa am luong -1 dBFS
    k = 0.89 * 32767 / peak
    out = array.array("h", [0] * (SR * LEAD_MS // 1000))
    out.extend(int(x * k) for x in s)
    out.extend([0] * (SR * TAIL_MS // 1000))

    wav = os.path.splitext(mp3)[0] + ".wav"
    with wave.open(wav, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(out.tobytes())
    return wav, len(out) / SR


folder = sys.argv[1] if len(sys.argv) > 1 else "."
files = sorted(glob.glob(os.path.join(folder, "*.mp3")))
if not files:
    print("Khong tim thay file .mp3 nao trong", os.path.abspath(folder))
for f in files:
    try:
        wav, sec = convert(f)
        print(f"OK  {os.path.basename(f)} -> {os.path.basename(wav)}  ({sec:.2f} s)")
    except Exception as e:
        print(f"LOI {os.path.basename(f)}: {e}")