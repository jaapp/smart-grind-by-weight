/**
 * @file display_manager.cpp
 *
 * Display driver for SH8601 AMOLED (280x456, QSPI interface).
 *
 * Hardware stack:
 *   - esp_lcd SPI bus + panel-IO for QSPI command/pixel transfer
 *   - Custom SH8601 panel driver (init sequence + draw_bitmap)
 *   - Direct LVGL 9.x display/indev registration (no esp_lvgl_port)
 *   - FreeRTOS mutex for thread-safe lv_timer_handler() access
 *
 * Threading:
 *   DisplayManager::lvgl_mutex protects LVGL state.  The UIRender task calls
 *   update() -> lock() -> lv_timer_handler() -> unlock() at 50ms intervals.
 */

#include "display_manager.h"
#include "../config/constants.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_interface.h"  // Full esp_lcd_panel_t struct for custom drivers
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------------------
// Module tag / logging
// ---------------------------------------------------------------------------
static const char* TAG = "DISPLAY";

// ---------------------------------------------------------------------------
// Global instance pointer (used by static callbacks)
// ---------------------------------------------------------------------------
DisplayManager* g_display_manager = nullptr;

// ---------------------------------------------------------------------------
// SH8601 command constants
// ---------------------------------------------------------------------------
namespace sh8601 {
    static constexpr uint8_t CMD_SWRESET  = 0x01;
    static constexpr uint8_t CMD_SLPOUT   = 0x11;
    static constexpr uint8_t CMD_INVOFF   = 0x20;
    static constexpr uint8_t CMD_INVON    = 0x21;
    static constexpr uint8_t CMD_DISPOFF  = 0x28;
    static constexpr uint8_t CMD_DISPON   = 0x29;
    static constexpr uint8_t CMD_CASET    = 0x2A;
    static constexpr uint8_t CMD_RASET    = 0x2B;
    static constexpr uint8_t CMD_RAMWR    = 0x2C;
    static constexpr uint8_t CMD_TEON     = 0x35;
    static constexpr uint8_t CMD_MADCTL   = 0x36;
    static constexpr uint8_t CMD_COLMOD   = 0x3A;
    static constexpr uint8_t CMD_WRDISBV  = 0x51;  // Write display brightness
    static constexpr uint8_t CMD_WRCTRLD  = 0x53;  // Write ctrl display
    static constexpr uint8_t CMD_PAGESEL  = 0xFE;  // Page select

    // MADCTL bits
    static constexpr uint8_t MADCTL_MY    = 0x80;  // Row address order (flip vertical)
    static constexpr uint8_t MADCTL_MX    = 0x40;  // Column address order (flip horizontal)
    static constexpr uint8_t MADCTL_RGB   = 0x00;  // RGB colour order

    // COLMOD: 16-bit RGB565
    static constexpr uint8_t COLMOD_RGB565 = 0x55;

    // WRCTRLD: brightness control enabled
    static constexpr uint8_t CTRLD_BCTRL  = 0x20;

    // MADCTL value for 180 degree rotation: both MX and MY set
    static constexpr uint8_t MADCTL_180   = MADCTL_MY | MADCTL_MX | MADCTL_RGB;
} // namespace sh8601

// ---------------------------------------------------------------------------
// SH8601 custom panel driver
//
// esp_lcd has no official SH8601 component, so we implement the vtable
// manually.  Pixel data is pushed via esp_lcd_panel_io_tx_color which
// drives the QSPI DMA engine.
// ---------------------------------------------------------------------------

struct sh8601_panel_t {
    esp_lcd_panel_t           base;           // MUST be first member
    esp_lcd_panel_io_handle_t io;
    int                       reset_gpio;
    uint32_t                  x_gap;
    uint32_t                  y_gap;
    uint8_t                   madctl_val;
    uint32_t                  fb_bits_per_pixel;
    uint32_t                  width;
    uint32_t                  height;
};

