#include "display_manager.h"
#if HW_DISPLAY_VARIANT_V2
#include "esp_lcd_sh8601.h"
#endif
#include "../config/constants.h"
#include "../config/logging.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#if HW_DISPLAY_VARIANT_V2
#include <driver/spi_master.h>
#else
#include <algorithm>
#include <cstring>
#endif

DisplayManager* g_display_manager = nullptr;

#if HW_DISPLAY_VARIANT_V2
namespace {
constexpr spi_host_device_t kDisplaySpiHost = SPI2_HOST;

static const uint8_t kCmdC4[] = {0x80};
static const uint8_t kCmd35[] = {0x00};
static const uint8_t kCmd53[] = {0x20};
static const uint8_t kCmd63[] = {0xFF};
static const uint8_t kBrightnessOff[] = {0x00};
static const uint8_t kBrightnessFull[] = {0xFF};
static const sh8601_lcd_init_cmd_t kV2InitCommands[] = {
    {0x11, nullptr, 0, 80},
    {0xC4, kCmdC4, sizeof(kCmdC4), 0},
    {0x35, kCmd35, sizeof(kCmd35), 0},
    {0x53, kCmd53, sizeof(kCmd53), 1},
    {0x63, kCmd63, sizeof(kCmd63), 1},
    {0x51, kBrightnessOff, sizeof(kBrightnessOff), 1},
    {0x29, nullptr, 0, 10},
    {0x51, kBrightnessFull, sizeof(kBrightnessFull), 0},
};
}
#endif

