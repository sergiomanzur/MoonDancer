#include "settings.h"

const char *mw_display_label(int mode)
{
    switch (mode) {
    case MW_DISPLAY_ORIGINAL: return "ORIGINAL";
    case MW_DISPLAY_16_10:    return "16:10";
    case MW_DISPLAY_21_9:     return "21:9";
    case MW_DISPLAY_16_9:
    default:                  return "16:9";
    }
}

int mw_display_crop_width(int mode)
{
    switch (mode) {
    case MW_DISPLAY_ORIGINAL: return 320;
    case MW_DISPLAY_16_10:    return 358;
    case MW_DISPLAY_21_9:     return 523;
    case MW_DISPLAY_16_9:
    default:                  return 398;
    }
}

int mw_display_crop_x(int mode, int view_width)
{
    int w = mw_display_crop_width(mode);
    if (view_width <= 0) return 0;
    if (w > view_width) w = view_width;
    return (view_width - w) / 2;
}

