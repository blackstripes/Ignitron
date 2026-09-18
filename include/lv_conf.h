#ifndef LV_CONF_H
#define LV_CONF_H

// The smoke-test target intentionally keeps LVGL close to its pinned upstream
// defaults.  Explicit color depth and refresh cadence make the display format
// and measurement cadence stable while the PanelLan/LovyanGFX port is proven.
#define LV_COLOR_DEPTH 16
#define LV_DEF_REFR_PERIOD 16

#endif