void DisplayManager::init() {
    g_display_manager = this;
    
#if HW_DISPLAY_VARIANT_V2
    // V2 uses an SH8601 panel and GPIO 46 for chip select. The native
    // esp_lcd driver is required; the V1 Arduino_GFX path leaves V2 black.
    spi_bus_config_t bus_config = SH8601_PANEL_BUS_QSPI_CONFIG(
        HW_DISPLAY_SCK_PIN, HW_DISPLAY_D0_PIN, HW_DISPLAY_D1_PIN,
        HW_DISPLAY_D2_PIN, HW_DISPLAY_D3_PIN,
        HW_DISPLAY_WIDTH_PX * HW_DISPLAY_HEIGHT_PX * sizeof(uint16_t));
    if (spi_bus_initialize(kDisplaySpiHost, &bus_config, SPI_DMA_CH_AUTO) != ESP_OK) {
        return;
    }

    esp_lcd_panel_io_spi_config_t io_config =
        SH8601_PANEL_IO_QSPI_CONFIG(HW_DISPLAY_CS_PIN, color_transfer_done_cb, this);
    io_config.pclk_hz = HW_DISPLAY_QSPI_FREQUENCY_HZ;
    if (esp_lcd_new_panel_io_spi(
            static_cast<esp_lcd_spi_bus_handle_t>(kDisplaySpiHost),
            &io_config, &panel_io) != ESP_OK) {
        return;
    }

    sh8601_vendor_config_t vendor_config = {};
    vendor_config.init_cmds = kV2InitCommands;
    vendor_config.init_cmds_size = sizeof(kV2InitCommands) / sizeof(kV2InitCommands[0]);
    vendor_config.flags.use_qspi_interface = 1;

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = HW_DISPLAY_RESET_PIN;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    panel_config.vendor_config = &vendor_config;

    if (esp_lcd_new_panel_sh8601(panel_io, &panel_config, &panel_handle) != ESP_OK ||
        esp_lcd_panel_reset(panel_handle) != ESP_OK ||
        esp_lcd_panel_init(panel_handle) != ESP_OK ||
        esp_lcd_panel_disp_on_off(panel_handle, true) != ESP_OK) {
        return;
    }
#else
    // Initialize V1 display hardware
    bus = new Arduino_ESP32QSPI(
        HW_DISPLAY_CS_PIN, HW_DISPLAY_SCK_PIN, HW_DISPLAY_D0_PIN, 
        HW_DISPLAY_D1_PIN, HW_DISPLAY_D2_PIN, HW_DISPLAY_D3_PIN);
    
    gfx_device = new Arduino_CO5300(
        bus, HW_DISPLAY_RESET_PIN, HW_DISPLAY_ROTATION_DEG, HW_DISPLAY_WIDTH_PX, HW_DISPLAY_HEIGHT_PX,
        HW_DISPLAY_COLOR_ORDER, HW_DISPLAY_OFFSET_X_PX, HW_DISPLAY_IPS_INVERT_X, HW_DISPLAY_IPS_INVERT_Y);

    
    if (!gfx_device->begin()) {
        return;
    }
    
    gfx_device->fillScreen(RGB565_BLACK);
#endif
    
    // Initialize LVGL
    lv_init();
    lv_tick_set_cb(millis_cb);
    
#if HW_DISPLAY_VARIANT_V2
    screen_width = HW_DISPLAY_WIDTH_PX;
    screen_height = HW_DISPLAY_HEIGHT_PX;
#else
    screen_width = gfx_device->width();
    screen_height = gfx_device->height();
#endif

    // Full screen buffer, but only partial updates used
    // RGB565 format (16bit per pixel)
    draw_buffer = nullptr;
#if HW_DISPLAY_VARIANT_V2
    // Keep the DMA-capable buffer small enough to leave internal RAM for the UI
    // FreeRTOS task. LVGL splits larger invalidated areas into strips.
    const size_t draw_rows = 8; // 280 * 8 * 2 = 4,480 bytes
    buffer_size = screen_width * draw_rows * sizeof(uint16_t);
    draw_buffer = static_cast<lv_color_t*>(
        heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, buffer_size,
                                MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
#else
    dma_staging_buffer = nullptr;
    dma_staging_rows = 16;

    const size_t draw_rows = 40; // 280 * 40 * 2 = 22,400 bytes
    buffer_size = screen_width * draw_rows * sizeof(uint16_t);

    draw_buffer = static_cast<lv_color_t*>(
        heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN,
                                buffer_size,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    if (!draw_buffer) {
        draw_buffer = static_cast<lv_color_t*>(
            heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN,
                                    buffer_size,
                                    MALLOC_CAP_8BIT));
    }
#endif

    if (!draw_buffer) {
        LOG_BLE("[DISPLAY] ERROR: Failed to allocate LVGL draw buffer\n");
        return;
    }

#if !HW_DISPLAY_VARIANT_V2
    while (dma_staging_rows >= 4 && dma_staging_buffer == nullptr) {
        dma_staging_buffer = static_cast<uint16_t*>(
            heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN,
                                    screen_width * dma_staging_rows * sizeof(uint16_t),
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
        if (!dma_staging_buffer) {
            dma_staging_rows /= 2;
        }
    }

    if (!dma_staging_buffer) {
        LOG_BLE("[DISPLAY] ERROR: Failed to allocate DMA staging buffer\n");
        heap_caps_free(draw_buffer);
        draw_buffer = nullptr;
        return;
    }
#endif

    lvgl_display = lv_display_create(screen_width, screen_height);
    lv_display_set_flush_cb(lvgl_display, display_flush_cb);
    lv_display_set_buffers(lvgl_display, draw_buffer, NULL,
                          buffer_size , LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_display_add_event_cb(lvgl_display, display_rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    
    // Initialize touch
    touch_driver.init();
    lvgl_input = lv_indev_create();
    lv_indev_set_type(lvgl_input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvgl_input, touchpad_read_cb);
    
    initialized = true;
}

void DisplayManager::update() {
    if (!initialized) return;
    
    touch_driver.update();
    lv_timer_handler();
}

// Update the refresh area to be full width
// This avoids weird artifacts when partial row updates are used
void DisplayManager::display_rounder_cb(lv_event_t* e) {
    lv_area_t* area = (lv_area_t*)lv_event_get_param(e);
    
    area->x1 = 0;
    area->x2 = g_display_manager->screen_width - 1;
}

void DisplayManager::display_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
#if HW_DISPLAY_VARIANT_V2
    if (!g_display_manager || !g_display_manager->panel_handle) {
        lv_display_flush_ready(disp);
        return;
    }

    g_display_manager->pending_flush_display = disp;
    esp_err_t result = esp_lcd_panel_draw_bitmap(
        g_display_manager->panel_handle,
        area->x1 + HW_DISPLAY_OFFSET_X_PX, area->y1,
        area->x2 + HW_DISPLAY_OFFSET_X_PX + 1, area->y2 + 1,
        px_map);
    if (result != ESP_OK) {
        g_display_manager->pending_flush_display = nullptr;
        lv_display_flush_ready(disp);
    }
#else
    if (!g_display_manager || !g_display_manager->gfx_device) return;
    
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);

    uint32_t remaining_rows = h;
    uint32_t current_y = area->y1;
    uint32_t src_row_offset = 0;
    uint16_t* staging = g_display_manager->dma_staging_buffer;
    uint32_t staging_rows = g_display_manager->dma_staging_rows ? g_display_manager->dma_staging_rows : h;
    const uint16_t* src_pixels = reinterpret_cast<const uint16_t*>(px_map);

    while (remaining_rows > 0) {
        uint32_t rows = std::min<uint32_t>(remaining_rows, staging_rows);
        size_t copy_pixels = static_cast<size_t>(w) * rows;
        const uint16_t* chunk_src = src_pixels + (static_cast<size_t>(src_row_offset) * w);

        if (staging) {
            memcpy(staging, chunk_src, copy_pixels * sizeof(uint16_t));

            if (LV_COLOR_16_SWAP) {
                g_display_manager->gfx_device->draw16bitBeRGBBitmap(area->x1, current_y, staging, w, rows);
            } else {
                g_display_manager->gfx_device->draw16bitRGBBitmap(area->x1, current_y, staging, w, rows);
            }
        } else {
            if (LV_COLOR_16_SWAP) {
                g_display_manager->gfx_device->draw16bitBeRGBBitmap(area->x1, current_y, const_cast<uint16_t*>(chunk_src), w, rows);
            } else {
                g_display_manager->gfx_device->draw16bitRGBBitmap(area->x1, current_y, const_cast<uint16_t*>(chunk_src), w, rows);
            }
        }

        remaining_rows -= rows;
        src_row_offset += rows;
        current_y += rows;
    }
    
    lv_display_flush_ready(disp);
#endif
}

#if HW_DISPLAY_VARIANT_V2
bool DisplayManager::color_transfer_done_cb(esp_lcd_panel_io_handle_t,
                                            esp_lcd_panel_io_event_data_t*,
                                            void* user_context) {
    DisplayManager* manager = static_cast<DisplayManager*>(user_context);
    if (manager && manager->pending_flush_display) {
        lv_display_t* display = manager->pending_flush_display;
        manager->pending_flush_display = nullptr;
        lv_display_flush_ready(display);
    }
    return false;
}
#endif

void DisplayManager::touchpad_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    if (!g_display_manager) return;
    
    TouchData touch = g_display_manager->touch_driver.get_touch_data();
    
    if (touch.pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = touch.x;
        data->point.y = touch.y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

uint32_t DisplayManager::millis_cb() {
    return millis();
}

void DisplayManager::set_brightness(float brightness) {
#if HW_DISPLAY_VARIANT_V2
    if (!initialized || !panel_io) return;
#else
    if (!initialized || !gfx_device) return;
#endif
    
    // Clamp brightness to valid hardware range [0.0, 1.0]
    if (brightness < 0.0f) brightness = 0.0f;
    if (brightness > 1.0f) brightness = 1.0f;
    
    uint8_t brightness_value = (uint8_t)(brightness * 255.0f);
#if HW_DISPLAY_VARIANT_V2
    uint32_t brightness_command = (0x02UL << 24) | (0x51UL << 8);
    esp_lcd_panel_io_tx_param(panel_io, brightness_command,
                              &brightness_value, sizeof(brightness_value));
#else
    // Cast to CO5300 and call setBrightness with 8-bit value
    Arduino_CO5300* display = static_cast<Arduino_CO5300*>(gfx_device);
    display->setBrightness(brightness_value);
#endif
}
