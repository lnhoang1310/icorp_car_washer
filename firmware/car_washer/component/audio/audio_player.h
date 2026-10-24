#ifndef __AUDIO_PLAYER_H__
#define __AUDIO_PLAYER_H__

/*
 * Phat thong bao qua I2S -> MAX98357A.
 * Cac ham deu KHONG chan: dua vao hang doi roi return.
 * audio_play() khi dang phat: cau moi chen ngang cau cu (thong bao moi nhat luon quan trong hon).
 */

#include "esp_err.h"
#include "audio_clips.h"
#include <stdint.h>

esp_err_t audio_init(void);
void audio_play(audio_clip_t clip);
void audio_stop(void);
/* TEST LOA: phat tieng "tut" 1 kHz lien tuc trong `seconds` giay, am luong toi da, khong bi cau
 * khac chen ngang -> de do dien ap cac chan ampli (xem log). Goi tu peripheral_test. */
void audio_test_tone(uint32_t seconds);
void audio_set_volume(uint8_t percent);
const char *audio_clip_text(audio_clip_t clip);
const char *audio_clip_file(audio_clip_t clip);

#endif