/**
 * @file display_manager.cpp
 *
 * Display driver for CO5300/SH8601 AMOLED (280x456, QSPI interface).
 *
 * Uses Espressif's esp_lcd_sh8601 component with CO5300-specific init commands.
 * LVGL 9.x display/indev registered directly with FreeRTOS mutex.
 */

#include "display_manager.h"
#include "../config/constants.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>
#include <algorithm>

static const char* TAG = "DISPLAY";

DisplayManager* g_display_manager = nullptr;

// ---------------------------------------------------------------------------
// CO5300 vendor-specific init commands (from kodediy/esp_lcd_co5300)
// ---------------------------------------------------------------------------
static const sh8601_lcd_init_cmd_t co5300_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},                             // Sleep Out + 120ms
    {0x35, (uint8_t[]){0x00}, 1, 0},                               // Tearing Effect ON (vsync line)
    {0xFE, (uint8_t[]){0x00}, 1, 0},                               // Page switch (page 0)
    {0xC4, (uint8_t[]){0x80}, 1, 0},                               // SPI Mode: enable QSPI data
    {0x3A, (uint8_t[]){0x55}, 1, 0},                               // COLMOD: RGB565
    {0x53, (uint8_t[]){0x20}, 1, 0},                               // Brightness control enabled
    {0x63, (uint8_t[]){0xFF}, 1, 0},                               // HBM brightness max
    // CASET/RASET omitted — draw_bitmap sets them per-frame with correct gap offset
    {0x29, (uint8_t[]){0x00}, 0, 0},                               // Display ON
    {0x51, (uint8_t[]){0xD0}, 1, 0},                               // Normal brightness
    {0x58, (uint8_t[]){0x00}, 1, 0},                               // Contrast enhancement OFF
};

// ---------------------------------------------------------------------------
// DMA flush-done callback (ISR context)
// ---------------------------------------------------------------------------
static bool on_color_trans_done(esp_lcd_panel_io_handle_t /*io*/,
                                esp_lcd_panel_io_event_data_t* /*edata*/,
                                void* user_ctx)
{
    DisplayManager* self = static_cast<DisplayManager*>(user_ctx);
    if (self && self->lvgl_display) {
        lv_display_flush_ready(self->lvgl_display);
    }
    return false;
}

// ---------------------------------------------------------------------------
// DisplayManager public lock/unlock
// ---------------------------------------------------------------------------

bool DisplayManager::lock(uint32_t timeout_ms)
{
    if (!lvgl_mutex) return false;
    TickType_t ticks = (timeout_ms == portMAX_DELAY)
                       ? portMAX_DELAY
                       : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mutex, ticks) == pdTRUE;
}

void DisplayManager::unlock()
{
    if (lvgl_mutex) xSemaphoreGive(lvgl_mutex);
}

// ---------------------------------------------------------------------------
// DisplayManager::init
// ---------------------------------------------------------------------------

