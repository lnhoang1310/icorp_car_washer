#ifndef __AUDIO_CLIPS_H__
#define __AUDIO_CLIPS_H__

/*
 * Danh sach cau thong bao. Cot 3 la noi dung can thu am (dung luon lam kich ban thu).
 * File WAV: 16 kHz, 16-bit, mono, dat trong assets/audio/, nap vao phan vung storage.
 * Them/bot cau: chi sua bang nay.
 */

#define AUDIO_CLIP_LIST(X)                                                                          \
    X(AUDIO_SCAN_QR, "scan_qr.wav", "Vui lòng quét mã QR code trên màn hình để thanh toán") \
    X(AUDIO_PAY_OK, "pay_ok.wav", "Thanh toán thành công, bắt đầu rửa xe") \
    X(AUDIO_END_SESSION, "end_session.wav", "Kết thúc rửa xe, cảm ơn bạn đã sử dụng dịch vụ. Tạm biệt và hẹn gặp lại") \
    X(AUDIO_PAY_ERROR, "pay_error.wav", "Thanh toán thất bại, vui lòng thử lại") \
    X(AUDIO_QR_TIMEOUT, "qr_timeout.wav", "Mã QR đã hết hạn, vui lòng thử lại") \
    X(AUDIO_CONNECTION_ERROR, "connection_error.wav", "Lỗi kết nối, vui lòng thử lại") \
    X(AUDIO_BUTTON_CLICK, "button_click.wav", "Button click")

typedef enum
{
#define X_ENUM(id, file, text) id,
    AUDIO_CLIP_LIST(X_ENUM)
#undef X_ENUM
    AUDIO_CLIP_COUNT
} audio_clip_t;

#endif