// Helper: send a command with optional parameters
static esp_err_t sh8601_tx_param(esp_lcd_panel_io_handle_t io,
                                  uint8_t cmd, const uint8_t* param, size_t param_len)
{
    return esp_lcd_panel_io_tx_param(io, cmd, param, param_len);
}

// Helper: send a no-parameter command
static esp_err_t sh8601_tx_cmd(esp_lcd_panel_io_handle_t io, uint8_t cmd)
{
    return esp_lcd_panel_io_tx_param(io, cmd, nullptr, 0);
}

// ---------------------------------------------------------------------------
// Panel vtable implementations
// ---------------------------------------------------------------------------

static esp_err_t sh8601_reset(esp_lcd_panel_t* panel)
{
    sh8601_panel_t* ctx = __containerof(panel, sh8601_panel_t, base);

    if (ctx->reset_gpio >= 0) {
        gpio_set_level(static_cast<gpio_num_t>(ctx->reset_gpio), 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(static_cast<gpio_num_t>(ctx->reset_gpio), 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    } else {
        sh8601_tx_cmd(ctx->io, sh8601::CMD_SWRESET);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    return ESP_OK;
}

static esp_err_t sh8601_init(esp_lcd_panel_t* panel)
{
    sh8601_panel_t* ctx = __containerof(panel, sh8601_panel_t, base);
    esp_lcd_panel_io_handle_t io = ctx->io;

    // Page 0 (user page)
    const uint8_t page0 = 0x00;
    sh8601_tx_param(io, sh8601::CMD_PAGESEL, &page0, 1);

    // Tearing effect on
    const uint8_t te_mode = 0x00;
    sh8601_tx_param(io, sh8601::CMD_TEON, &te_mode, 1);

    // Colour format: RGB565
    const uint8_t colmod = sh8601::COLMOD_RGB565;
    sh8601_tx_param(io, sh8601::CMD_COLMOD, &colmod, 1);

    // Brightness control enabled
    const uint8_t ctrld = sh8601::CTRLD_BCTRL;
    sh8601_tx_param(io, sh8601::CMD_WRCTRLD, &ctrld, 1);

    // Initial brightness: max
    const uint8_t brightness = 0xFF;
    sh8601_tx_param(io, sh8601::CMD_WRDISBV, &brightness, 1);

    // MADCTL: rotation
    sh8601_tx_param(io, sh8601::CMD_MADCTL, &ctx->madctl_val, 1);

    // Sleep out — requires >=120 ms before display-on
    sh8601_tx_cmd(io, sh8601::CMD_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));

    // Display on
    sh8601_tx_cmd(io, sh8601::CMD_DISPON);
    vTaskDelay(pdMS_TO_TICKS(20));

    return ESP_OK;
}

static esp_err_t sh8601_del(esp_lcd_panel_t* panel)
{
    sh8601_panel_t* ctx = __containerof(panel, sh8601_panel_t, base);
    free(ctx);
    return ESP_OK;
}

static esp_err_t sh8601_draw_bitmap(esp_lcd_panel_t* panel,
                                    int x_start, int y_start,
                                    int x_end, int y_end,
                                    const void* color_data)
{
    sh8601_panel_t* ctx = __containerof(panel, sh8601_panel_t, base);

    // Apply gap offsets
    x_start += static_cast<int>(ctx->x_gap);
    x_end   += static_cast<int>(ctx->x_gap);
    y_start += static_cast<int>(ctx->y_gap);
    y_end   += static_cast<int>(ctx->y_gap);

    // Column address set
    uint8_t caset[4] = {
        static_cast<uint8_t>((x_start >> 8) & 0xFF),
        static_cast<uint8_t>( x_start       & 0xFF),
        static_cast<uint8_t>(((x_end - 1) >> 8) & 0xFF),
        static_cast<uint8_t>( (x_end - 1)       & 0xFF),
    };
    sh8601_tx_param(ctx->io, sh8601::CMD_CASET, caset, sizeof(caset));

    // Row address set
    uint8_t raset[4] = {
        static_cast<uint8_t>((y_start >> 8) & 0xFF),
        static_cast<uint8_t>( y_start       & 0xFF),
        static_cast<uint8_t>(((y_end - 1) >> 8) & 0xFF),
        static_cast<uint8_t>( (y_end - 1)       & 0xFF),
    };
    sh8601_tx_param(ctx->io, sh8601::CMD_RASET, raset, sizeof(raset));

    // Write memory (pixel data) via DMA
    size_t pixel_count = static_cast<size_t>(x_end - x_start) *
                         static_cast<size_t>(y_end - y_start);
    size_t data_size   = pixel_count * ctx->fb_bits_per_pixel / 8;

    return esp_lcd_panel_io_tx_color(ctx->io, sh8601::CMD_RAMWR,
                                     color_data, data_size);
}

static esp_err_t sh8601_mirror(esp_lcd_panel_t* panel, bool mirror_x, bool mirror_y)
{
    sh8601_panel_t* ctx = __containerof(panel, sh8601_panel_t, base);

    if (mirror_x) {
        ctx->madctl_val |= sh8601::MADCTL_MX;
    } else {
        ctx->madctl_val &= static_cast<uint8_t>(~sh8601::MADCTL_MX);
    }
    if (mirror_y) {
        ctx->madctl_val |= sh8601::MADCTL_MY;
    } else {
        ctx->madctl_val &= static_cast<uint8_t>(~sh8601::MADCTL_MY);
    }
    return sh8601_tx_param(ctx->io, sh8601::CMD_MADCTL, &ctx->madctl_val, 1);
}

static esp_err_t sh8601_swap_xy(esp_lcd_panel_t* panel, bool swap_axes)
{
    (void)panel;
    (void)swap_axes;
    ESP_LOGW(TAG, "swap_xy not supported by SH8601 MADCTL");
    return ESP_OK;
}

static esp_err_t sh8601_set_gap(esp_lcd_panel_t* panel, int x_gap, int y_gap)
{
    sh8601_panel_t* ctx = __containerof(panel, sh8601_panel_t, base);
    ctx->x_gap = static_cast<uint32_t>(x_gap);
    ctx->y_gap = static_cast<uint32_t>(y_gap);
    return ESP_OK;
}

static esp_err_t sh8601_invert_color(esp_lcd_panel_t* panel, bool invert)
{
    sh8601_panel_t* ctx = __containerof(panel, sh8601_panel_t, base);
    uint8_t cmd = invert ? sh8601::CMD_INVON : sh8601::CMD_INVOFF;
    return sh8601_tx_cmd(ctx->io, cmd);
}

static esp_err_t sh8601_disp_on_off(esp_lcd_panel_t* panel, bool on_off)
{
    sh8601_panel_t* ctx = __containerof(panel, sh8601_panel_t, base);
    uint8_t cmd = on_off ? sh8601::CMD_DISPON : sh8601::CMD_DISPOFF;
    return sh8601_tx_cmd(ctx->io, cmd);
}

/**
 * Allocate and return a new SH8601 panel handle.
 *
 * @param io          Panel IO handle (already created for the QSPI bus)
 * @param reset_gpio  GPIO number of RESET pin (-1 to use SW reset only)
 * @param out_panel   Returned panel handle
 */
static esp_err_t sh8601_new_panel(esp_lcd_panel_io_handle_t io,
                                   int reset_gpio,
                                   esp_lcd_panel_handle_t* out_panel)
{
    sh8601_panel_t* ctx = static_cast<sh8601_panel_t*>(
        calloc(1, sizeof(sh8601_panel_t)));
    if (!ctx) {
        return ESP_ERR_NO_MEM;
    }

    ctx->io                = io;
    ctx->reset_gpio        = reset_gpio;
    ctx->x_gap             = HW_DISPLAY_OFFSET_X_PX;
    ctx->y_gap             = 0;
    ctx->width             = HW_DISPLAY_WIDTH_PX;
    ctx->height            = HW_DISPLAY_HEIGHT_PX;
    ctx->fb_bits_per_pixel = 16; // RGB565

#if HW_DISPLAY_ROTATE_180
    ctx->madctl_val = sh8601::MADCTL_180;
#else
    ctx->madctl_val = sh8601::MADCTL_RGB;
#endif

    // Configure reset GPIO as output if provided
    if (reset_gpio >= 0) {
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask = (1ULL << reset_gpio);
        io_conf.mode         = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en   = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type    = GPIO_INTR_DISABLE;
        gpio_config(&io_conf);
    }

    // Populate the vtable
    ctx->base.reset        = sh8601_reset;
    ctx->base.init         = sh8601_init;
    ctx->base.del          = sh8601_del;
    ctx->base.draw_bitmap  = sh8601_draw_bitmap;
    ctx->base.mirror       = sh8601_mirror;
    ctx->base.swap_xy      = sh8601_swap_xy;
    ctx->base.set_gap      = sh8601_set_gap;
    ctx->base.invert_color = sh8601_invert_color;
    ctx->base.disp_on_off  = sh8601_disp_on_off;

    *out_panel = &ctx->base;
    return ESP_OK;
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
    buscfg.data0_io_num    = HW_DISPLAY_D0_PIN;
    buscfg.data1_io_num    = HW_DISPLAY_D1_PIN;
    buscfg.data2_io_num    = HW_DISPLAY_D2_PIN;
    buscfg.data3_io_num    = HW_DISPLAY_D3_PIN;
    buscfg.sclk_io_num     = HW_DISPLAY_SCK_PIN;
    buscfg.mosi_io_num     = -1;  // not used separately in QSPI mode
    buscfg.miso_io_num     = -1;
    buscfg.max_transfer_sz = HW_DISPLAY_WIDTH_PX * 80 * sizeof(uint16_t);
    buscfg.flags           = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD;

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return;
    }

    // ------------------------------------------------------------------
    // 2. Panel IO (SPI -> QSPI command/data interface)
    //
    //    SH8601 uses a 32-bit command word: 8-bit opcode | 24-bit address.
    //    Parameters are 8-bit each.
    // ------------------------------------------------------------------
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num         = HW_DISPLAY_CS_PIN;
    io_config.pclk_hz             = 40 * 1000 * 1000; // 40 MHz
    io_config.lcd_cmd_bits        = 32;  // 8-bit cmd + 24-bit address
    io_config.lcd_param_bits      = 8;
    io_config.spi_mode            = 0;
    io_config.trans_queue_depth   = 10;
    io_config.on_color_trans_done = on_color_trans_done;
    io_config.user_ctx            = this;
    io_config.flags.quad_mode     = 1;  // IDF 5.x: quad_spi renamed to quad_mode

    err = esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)SPI2_HOST,  // IDF 5.x: int typedef, safe cast
        &io_config, &io_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Panel IO init failed: %s", esp_err_to_name(err));
        return;
    }

    // ------------------------------------------------------------------
    // 3. Custom SH8601 panel
    // ------------------------------------------------------------------
    err = sh8601_new_panel(io_handle, HW_DISPLAY_RESET_PIN, &panel_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SH8601 panel alloc failed: %s", esp_err_to_name(err));
        return;
    }

    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);

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
    //    Prefer PSRAM so internal DRAM stays available for task stacks.
    // ------------------------------------------------------------------
    const size_t buf_pixels = HW_DISPLAY_WIDTH_PX * 40;
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
    // 6. Create LVGL display and register it (direct LVGL 9.x API)
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

    // Full-width rounding: snap dirty regions to full display width to
    // avoid column-shift artefacts on the SH8601.
    lv_display_add_event_cb(lvgl_display, lvgl_rounder_cb,
                            LV_EVENT_INVALIDATE_AREA, nullptr);

    // ------------------------------------------------------------------
    // 8. Touch input device
    // ------------------------------------------------------------------
    touch_driver.init();

    lvgl_input = lv_indev_create();
    lv_indev_set_type(lvgl_input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvgl_input, touchpad_read_cb);

    initialized = true;
    ESP_LOGI(TAG, "Display init complete (%"PRIu32"x%"PRIu32")",
             screen_width, screen_height);
}

