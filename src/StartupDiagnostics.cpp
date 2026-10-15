#include "StartupDiagnostics.h"

#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE) && defined(PANELAN_MINI_SPI_MODE)
namespace StartupDiagnostics {
RTC_NOINIT_ATTR Breadcrumb breadcrumb;
}
#endif
