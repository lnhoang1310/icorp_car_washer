# Tao file am thanh giong nu AI (Microsoft "HoaiMy") cho tu rua xe.
#     python3 -m pip install -U edge-tts miniaudio
#     python3 create_wav.py
# Ket qua: WAV 16 kHz, 16-bit, mono trong thu muc "audio" -> chep vao assets/audio cua project.
import asyncio, os, wave, array, time
import edge_tts, miniaudio

VOICE = "vi-VN-HoaiMyNeural"   # giong nu. Giong nam: "vi-VN-NamMinhNeural"
RATE = "-0%"                   # toc do doc, PHAI co dau +/- va %: "-10%", "+0%", "+10%"
OUT = "audio_wav"

CLIPS = {
    #"scan_qr.wav":   "Vui lòng quét mã QR trên màn hình để thanh toán",
    #"pay_ok.wav":    "Thanh toán thành công, bắt đầu rửa xe",
    "end_session.wav": "Kết thúc rửa xe, cảm ơn bạn đã sử dụng dịch vụ, tạm biệt và hẹn gặp lại",
    #"pay_error.wav": "Thanh toán thất bại, vui lòng thử lại",
    #"qr_timeout.wav": "Mã QR đã hết hạn, vui lòng thử lại"
}


async def tts_to_mp3(text, mp3):
    """Thu toi da 3 lan; lan cuoi bo tuy chinh toc do (de loai tru loi do tham so rate)."""
    last = None
    for attempt in range(3):
        kw = {"rate": RATE} if attempt < 2 else {}
        try:
            await edge_tts.Communicate(text, VOICE, **kw).save(mp3)
            if os.path.getsize(mp3) > 0:
                return
        except Exception as e:
            last = e
            print(f"   lan {attempt + 1} loi: {type(e).__name__}: {e}")
        time.sleep(1.5)  # may chu gioi han goi lien tiep -> nghi mot chut roi thu lai
    raise RuntimeError(f"khong tao duoc am thanh: {last}")


def mp3_to_wav(mp3, wav):
    pcm = miniaudio.decode_file(mp3, output_format=miniaudio.SampleFormat.SIGNED16,
                                nchannels=1, sample_rate=16000)
    s = array.array("h", pcm.samples)
    peak = max(1, max(abs(x) for x in s))
    k = 0.89 * 32767 / peak  # chuan hoa am luong (-1 dBFS)
    s = array.array("h", (int(x * k) for x in s))
    with wave.open(wav, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(16000)
        w.writeframes(s.tobytes())
    return len(s) / 16000


async def main():
    os.makedirs(OUT, exist_ok=True)
    print(f"edge-tts {getattr(edge_tts, '__version__', '?')}, giong {VOICE!r}, toc do {RATE!r}")
    for name, text in CLIPS.items():
        print(f"-> {name}: {text}")
        mp3 = os.path.join(OUT, name + ".mp3")
        await tts_to_mp3(text, mp3)
        sec = mp3_to_wav(mp3, os.path.join(OUT, name))
        os.remove(mp3)
        print(f"   OK {sec:.2f} s")
        time.sleep(1.0)


asyncio.run(main())