// ---------------------------------------------------------------------------
// DisplayManager::update
//
// Called from the UIRender FreeRTOS task (50 ms interval).
// Touch data is polled first; LVGL rendering is guarded by the port mutex.
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

void DisplayManager::update()
{
    if (!initialized) return;

    touch_driver.update();

    if (lock(0)) {
        lv_timer_handler();
        unlock();
    }
}

// ---------------------------------------------------------------------------
// DisplayManager::set_brightness
//
// Writes WRDISBV (0x51) directly via the panel IO.
// brightness: 0.0 (off) ... 1.0 (full)
// ---------------------------------------------------------------------------

void DisplayManager::set_brightness(float brightness)
{
    if (!initialized || !io_handle) return;

    brightness = std::max(0.0f, std::min(1.0f, brightness));
    uint8_t value = static_cast<uint8_t>(brightness * 255.0f);

    esp_err_t err = esp_lcd_panel_io_tx_param(io_handle, sh8601::CMD_WRDISBV,
                                               &value, sizeof(value));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_brightness tx failed: %s", esp_err_to_name(err));
    }
}

// ---------------------------------------------------------------------------
// Static callbacks
// ---------------------------------------------------------------------------

/**
 * DMA transfer complete callback (called from ISR context by the SPI driver).
 * Signals LVGL that the flush buffer is free for the next render pass.
 */
