#include <Arduino.h>
#include <PanelLan.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

namespace {

constexpr uint16_t kDisplayWidth = 320;
constexpr uint16_t kDisplayHeight = 240;
constexpr uint16_t kBufferLines = 40;
constexpr uint32_t kTelemetryPeriodMs = 5000;

PanelLan tft(BOARD_SC05_X);

// One partial RGB565 buffer (25,600 bytes). Static allocation keeps it in
// internal memory and makes the buffer lifetime unambiguous for synchronous
// LovyanGFX writes.
DMA_ATTR uint16_t drawBuffer[kDisplayWidth * kBufferLines];

lv_display_t *display = nullptr;
lv_obj_t *touchStatus = nullptr;
uint32_t lastTelemetryAt = 0;
uint32_t lastLvglTickAt = 0;
uint32_t touchCount = 0;
bool rawTouchActive = false;

void logMemory(const char *phase) {
    Serial.printf(
        "[%s] internal=%u dma=%u largest-dma=%u psram-free=%u psram-size=%u\n",
        phase,
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        heap_caps_get_free_size(MALLOC_CAP_DMA),
        heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
        ESP.getFreePsram(),
        ESP.getPsramSize());
}

void flushDisplay(lv_display_t *lvDisplay, const lv_area_t *area, uint8_t *pixelMap) {
    const uint32_t width = area->x2 - area->x1 + 1;
    const uint32_t height = area->y2 - area->y1 + 1;

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, width, height);
    tft.writePixels(reinterpret_cast<uint16_t *>(pixelMap), width * height, true);
    tft.endWrite();

    // The write is synchronous. The LVGL draw buffer is safe to reuse only
    // after LovyanGFX has returned from writePixels/endWrite.
    lv_display_flush_ready(lvDisplay);
}

void readTouch(lv_indev_t *, lv_indev_data_t *data) {
    uint16_t x = 0;
    uint16_t y = 0;
    if (tft.getTouch(&x, &y)) {
        if (!rawTouchActive) {
            Serial.printf("Raw touch: %u, %u\n", x, y);
            rawTouchActive = true;
        }
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
        return;
    }

    rawTouchActive = false;
    data->state = LV_INDEV_STATE_RELEASED;
}

void updateTouchStatus(lv_event_t *event) {
    ++touchCount;
    Serial.printf("Touch confirmed: %lu\n", touchCount);
    lv_label_set_text_fmt(touchStatus, "Touch confirmed: %lu", touchCount);
    lv_obj_set_style_bg_color(
        static_cast<lv_obj_t *>(lv_event_get_target(event)),
        lv_palette_main(LV_PALETTE_GREEN),
        0);
}

void createUi() {
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0B1014), 0);
    lv_obj_set_style_pad_all(screen, 12, 0);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "IGNITRON / LVGL CHECKPOINT");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *detail = lv_label_create(screen);
    lv_label_set_text(detail, "PanelLan SC05_X  |  320 x 240 landscape\n"
                              "LVGL 9.3.0  |  40-line RGB565 buffer");
    lv_obj_set_style_text_color(detail, lv_palette_lighten(LV_PALETTE_BLUE, 2), 0);
    lv_obj_align(detail, LV_ALIGN_TOP_MID, 0, 34);

    lv_obj_t *button = lv_button_create(screen);
    lv_obj_set_size(button, 252, 76);
    lv_obj_align(button, LV_ALIGN_CENTER, 0, 8);
    lv_obj_set_style_bg_color(button, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_add_event_cb(button, updateTouchStatus, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *buttonLabel = lv_label_create(button);
    lv_label_set_text(buttonLabel, "TAP TO VERIFY TOUCH");
    lv_obj_center(buttonLabel);

    touchStatus = lv_label_create(screen);
    lv_label_set_text(touchStatus, "Touch confirmed: 0");
    lv_obj_set_style_text_color(touchStatus, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
    lv_obj_align(touchStatus, LV_ALIGN_BOTTOM_MID, 0, -12);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    const uint32_t serialDeadline = millis() + 1500;
    while (!Serial && millis() < serialDeadline) {
        delay(10);
    }

    Serial.println("Ignitron PanelLan LVGL bring-up");
    tft.begin();
    tft.setRotation(1);

    if (tft.width() != kDisplayWidth || tft.height() != kDisplayHeight) {
        Serial.printf("Unexpected display geometry: %d x %d\n", tft.width(), tft.height());
        return;
    }

    lv_init();
    lastLvglTickAt = millis();
    display = lv_display_create(kDisplayWidth, kDisplayHeight);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(
        display, drawBuffer, nullptr, sizeof(drawBuffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flushDisplay);

    lv_indev_t *touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, readTouch);
    lv_indev_set_display(touch, display);

    createUi();
    logMemory("startup");
}

void loop() {
    const uint32_t now = millis();
    lv_tick_inc(now - lastLvglTickAt);
    lastLvglTickAt = now;
    lv_timer_handler();

    if (now - lastTelemetryAt >= kTelemetryPeriodMs) {
        lastTelemetryAt = now;
        logMemory("steady-state");
    }

    delay(5);
}
