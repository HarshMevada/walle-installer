#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif
// ~200 common emoji, 15 px, 1 bpp (Noto Emoji, SIL Open Font License 1.1).
// Used as a fallback font so labels can show emoji next to normal text.
extern const lv_font_t chitu_emoji_font;
#ifdef __cplusplus
}
#endif