void DisplayManager::init()
{
    g_display_manager = this;

    // ------------------------------------------------------------------
    // 1. SPI bus (QSPI)
    // ------------------------------------------------------------------
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num    = HW_DISPLAY_SCK_PIN;
    buscfg.data0_io_num   = HW_DISPLAY_D0_PIN;
    buscfg.data1_io_num   = HW_DISPLAY_D1_PIN;
    buscfg.data2_io_num   = HW_DISPLAY_D2_PIN;
    buscfg.data3_io_num   = HW_DISPLAY_D3_PIN;
    buscfg.max_transfer_sz = HW_DISPLAY_WIDTH_PX * 80 * sizeof(uint16_t);

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return;
    }

    // ------------------------------------------------------------------
    // 2. Panel IO (QSPI config from Espressif's SH8601_PANEL_IO_QSPI_CONFIG)
    // ------------------------------------------------------------------
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num         = HW_DISPLAY_CS_PIN;
    io_config.dc_gpio_num         = -1;      // QSPI: no D/C line
    io_config.spi_mode            = 0;
    io_config.pclk_hz             = 80 * 1000 * 1000;  // 80 MHz QSPI (CO5300 max)
    io_config.trans_queue_depth   = 10;
    io_config.on_color_trans_done = on_color_trans_done;
    io_config.user_ctx            = this;
    io_config.lcd_cmd_bits        = 32;      // instruction(8) + address(24)
    io_config.lcd_param_bits      = 8;
    io_config.flags.quad_mode     = true;

    err = esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)SPI2_HOST,
        &io_config, &io_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Panel IO init failed: %s", esp_err_to_name(err));
        return;
    }

    // ------------------------------------------------------------------
    // 3. SH8601/CO5300 panel via Espressif component
    // ------------------------------------------------------------------
    sh8601_vendor_config_t vendor_cfg = {};
    vendor_cfg.init_cmds = co5300_init_cmds;
    vendor_cfg.init_cmds_size = sizeof(co5300_init_cmds) / sizeof(co5300_init_cmds[0]);
    vendor_cfg.flags.use_qspi_interface = 1;

    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num = HW_DISPLAY_RESET_PIN;
    panel_cfg.rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_cfg.bits_per_pixel = 16;
    panel_cfg.vendor_config  = &vendor_cfg;

    err = esp_lcd_new_panel_sh8601(io_handle, &panel_cfg, &panel_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Panel create failed: %s", esp_err_to_name(err));
        return;
    }

    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);

    // Set 180° rotation via MADCTL (MX | MY)
#if HW_DISPLAY_ROTATE_180
    esp_lcd_panel_mirror(panel_handle, true, false);
    // CO5300 doesn't support mirror_y via the driver, set MADCTL directly
    // Actually the CO5300 uses MX for column flip. For 180° we need both flips.
    // The Espressif driver only supports mirror_x, so we handle MADCTL manually:
    {
        uint8_t madctl = 0x40 | 0x80; // MX | MY
        int lcd_cmd = 0x36;           // MADCTL register
        lcd_cmd &= 0xff;
        lcd_cmd <<= 8;
        lcd_cmd |= (0x02 << 24);     // QSPI write instruction
        esp_lcd_panel_io_tx_param(io_handle, lcd_cmd, &madctl, 1);
    }