bool DisplayManager::on_color_trans_done(esp_lcd_panel_io_handle_t /*io*/,
                                          esp_lcd_panel_io_event_data_t* /*edata*/,
                                          void* user_ctx)
{
    DisplayManager* self = static_cast<DisplayManager*>(user_ctx);
    if (self && self->lvgl_display) {
        lv_display_flush_ready(self->lvgl_display);
    }
    return false; // false = no high-priority task woken from ISR
}

/**
 * LVGL flush callback.
 *
 * Calls esp_lcd_panel_draw_bitmap() which invokes our sh8601_draw_bitmap
 * vtable function: it issues CASET/RASET then esp_lcd_panel_io_tx_color for
 * DMA transfer.  The SPI DMA completion ISR fires on_color_trans_done(), which
 * calls lv_display_flush_ready().  Do NOT call lv_display_flush_ready() here.
 */
void DisplayManager::lvgl_flush_cb(lv_display_t* disp,
                                    const lv_area_t* area,
                                    uint8_t* px_map)
{
    if (!g_display_manager || !g_display_manager->panel_handle) {
        lv_display_flush_ready(disp);
        return;
    }

    // esp_lcd draw_bitmap end coordinates are exclusive (x_end = x2+1, y_end = y2+1)
    esp_lcd_panel_draw_bitmap(g_display_manager->panel_handle,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              px_map);
}

/**
 * Invalidate-area rounder: snap every dirty rectangle to full display width.
 */
void DisplayManager::lvgl_rounder_cb(lv_event_t* e)
{
    lv_area_t* area = static_cast<lv_area_t*>(lv_event_get_param(e));
    if (!area || !g_display_manager) return;

    area->x1 = 0;
    area->x2 = static_cast<int32_t>(g_display_manager->screen_width) - 1;
}

/**
 * Touch input read callback.
 */
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

/**
 * LVGL tick callback.
 * Returns milliseconds since boot using esp_timer (IDF, no Arduino dependency).
 */
uint32_t DisplayManager::lvgl_tick_cb()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}
