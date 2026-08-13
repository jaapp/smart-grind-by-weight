#include <lvgl.h>
#include <src/drivers/windows/lv_windows_display.h>

#include "config/constants.h"
#include "ui/screens/grinding_screen_arc.h"
#include "ui/screens/grinding_screen_chart.h"
#include "ui/screens/ready_screen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <windows.h>

namespace {

constexpr int kDisplayWidth = 280;
constexpr int kDisplayHeight = 456;
constexpr float kTargetWeight = USER_SINGLE_ESPRESSO_WEIGHT_G;
constexpr uint64_t kArcPixelBudget = 800000;
constexpr uint64_t kChartPixelBudget = 3500000;
constexpr uint64_t kSwipePixelBudget = 1000000;
constexpr uint32_t kSwipeDurationBudgetMs = 300;

ReadyScreen ready_screen;
GrindingScreenArc arc_screen;
GrindingScreenChart chart_screen;
lv_obj_t* grind_button = nullptr;
lv_obj_t* grind_icon = nullptr;
lv_obj_t* pulse_button = nullptr;
bool chart_layout = false;
bool grinding = false;
bool settling = false;
float weight_g = 0.0f;
float flow_gps = 0.0f;
uint32_t grind_started_ms = 0;
uint32_t last_update_ms = 0;
HWND simulator_window = nullptr;

struct RenderMetrics {
    uint64_t flushed_pixels = 0;
    uint64_t render_time_ms = 0;
    uint32_t flush_count = 0;
    uint32_t refresh_count = 0;
    uint32_t render_started_ms = 0;
};

RenderMetrics render_metrics;

uint32_t now_ms() {
    return static_cast<uint32_t>(GetTickCount64());
}

void reset_render_metrics() {
    render_metrics = {};
}

void print_render_metrics(const char* layout, uint32_t duration_ms) {
    std::printf(
        "[benchmark] %s duration=%lums refreshes=%lu flushes=%lu pixels=%llu render=%llums\n",
        layout,
        static_cast<unsigned long>(duration_ms),
        static_cast<unsigned long>(render_metrics.refresh_count),
        static_cast<unsigned long>(render_metrics.flush_count),
        static_cast<unsigned long long>(render_metrics.flushed_pixels),
        static_cast<unsigned long long>(render_metrics.render_time_ms));
    std::fflush(stdout);
}

void display_metrics_event(lv_event_t* event) {
    switch (lv_event_get_code(event)) {
        case LV_EVENT_RENDER_START:
            render_metrics.render_started_ms = now_ms();
            break;
        case LV_EVENT_RENDER_READY:
            if (render_metrics.render_started_ms != 0) {
                render_metrics.render_time_ms += now_ms() - render_metrics.render_started_ms;
                render_metrics.render_started_ms = 0;
            }
            break;
        case LV_EVENT_FLUSH_START: {
            const auto* area = static_cast<const lv_area_t*>(lv_event_get_param(event));
            if (area) {
                render_metrics.flushed_pixels += static_cast<uint64_t>(lv_area_get_size(area));
                render_metrics.flush_count++;
            }
            break;
        }
        case LV_EVENT_REFR_READY:
            render_metrics.refresh_count++;
            break;
        default:
            break;
    }
}

void set_status(const char* status) {
    if (simulator_window) {
        wchar_t title[160];
        swprintf_s(title, L"Smart Grind Simulator - %S  [V: view, T: tare]", status);
        SetWindowTextW(simulator_window, title);
    }
}

void set_grind_button(const char* symbol, uint32_t color) {
    if (grind_icon) {
        lv_image_set_src(grind_icon, symbol);
    }
    if (grind_button) {
        lv_obj_set_style_bg_color(grind_button, lv_color_hex(color), 0);
    }
}

void show_ready() {
    grinding = false;
    settling = false;
    arc_screen.hide();
    chart_screen.hide();
    ready_screen.show();
    set_grind_button(LV_SYMBOL_PLAY, THEME_COLOR_PRIMARY);
    set_status("READY");
}

void show_active_grind_screen() {
    ready_screen.hide();
    if (chart_layout) {
        arc_screen.hide();
        chart_screen.show();
    } else {
        chart_screen.hide();
        arc_screen.show();
    }
}

void reset_grind_views() {
    arc_screen.update_profile_name("SINGLE");
    chart_screen.update_profile_name("SINGLE");
    arc_screen.update_target_weight(kTargetWeight);
    chart_screen.update_target_weight(kTargetWeight);
    arc_screen.update_current_weight(0.0f);
    chart_screen.update_current_weight(0.0f);
    arc_screen.update_progress(0);
    chart_screen.reset_chart_data();
}

void start_grind() {
    weight_g = 0.0f;
    flow_gps = 0.0f;
    grinding = true;
    settling = false;
    grind_started_ms = now_ms();
    last_update_ms = grind_started_ms;
    reset_grind_views();
    show_active_grind_screen();
    set_grind_button(LV_SYMBOL_STOP, THEME_COLOR_PRIMARY);
    set_status("GRINDING");
}

void toggle_layout() {
    chart_layout = !chart_layout;
    if (!ready_screen.is_visible()) {
        show_active_grind_screen();
    }
    set_status(chart_layout ? "CHART VIEW" : "ARC VIEW");
}

void tare() {
    weight_g = 0.0f;
    arc_screen.update_tare_display();
    chart_screen.update_tare_display();
    set_status("TARE");
}

void grind_button_event(lv_event_t*) {
    if (ready_screen.is_visible()) {
        start_grind();
    } else if (grinding || settling) {
        grinding = false;
        settling = false;
        set_grind_button(LV_SYMBOL_OK, THEME_COLOR_SUCCESS);
        set_status("STOPPED");
    } else {
        show_ready();
    }
}

void create_grinder_controls() {
    grind_button = lv_button_create(lv_screen_active());
    lv_obj_set_size(grind_button, 100, 100);
    lv_obj_align(grind_button, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_radius(grind_button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(grind_button, lv_color_hex(THEME_COLOR_PRIMARY), 0);
    lv_obj_set_style_border_width(grind_button, 0, 0);
    lv_obj_set_style_shadow_width(grind_button, 0, 0);
    lv_obj_add_event_cb(grind_button, grind_button_event, LV_EVENT_CLICKED, nullptr);

    grind_icon = lv_image_create(grind_button);
    lv_image_set_src(grind_icon, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(grind_icon, &lv_font_montserrat_24, 0);
    lv_obj_center(grind_icon);

    pulse_button = lv_button_create(lv_screen_active());
    lv_obj_set_size(pulse_button, 100, 100);
    lv_obj_align(pulse_button, LV_ALIGN_BOTTOM_MID, 60, -10);
    lv_obj_set_style_radius(pulse_button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pulse_button, lv_color_hex(THEME_COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(pulse_button, 0, 0);
    lv_obj_set_style_shadow_width(pulse_button, 0, 0);
    lv_obj_add_flag(pulse_button, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* pulse_icon = lv_image_create(pulse_button);
    lv_image_set_src(pulse_icon, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(pulse_icon, &lv_font_montserrat_32, 0);
    lv_obj_center(pulse_icon);
}

void update_mock_grind() {
    if (!grinding && !settling) {
        return;
    }

    const uint32_t current_ms = now_ms();
    const float dt_s = std::min(0.1f, static_cast<float>(current_ms - last_update_ms) / 1000.0f);
    last_update_ms = current_ms;
    const float elapsed_s = static_cast<float>(current_ms - grind_started_ms) / 1000.0f;

    if (grinding) {
        flow_gps = std::min(1.75f, 0.25f + elapsed_s * 0.55f);
        weight_g += flow_gps * dt_s;
        if (weight_g >= kTargetWeight - 0.45f) {
            grinding = false;
            settling = true;
            set_status("SETTLING");
        }
    } else if (settling) {
        flow_gps *= std::pow(0.12f, dt_s);
        weight_g += flow_gps * dt_s;
        if (flow_gps < 0.015f) {
            settling = false;
            flow_gps = 0.0f;
            set_grind_button(LV_SYMBOL_OK, THEME_COLOR_SUCCESS);
            set_status("COMPLETE");
        }
    }

    const int progress = std::clamp(static_cast<int>((weight_g / kTargetWeight) * 100.0f), 0, 100);
    arc_screen.update_current_weight(weight_g);
    chart_screen.update_current_weight(weight_g);
    arc_screen.update_progress(progress);
    chart_screen.add_chart_data_point(weight_g, flow_gps, current_ms);
}

bool has_arg(int argc, char** argv, const char* expected) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], expected) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    const bool smoke_test = has_arg(argc, argv, "--smoke");
    const bool benchmark = has_arg(argc, argv, "--benchmark");
    const bool swipe_benchmark = has_arg(argc, argv, "--swipe-benchmark");

    if (smoke_test) {
        std::puts("[smoke] starting simulator");
        std::fflush(stdout);
    }

    lv_init();
    lv_tick_set_cb(now_ms);

    lv_display_t* display = lv_windows_create_display(
        L"Smart Grind-by-Weight Simulator",
        kDisplayWidth,
        kDisplayHeight,
        140,
        true,
        true);
    if (!display) {
        std::fprintf(stderr, "Could not create the simulator display.\n");
        return 1;
    }

    simulator_window = lv_windows_get_display_window_handle(display);
    if (smoke_test || benchmark || swipe_benchmark) {
        ShowWindow(simulator_window, SW_HIDE);
    }
    lv_display_add_event_cb(display, display_metrics_event, LV_EVENT_ALL, nullptr);

    // The Windows driver creates its framebuffer on the first LVGL timer pass.
    // Let that happen before sizing and styling the production screen objects.
    Sleep(LV_DEF_REFR_PERIOD + 1);
    lv_timer_handler();

    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(THEME_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);

    ready_screen.create();
    arc_screen.create();
    chart_screen.create();
    create_grinder_controls();
    show_ready();
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(display);

    if (smoke_test) {
        std::puts("[smoke] UI created");
        std::fflush(stdout);
    }

    HWND window = simulator_window;
    const uint32_t smoke_started_ms = now_ms();
    bool smoke_scenario_started = false;
    bool benchmark_switched_layout = false;
    uint64_t benchmark_arc_pixels = 0;
    bool swipe_started = false;
    uint32_t swipe_started_ms = 0;
    bool view_key_down = false;
    bool tare_key_down = false;

    while (IsWindow(window)) {
        if (swipe_benchmark && !swipe_started && now_ms() - smoke_started_ms > 100) {
            reset_render_metrics();
            swipe_started_ms = now_ms();
            lv_tabview_set_act(ready_screen.get_tabview(), 1, LV_ANIM_ON);
            swipe_started = true;
        }

        if ((smoke_test || benchmark) && !smoke_scenario_started && now_ms() - smoke_started_ms > 100) {
            start_grind();
            smoke_scenario_started = true;
            reset_render_metrics();
        }

        update_mock_grind();

        const bool view_pressed = (GetAsyncKeyState('V') & 0x8000) != 0;
        if (view_pressed && !view_key_down) {
            toggle_layout();
        }
        view_key_down = view_pressed;

        const bool tare_pressed = (GetAsyncKeyState('T') & 0x8000) != 0;
        if (tare_pressed && !tare_key_down) {
            tare();
        }
        tare_key_down = tare_pressed;
        const uint32_t wait_ms = std::clamp<uint32_t>(lv_timer_handler(), 1, 16);
        Sleep(wait_ms);

        if (swipe_benchmark && swipe_started &&
            !lv_obj_is_scrolling(lv_tabview_get_content(ready_screen.get_tabview()))) {
            const uint32_t duration_ms = now_ms() - swipe_started_ms;
            print_render_metrics("swipe", duration_ms);
            if (lv_tabview_get_tab_act(ready_screen.get_tabview()) != 1 ||
                duration_ms > kSwipeDurationBudgetMs ||
                render_metrics.flushed_pixels > kSwipePixelBudget) {
                std::fprintf(stderr, "Simulator swipe budget exceeded.\n");
                std::fflush(stderr);
                ExitProcess(4);
            }
            ExitProcess(0);
        }

        if (smoke_test && now_ms() - smoke_started_ms > 750) {
            if (!arc_screen.is_visible() || ready_screen.is_visible() || weight_g <= 0.0f) {
                std::fprintf(stderr, "Simulator scenario did not advance.\n");
                std::fflush(stderr);
                ExitProcess(2);
            }
            std::printf("[smoke] scenario advanced to %.2fg\n", weight_g);
            std::fflush(stdout);
            ExitProcess(0);
        }

        if (benchmark && !benchmark_switched_layout && now_ms() - smoke_started_ms > 2600) {
            print_render_metrics("arc", 2500);
            benchmark_arc_pixels = render_metrics.flushed_pixels;
            toggle_layout();
            benchmark_switched_layout = true;
            reset_render_metrics();
        }

        if (benchmark && benchmark_switched_layout && now_ms() - smoke_started_ms > 5100) {
            print_render_metrics("chart", 2500);
            if (benchmark_arc_pixels > kArcPixelBudget ||
                render_metrics.flushed_pixels > kChartPixelBudget) {
                std::fprintf(stderr, "Simulator render budget exceeded.\n");
                std::fflush(stderr);
                ExitProcess(3);
            }
            ExitProcess(0);
        }
    }

    lv_deinit();
    return 0;
}
