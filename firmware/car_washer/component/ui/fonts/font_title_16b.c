/*******************************************************************************
 * Size: 16 px
 * Bpp: 4
 * Opts: --no-compress --bpp 4 --format lvgl --lv-include lvgl.h --size 16 --font BeVietnamPro-Bold.ttf --symbols Còn lại -o out/font_title_16b.c --lv-font-name font_title_16b
 ******************************************************************************/

 #ifdef LV_LVGL_H_INCLUDE_SIMPLE
 #include "lvgl.h"
 #else
 #include "lvgl.h"
 #endif
 
 #ifndef FONT_TITLE_16B
 #define FONT_TITLE_16B 1
 #endif
 
 #if FONT_TITLE_16B
 
 /*-----------------
  *    BITMAPS
  *----------------*/
 
 /*Store the image of the glyphs*/
 static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
     /* U+0020 " " */
 
     /* U+0043 "C" */
     0x0, 0x3, 0xae, 0xfe, 0xb3, 0x0, 0x0, 0x7f,
     0xff, 0xff, 0xff, 0x60, 0x5, 0xff, 0xe7, 0x57,
     0xef, 0xf3, 0xd, 0xfe, 0x20, 0x0, 0x1e, 0xfa,
     0x2f, 0xf7, 0x0, 0x0, 0x3, 0x43, 0x5f, 0xf3,
     0x0, 0x0, 0x0, 0x0, 0x5f, 0xf3, 0x0, 0x0,
     0x0, 0x0, 0x2f, 0xf7, 0x0, 0x0, 0x3, 0x43,
     0xd, 0xfe, 0x20, 0x0, 0x1e, 0xfa, 0x5, 0xff,
     0xe8, 0x57, 0xef, 0xf3, 0x0, 0x7f, 0xff, 0xff,
     0xff, 0x60, 0x0, 0x3, 0xae, 0xfe, 0xb4, 0x0,
 
     /* U+0069 "i" */
     0xa, 0xf6, 0xf, 0xfc, 0x8, 0xd5, 0x0, 0x0,
     0xd, 0xf9, 0xd, 0xf9, 0xd, 0xf9, 0xd, 0xf9,
     0xd, 0xf9, 0xd, 0xf9, 0xd, 0xf9, 0xd, 0xf9,
     0xd, 0xf9,
 
     /* U+006C "l" */
     0xef, 0x8e, 0xf8, 0xef, 0x8e, 0xf8, 0xef, 0x8e,
     0xf8, 0xef, 0x8e, 0xf8, 0xef, 0x8e, 0xf8, 0xef,
     0x8e, 0xf8, 0xef, 0x80,
 
     /* U+006E "n" */
     0x1f, 0xf6, 0xae, 0xe6, 0x1, 0xff, 0xff, 0xff,
     0xf3, 0x1f, 0xfc, 0x2, 0xff, 0x81, 0xff, 0x60,
     0xd, 0xfa, 0x1f, 0xf5, 0x0, 0xcf, 0xa1, 0xff,
     0x50, 0xc, 0xfa, 0x1f, 0xf5, 0x0, 0xcf, 0xa1,
     0xff, 0x50, 0xc, 0xfa, 0x1f, 0xf5, 0x0, 0xcf,
     0xa0,
 
     /* U+00F2 "ò" */
     0x0, 0x1f, 0xf2, 0x0, 0x0, 0x0, 0x7, 0xf8,
     0x0, 0x0, 0x0, 0x0, 0xde, 0x0, 0x0, 0x0,
     0x0, 0x0, 0x0, 0x0, 0x0, 0x2a, 0xef, 0xd6,
     0x0, 0x2, 0xef, 0xff, 0xff, 0x90, 0xb, 0xff,
     0x41, 0xaf, 0xf4, 0x1f, 0xf7, 0x0, 0xe, 0xf9,
     0x2f, 0xf5, 0x0, 0xc, 0xfb, 0x1f, 0xf7, 0x0,
     0xe, 0xf9, 0xb, 0xff, 0x41, 0xaf, 0xf4, 0x2,
     0xef, 0xff, 0xff, 0x90, 0x0, 0x2a, 0xef, 0xd6,
     0x0,
 
     /* U+1EA1 "ạ" */
     0x0, 0x4d, 0xfd, 0x4c, 0xfa, 0x4, 0xff, 0xff,
     0xff, 0xfa, 0xb, 0xff, 0x41, 0xaf, 0xfa, 0xf,
     0xf8, 0x0, 0xf, 0xfa, 0x1f, 0xf5, 0x0, 0xc,
     0xfa, 0xf, 0xf7, 0x0, 0xe, 0xfa, 0xb, 0xfe,
     0x20, 0x7f, 0xfa, 0x3, 0xff, 0xff, 0xff, 0xfa,
     0x0, 0x4d, 0xfe, 0x6c, 0xfa, 0x0, 0x0, 0x0,
     0x0, 0x0, 0x0, 0x0, 0x49, 0x10, 0x0, 0x0,
     0x0, 0xef, 0x80, 0x0, 0x0, 0x0, 0x9e, 0x40,
     0x0
 };
 
 
 /*---------------------
  *  GLYPH DESCRIPTION
  *--------------------*/
 
 static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
     {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
     {.bitmap_index = 0, .adv_w = 59, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
     {.bitmap_index = 0, .adv_w = 200, .box_w = 12, .box_h = 12, .ofs_x = 0, .ofs_y = 0},
     {.bitmap_index = 72, .adv_w = 76, .box_w = 4, .box_h = 13, .ofs_x = 0, .ofs_y = 0},
     {.bitmap_index = 98, .adv_w = 74, .box_w = 3, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
     {.bitmap_index = 118, .adv_w = 153, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
     {.bitmap_index = 159, .adv_w = 168, .box_w = 10, .box_h = 13, .ofs_x = 0, .ofs_y = 0},
     {.bitmap_index = 224, .adv_w = 170, .box_w = 10, .box_h = 13, .ofs_x = 0, .ofs_y = -4}
 };
 
 /*---------------------
  *  CHARACTER MAPPING
  *--------------------*/
 
 static const uint16_t unicode_list_0[] = {
     0x0, 0x23, 0x49, 0x4c, 0x4e, 0xd2, 0x1e81
 };
 
 /*Collect the unicode lists and glyph_id offsets*/
 static const lv_font_fmt_txt_cmap_t cmaps[] =
 {
     {
         .range_start = 32, .range_length = 7810, .glyph_id_start = 1,
         .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 7, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
     }
 };
 
 /*-----------------
  *    KERNING
  *----------------*/
 
 
 /*Map glyph_ids to kern left classes*/
 static const uint8_t kern_left_class_mapping[] =
 {
     0, 0, 0, 0, 1, 1, 2, 3
 };
 
 /*Map glyph_ids to kern right classes*/
 static const uint8_t kern_right_class_mapping[] =
 {
     0, 0, 1, 2, 3, 3, 4, 4
 };
 
 /*Kern values between classes*/
 static const int8_t kern_class_values[] =
 {
     0, 0, 0, -2, -1, 0, -2, -1,
     0, 1, 1, 2
 };
 
 
 /*Collect the kern class' data in one place*/
 static const lv_font_fmt_txt_kern_classes_t kern_classes =
 {
     .class_pair_values   = kern_class_values,
     .left_class_mapping  = kern_left_class_mapping,
     .right_class_mapping = kern_right_class_mapping,
     .left_class_cnt      = 3,
     .right_class_cnt     = 4,
 };
 
 /*--------------------
  *  ALL CUSTOM DATA
  *--------------------*/
 
 #if LVGL_VERSION_MAJOR == 8
 /*Store all the custom data of the font*/
 static  lv_font_fmt_txt_glyph_cache_t cache;
 #endif
 
 #if LVGL_VERSION_MAJOR >= 8
 static const lv_font_fmt_txt_dsc_t font_dsc = {
 #else
 static lv_font_fmt_txt_dsc_t font_dsc = {
 #endif
     .glyph_bitmap = glyph_bitmap,
     .glyph_dsc = glyph_dsc,
     .cmaps = cmaps,
     .kern_dsc = &kern_classes,
     .kern_scale = 16,
     .cmap_num = 1,
     .bpp = 4,
     .kern_classes = 1,
     .bitmap_format = 0,
 #if LVGL_VERSION_MAJOR == 8
     .cache = &cache
 #endif
 };
 
 
 
 /*-----------------
  *  PUBLIC FONT
  *----------------*/
 
 /*Initialize a public general font descriptor*/
 #if LVGL_VERSION_MAJOR >= 8
 const lv_font_t font_title_16b = {
 #else
 lv_font_t font_title_16b = {
 #endif
     .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
     .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
     .line_height = 17,          /*The maximum line height required by the font*/
     .base_line = 4,             /*Baseline measured from the bottom of the line*/
 #if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
     .subpx = LV_FONT_SUBPX_NONE,
 #endif
 #if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
     .underline_position = -2,
     .underline_thickness = 1,
 #endif
     .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
 #if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
     .fallback = NULL,
 #endif
     .user_data = NULL,
 };
 
 
 
 #endif /*#if FONT_TITLE_16B*/