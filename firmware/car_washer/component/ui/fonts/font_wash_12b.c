/*******************************************************************************
 * Size: 12 px
 * Bpp: 4
 * Opts: --no-compress --bpp 4 --format lvgl --lv-include lvgl.h --size 12 --font BeVietnamPro-Bold.ttf --symbols Xịt bọt nước khô -o out/font_wash_12b.c --lv-font-name font_wash_12b
 ******************************************************************************/

 #ifdef LV_LVGL_H_INCLUDE_SIMPLE
 #include "lvgl.h"
 #else
 #include "lvgl.h"
 #endif
 
 #ifndef FONT_WASH_12B
 #define FONT_WASH_12B 1
 #endif
 
 #if FONT_WASH_12B
 
 /*-----------------
  *    BITMAPS
  *----------------*/
 
 /*Store the image of the glyphs*/
 static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
     /* U+0020 " " */
 
     /* U+0058 "X" */
     0x2f, 0xf1, 0x0, 0xdf, 0x50, 0x9f, 0x90, 0x5f,
     0xc0, 0x1, 0xff, 0x2d, 0xf4, 0x0, 0x7, 0xfe,
     0xfb, 0x0, 0x0, 0xe, 0xff, 0x20, 0x0, 0x0,
     0xef, 0xf2, 0x0, 0x0, 0x7f, 0xef, 0xb0, 0x0,
     0x1f, 0xf2, 0xdf, 0x40, 0x9, 0xf9, 0x5, 0xfc,
     0x2, 0xff, 0x10, 0xd, 0xf5,
 
     /* U+0062 "b" */
     0x3f, 0xa0, 0x0, 0x0, 0x3f, 0xa0, 0x0, 0x0,
     0x3f, 0xa0, 0x0, 0x0, 0x3f, 0xaa, 0xfd, 0x20,
     0x3f, 0xfc, 0xbf, 0xd0, 0x3f, 0xd0, 0xb, 0xf4,
     0x3f, 0xa0, 0x7, 0xf5, 0x3f, 0xd0, 0xb, 0xf4,
     0x3f, 0xfc, 0xbf, 0xd0, 0x3f, 0xba, 0xfd, 0x20,
 
     /* U+0063 "c" */
     0x1, 0xaf, 0xf9, 0x0, 0xcf, 0xbb, 0xf8, 0x3f,
     0xa0, 0x8, 0x85, 0xf7, 0x0, 0x0, 0x3f, 0xa0,
     0x8, 0x80, 0xcf, 0xab, 0xf9, 0x1, 0xaf, 0xf9,
     0x0,
 
     /* U+0068 "h" */
     0x3f, 0xa0, 0x0, 0x3, 0xfa, 0x0, 0x0, 0x3f,
     0xa0, 0x0, 0x3, 0xfa, 0xaf, 0xb0, 0x3f, 0xfb,
     0xef, 0x63, 0xfc, 0x5, 0xf9, 0x3f, 0xa0, 0x3f,
     0x93, 0xfa, 0x3, 0xf9, 0x3f, 0xa0, 0x3f, 0x93,
     0xfa, 0x3, 0xf9,
 
     /* U+006B "k" */
     0x3f, 0xa0, 0x0, 0x3, 0xfa, 0x0, 0x0, 0x3f,
     0xa0, 0x0, 0x3, 0xfa, 0xa, 0xf6, 0x3f, 0xa6,
     0xfa, 0x3, 0xfd, 0xfd, 0x0, 0x3f, 0xff, 0x80,
     0x3, 0xfb, 0xdf, 0x30, 0x3f, 0xa3, 0xfd, 0x3,
     0xfa, 0x8, 0xf9,
 
     /* U+006E "n" */
     0x4f, 0xac, 0xf9, 0x4, 0xff, 0x9d, 0xf4, 0x4f,
     0x90, 0x6f, 0x74, 0xf8, 0x5, 0xf7, 0x4f, 0x80,
     0x5f, 0x74, 0xf8, 0x5, 0xf7, 0x4f, 0x80, 0x5f,
     0x70,
 
     /* U+0074 "t" */
     0x4, 0x82, 0x0, 0x9f, 0x40, 0xaf, 0xff, 0x95,
     0xdf, 0xa5, 0x9, 0xf4, 0x0, 0x9f, 0x40, 0x9,
     0xf4, 0x0, 0x6f, 0xc6, 0x0, 0xbf, 0xb0,
 
     /* U+00F4 "ô" */
     0x0, 0x2f, 0xe1, 0x0, 0x0, 0xc9, 0xbb, 0x0,
     0x1, 0x40, 0x14, 0x0, 0x1, 0xaf, 0xe9, 0x0,
     0xc, 0xfb, 0xcf, 0xb0, 0x3f, 0xb0, 0xd, 0xf2,
     0x5f, 0x70, 0x9, 0xf4, 0x3f, 0xb0, 0xd, 0xf2,
     0xc, 0xfc, 0xcf, 0xb0, 0x1, 0xaf, 0xe9, 0x0,
 
     /* U+01B0 "ư" */
     0x0, 0x0, 0x0, 0x44, 0x0, 0x0, 0x0, 0xcb,
     0x4f, 0x80, 0x7f, 0xf6, 0x4f, 0x80, 0x7f, 0x50,
     0x4f, 0x80, 0x7f, 0x50, 0x4f, 0x80, 0x7f, 0x50,
     0x4f, 0x90, 0x9f, 0x50, 0x2f, 0xfa, 0xff, 0x50,
     0x8, 0xfc, 0x8f, 0x50,
 
     /* U+1ECB "ị" */
     0x1e, 0x92, 0xfc, 0x2, 0x11, 0xfb, 0x1f, 0xb1,
     0xfb, 0x1f, 0xb1, 0xfb, 0x1f, 0xb1, 0xfb, 0x0,
     0x0, 0xe8, 0xe, 0x90, 0x0,
 
     /* U+1ECD "ọ" */
     0x1, 0xaf, 0xe9, 0x0, 0xc, 0xfb, 0xcf, 0xb0,
     0x3f, 0xb0, 0xd, 0xf2, 0x5f, 0x70, 0x9, 0xf4,
     0x3f, 0xb0, 0xd, 0xf2, 0xc, 0xfc, 0xcf, 0xb0,
     0x1, 0xaf, 0xe9, 0x0, 0x0, 0x0, 0x0, 0x0,
     0x0, 0xc, 0xa0, 0x0, 0x0, 0xd, 0xb0, 0x0,
     0x0, 0x0, 0x0, 0x0,
 
     /* U+1EDB "ớ" */
     0x0, 0x0, 0xea, 0x0, 0x0, 0x5, 0xf2, 0x41,
     0x0, 0x3, 0x31, 0xf6, 0x1, 0xaf, 0xff, 0xe2,
     0xc, 0xfb, 0xcf, 0xa0, 0x3f, 0xb0, 0xd, 0xf1,
     0x5f, 0x70, 0x9, 0xf3, 0x3f, 0xb0, 0xd, 0xf2,
     0xc, 0xfc, 0xcf, 0xb0, 0x1, 0xaf, 0xe9, 0x0
 };
 
 
 /*---------------------
  *  GLYPH DESCRIPTION
  *--------------------*/
 
 static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
     {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
     {.bitmap_index = 0, .adv_w = 44, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
     {.bitmap_index = 0, .adv_w = 148, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
     {.bitmap_index = 45, .adv_w = 128, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
     {.bitmap_index = 85, .adv_w = 121, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
     {.bitmap_index = 110, .adv_w = 117, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
     {.bitmap_index = 145, .adv_w = 115, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
     {.bitmap_index = 180, .adv_w = 115, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
     {.bitmap_index = 205, .adv_w = 84, .box_w = 5, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
     {.bitmap_index = 228, .adv_w = 126, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
     {.bitmap_index = 268, .adv_w = 114, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
     {.bitmap_index = 304, .adv_w = 58, .box_w = 3, .box_h = 14, .ofs_x = 0, .ofs_y = -4},
     {.bitmap_index = 325, .adv_w = 126, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = -4},
     {.bitmap_index = 369, .adv_w = 126, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0}
 };
 
 /*---------------------
  *  CHARACTER MAPPING
  *--------------------*/
 
 static const uint16_t unicode_list_0[] = {
     0x0, 0x38, 0x42, 0x43, 0x48, 0x4b, 0x4e, 0x54,
     0xd4, 0x190, 0x1eab, 0x1ead, 0x1ebb
 };
 
 /*Collect the unicode lists and glyph_id offsets*/
 static const lv_font_fmt_txt_cmap_t cmaps[] =
 {
     {
         .range_start = 32, .range_length = 7868, .glyph_id_start = 1,
         .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 13, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
     }
 };
 
 /*-----------------
  *    KERNING
  *----------------*/
 
 
 /*Map glyph_ids to kern left classes*/
 static const uint8_t kern_left_class_mapping[] =
 {
     0, 0, 1, 2, 3, 4, 5, 4,
     6, 2, 7, 0, 2, 8
 };
 
 /*Map glyph_ids to kern right classes*/
 static const uint8_t kern_right_class_mapping[] =
 {
     0, 0, 1, 2, 3, 2, 2, 4,
     5, 3, 6, 7, 3, 3
 };
 
 /*Kern values between classes*/
 static const int8_t kern_class_values[] =
 {
     0, 0, -8, 0, 0, -7, 0, -8,
     -1, -1, -1, -2, 0, 0, 0, 0,
     -1, 0, 0, 0, 0, 0, 0, -1,
     0, -2, 0, 0, 0, 0, -7, 0,
     0, 0, 0, 0, 0, -2, 0, -1,
     0, 0, 0, 14, 4, 10, 12, 10,
     14, 0, 0, 0, 0, 0, 0, 2
 };
 
 
 /*Collect the kern class' data in one place*/
 static const lv_font_fmt_txt_kern_classes_t kern_classes =
 {
     .class_pair_values   = kern_class_values,
     .left_class_mapping  = kern_left_class_mapping,
     .right_class_mapping = kern_right_class_mapping,
     .left_class_cnt      = 8,
     .right_class_cnt     = 7,
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
 const lv_font_t font_wash_12b = {
 #else
 lv_font_t font_wash_12b = {
 #endif
     .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
     .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
     .line_height = 14,          /*The maximum line height required by the font*/
     .base_line = 4,             /*Baseline measured from the bottom of the line*/
 #if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
     .subpx = LV_FONT_SUBPX_NONE,
 #endif
 #if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
     .underline_position = -1,
     .underline_thickness = 1,
 #endif
     .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
 #if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
     .fallback = NULL,
 #endif
     .user_data = NULL,
 };
 
 
 
 #endif /*#if FONT_WASH_12B*/