#endif

    // Turn display on
    esp_lcd_panel_disp_on_off(panel_handle, true);

    // CO5300 column gap — the Arduino_CO5300 driver uses col_offset=0 and the
    // panel init CASET (which we send in vendor commands) handles the addressing.
    // Some CO5300 modules need gap=22, but the Waveshare 1.64" uses gap=0.
    esp_lcd_panel_set_gap(panel_handle, 20, 0);

    // ------------------------------------------------------------------
    // 4. LVGL init + thread-safety mutex
    // ------------------------------------------------------------------
    lv_init();
    lv_tick_set_cb(lvgl_tick_cb);

    lvgl_mutex = xSemaphoreCreateMutex();
    if (!lvgl_mutex) {
        ESP_LOGE(TAG, "Failed to create LVGL mutex");
        return;
    }

    // ------------------------------------------------------------------
    // 5. Allocate LVGL draw buffers (double-buffered, 40 rows each)
    // ------------------------------------------------------------------
    const size_t buf_pixels = HW_DISPLAY_WIDTH_PX * 80;  // 80 rows per buffer — fewer DMA round-trips
    const size_t buf_bytes  = buf_pixels * sizeof(lv_color_t);

    draw_buf1 = static_cast<lv_color_t*>(
        heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, buf_bytes,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!draw_buf1) {
        draw_buf1 = static_cast<lv_color_t*>(
            heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, buf_bytes,
                                    MALLOC_CAP_8BIT));
    }
    if (!draw_buf1) {
        ESP_LOGE(TAG, "Failed to allocate draw_buf1");
        return;
    }

    draw_buf2 = static_cast<lv_color_t*>(
        heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, buf_bytes,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!draw_buf2) {
        draw_buf2 = static_cast<lv_color_t*>(
            heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, buf_bytes,
                                    MALLOC_CAP_8BIT));
    }
    if (!draw_buf2) {
        ESP_LOGW(TAG, "Failed to allocate draw_buf2; running single-buffered");
    }

    // ------------------------------------------------------------------
    // 6. Create LVGL display (direct LVGL 9.x API)
    // ------------------------------------------------------------------
    lvgl_display = lv_display_create(HW_DISPLAY_WIDTH_PX, HW_DISPLAY_HEIGHT_PX);
    if (!lvgl_display) {
        ESP_LOGE(TAG, "lv_display_create failed");
        return;
    }

    lv_display_set_color_format(lvgl_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(lvgl_display, lvgl_flush_cb);
    lv_display_set_buffers(lvgl_display, draw_buf1, draw_buf2,
                           buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Full-width rounding to avoid column-shift artefacts
    lv_display_add_event_cb(lvgl_display, lvgl_rounder_cb,
                            LV_EVENT_INVALIDATE_AREA, nullptr);

    // ------------------------------------------------------------------
    // 7. Touch input device
    // ------------------------------------------------------------------
    touch_driver.init();

    lvgl_input = lv_indev_create();
    lv_indev_set_type(lvgl_input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvgl_input, touchpad_read_cb);

    // iOS-like scroll feel: minimal dead-zone, smooth deceleration
    lv_indev_set_scroll_limit(lvgl_input, 2);   // Start scrolling after 2px (default 10) — fast flicks register
    lv_indev_set_scroll_throw(lvgl_input, 6);   // 6% deceleration per frame (default 10) — smooth coast, less edge bounce

    initialized = true;
    ESP_LOGI(TAG, "Display init complete (%" PRIu32 "x%" PRIu32 ") | %s | 80MHz QSPI | %zu-row bufs",
             screen_width, screen_height,
             draw_buf2 ? "double-buffered" : "SINGLE-buffered",
             (size_t)80);
}

// ---------------------------------------------------------------------------
// DisplayManager::update
// ---------------------------------------------------------------------------

void DisplayManager::update()
{
    if (!initialized) return;

    touch_driver.update();

    if (lock(5)) {  // Wait up to 5ms for LVGL mutex instead of skipping
        lv_timer_handler();
        unlock();
    }
}

// ---------------------------------------------------------------------------
// DisplayManager::set_brightness
// ---------------------------------------------------------------------------

void DisplayManager::set_brightness(float brightness)
{
    if (!initialized || !io_handle) return;

    brightness = std::max(0.0f, std::min(1.0f, brightness));
    uint8_t value = static_cast<uint8_t>(brightness * 255.0f);

    // Write brightness via QSPI command format
    int lcd_cmd = 0x51;  // WRDISBV
    lcd_cmd &= 0xff;
    lcd_cmd <<= 8;
    lcd_cmd |= (0x02 << 24);  // QSPI write instruction
    esp_lcd_panel_io_tx_param(io_handle, lcd_cmd, &value, sizeof(value));
}

// ---------------------------------------------------------------------------
// Static LVGL callbacks
// ---------------------------------------------------------------------------

void DisplayManager::lvgl_flush_cb(lv_display_t* disp,
                                    const lv_area_t* area,
                                    uint8_t* px_map)
{
    if (!g_display_manager || !g_display_manager->panel_handle) {
        lv_display_flush_ready(disp);
        return;
    }

    esp_lcd_panel_draw_bitmap(g_display_manager->panel_handle,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              px_map);
}

void DisplayManager::lvgl_rounder_cb(lv_event_t* e)
{
    lv_area_t* area = static_cast<lv_area_t*>(lv_event_get_param(e));
    if (!area || !g_display_manager) return;

    area->x1 = 0;
    area->x2 = static_cast<int32_t>(g_display_manager->screen_width) - 1;
}

void DisplayManager::touchpad_read_cb(lv_indev_t* /*indev*/,
                                       lv_indev_data_t* data)
{
    if (!g_display_manager) return;

    TouchData touch = g_display_manager->touch_driver.get_touch_data();

    if (touch.pressed) {
        data->state   = LV_INDEV_STATE_PRESSED;
        data->point.x = static_cast<int32_t>(touch.x);
        data->point.y = static_cast<int32_t>(touch.y);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

uint32_t DisplayManager::lvgl_tick_cb()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}
