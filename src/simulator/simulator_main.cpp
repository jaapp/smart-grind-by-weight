#include <Arduino.h>
#include <lvgl.h>
#include <core/lv_refr.h>

#include "../config/constants.h"
#include "../controllers/bean_controller.h"
#include "../ui/event_bridge_lvgl.h"
#include "../ui/screens/bean_feedback_screen.h"
#include "../ui/screens/bean_list_screen.h"
#include "../ui/screens/ready_screen.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if LV_USE_SDL
extern "C" lv_display_t* lv_sdl_window_create(int32_t hor_res, int32_t ver_res);
extern "C" lv_indev_t* lv_sdl_mouse_create(void);
#endif

namespace {

BeanController beans;
ReadyScreen ready_screen;
BeanListScreen bean_list_screen;
BeanFeedbackScreen feedback_screen;
	lv_obj_t* preview_grind_button = nullptr;
	lv_display_t* offscreen_display = nullptr;
	lv_indev_t* offscreen_indev = nullptr;
	std::vector<uint8_t> offscreen_draw_buffer;
	std::vector<uint16_t> offscreen_framebuffer;
	int selected_profile = BeanController::kDoubleProfileIndex;
	int preview_grind_click_count = 0;

	struct TouchState {
	    lv_point_t point{0, 0};
	    bool pressed = false;
	};

	TouchState touch_state;

	void refresh_ready();

void offscreen_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* color_p) {
    if (!area || !color_p) {
        lv_display_flush_ready(disp);
        return;
    }

    const int32_t width = lv_area_get_width(area);
    const int32_t height = lv_area_get_height(area);
    const uint16_t* src = reinterpret_cast<const uint16_t*>(color_p);
    const uint32_t src_stride_px =
        lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_RGB565) / sizeof(uint16_t);

    for (int32_t row = 0; row < height; ++row) {
        const int32_t dst_y = area->y1 + row;
        if (dst_y < 0 || dst_y >= HW_DISPLAY_HEIGHT_PX) {
            continue;
        }

        int32_t copy_x = area->x1;
        int32_t src_x = 0;
        int32_t copy_width = width;
        if (copy_x < 0) {
            src_x = -copy_x;
            copy_width += copy_x;
            copy_x = 0;
        }
        if (copy_x + copy_width > HW_DISPLAY_WIDTH_PX) {
            copy_width = HW_DISPLAY_WIDTH_PX - copy_x;
        }
        if (copy_width <= 0) {
            continue;
        }

        uint16_t* dst = offscreen_framebuffer.data() +
                        (static_cast<size_t>(dst_y) * HW_DISPLAY_WIDTH_PX) + copy_x;
        std::memcpy(dst, src + (static_cast<size_t>(row) * src_stride_px) + src_x,
                    static_cast<size_t>(copy_width) * sizeof(uint16_t));
    }
    lv_display_flush_ready(disp);
}

void offscreen_rounder_cb(lv_event_t* e) {
    lv_area_t* area = static_cast<lv_area_t*>(lv_event_get_param(e));
    if (!area) {
        return;
    }

    area->x1 = 0;
    area->x2 = HW_DISPLAY_WIDTH_PX - 1;
}

void offscreen_touch_read_cb(lv_indev_t*, lv_indev_data_t* data) {
    data->point = touch_state.point;
    data->state = touch_state.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

bool create_offscreen_display() {
    lv_tick_set_cb(millis);

    offscreen_display = lv_display_create(HW_DISPLAY_WIDTH_PX, HW_DISPLAY_HEIGHT_PX);
    if (!offscreen_display) {
        std::fprintf(stderr, "Failed to create offscreen LVGL display\n");
        return false;
    }

    offscreen_framebuffer.assign(static_cast<size_t>(HW_DISPLAY_WIDTH_PX) *
                                     HW_DISPLAY_HEIGHT_PX,
                                 0);

    constexpr uint32_t kRenderRows = 64;
    const uint32_t buffer_size = HW_DISPLAY_WIDTH_PX * kRenderRows * sizeof(uint16_t);
    offscreen_draw_buffer.resize(buffer_size + LV_DRAW_BUF_ALIGN);
    std::fill(offscreen_draw_buffer.begin(), offscreen_draw_buffer.end(), 0);

    void* aligned_buffer = lv_draw_buf_align(offscreen_draw_buffer.data(), LV_COLOR_FORMAT_RGB565);
    const auto offset = static_cast<uint32_t>(
        static_cast<uint8_t*>(aligned_buffer) - offscreen_draw_buffer.data());

    lv_display_set_color_format(offscreen_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(offscreen_display, aligned_buffer, nullptr,
                           static_cast<uint32_t>(offscreen_draw_buffer.size()) - offset,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
	    lv_display_set_flush_cb(offscreen_display, offscreen_flush_cb);
	    lv_display_add_event_cb(offscreen_display, offscreen_rounder_cb, LV_EVENT_INVALIDATE_AREA,
	                            nullptr);
	    lv_display_set_default(offscreen_display);

	    offscreen_indev = lv_indev_create();
	    if (!offscreen_indev) {
	        std::fprintf(stderr, "Failed to create offscreen LVGL input device\n");
	        return false;
	    }
	    lv_indev_set_type(offscreen_indev, LV_INDEV_TYPE_POINTER);
	    lv_indev_set_display(offscreen_indev, offscreen_display);
	    lv_indev_set_read_cb(offscreen_indev, offscreen_touch_read_cb);
	    return true;
	}

bool create_interactive_display() {
#if LV_USE_SDL
    if (!lv_sdl_window_create(HW_DISPLAY_WIDTH_PX, HW_DISPLAY_HEIGHT_PX)) {
        std::fprintf(stderr, "Failed to create SDL preview window\n");
        return false;
    }
    lv_sdl_mouse_create();
    return true;
#else
    std::fprintf(stderr, "LV_USE_SDL is disabled; build the lvgl-sdl-preview environment.\n");
    return false;
#endif
}

void create_preview_grind_button() {
    preview_grind_button = lv_btn_create(lv_scr_act());
    lv_obj_set_size(preview_grind_button, 96, 96);
    lv_obj_align(preview_grind_button, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_radius(preview_grind_button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(preview_grind_button, lv_color_hex(THEME_COLOR_PRIMARY), 0);
    lv_obj_set_style_border_width(preview_grind_button, 0, 0);

	    lv_obj_t* label = lv_label_create(preview_grind_button);
	    lv_label_set_text(label, "GRIND");
	    lv_obj_set_width(label, 86);
	    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
	    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
	    lv_obj_center(label);

	    lv_obj_add_event_cb(preview_grind_button, [](lv_event_t* e) {
	        if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
	            return;
	        }
	        if (selected_profile >= USER_PROFILE_COUNT) {
	            selected_profile = BeanController::kDoubleProfileIndex;
	            refresh_ready();
	        }
	        ++preview_grind_click_count;
	    }, LV_EVENT_CLICKED, nullptr);
	}

void seed_beans() {
    beans.init();
    if (beans.count() > 0) {
        return;
    }
    beans.create_bean("Monte Alegre", "Fjord", 250, 26.5f);
    beans.create_bean("House Blend", "Five Elephant", 250, 24.0f);
    beans.create_bean("Kii AA", "The Barn", 200, 29.5f);
    beans.set_active_bean(1);
    beans.add_dose_used_g(54.0f);
    beans.add_purge_used_g(2.0f);
}

void refresh_ready() {
    const float values[USER_PROFILE_COUNT] = {
        USER_SINGLE_ESPRESSO_WEIGHT_G,
        USER_DOUBLE_ESPRESSO_WEIGHT_G,
        USER_CUSTOM_PROFILE_WEIGHT_G,
    };
    ready_screen.update_profile_values(values, GrindMode::WEIGHT);

    const BeanRecord* active = beans.get_active_bean();
    if (active) {
        ready_screen.update_bean_summary(true, active->name, active->roaster,
                                         active->mahlgrad_x2[BeanController::kDoubleProfileIndex],
                                         active->dose_used_x10,
                                         active->purge_used_x10,
                                         active->bag_size_g);
    } else {
        ready_screen.update_bean_summary(false, nullptr, nullptr,
                                         BeanController::kDefaultMahlgradX2,
                                         0, 0, 0);
    }
    ready_screen.set_active_tab(selected_profile);
}

void show_scene(const char* scene) {
    ready_screen.hide();
    bean_list_screen.hide();
    feedback_screen.hide();
    if (preview_grind_button) {
        lv_obj_add_flag(preview_grind_button, LV_OBJ_FLAG_HIDDEN);
    }

    if (std::strcmp(scene, "beans") == 0 || std::strcmp(scene, "list") == 0) {
        bean_list_screen.update(&beans);
        bean_list_screen.show();
        return;
    }

    if (std::strcmp(scene, "feedback") == 0) {
        const BeanRecord* active = beans.get_active_bean();
        feedback_screen.update(active ? active->name : "Bean",
                               active ? active->mahlgrad_x2[BeanController::kDoubleProfileIndex]
                                      : BeanController::kDefaultMahlgradX2);
        feedback_screen.show();
        return;
    }

    ready_screen.show();
    if (preview_grind_button) {
        lv_obj_clear_flag(preview_grind_button, LV_OBJ_FLAG_HIDDEN);
    }
    ready_screen.set_active_tab(selected_profile);
}

bool object_is_visible(lv_obj_t* obj) {
    while (obj) {
        if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
            return false;
        }
        obj = lv_obj_get_parent(obj);
    }
    return true;
}

lv_obj_t* find_visible_button_by_label(lv_obj_t* root, const char* text) {
    if (!root || !text) {
        return nullptr;
    }

    if (object_is_visible(root) && lv_obj_check_type(root, &lv_label_class)) {
        const char* label_text = lv_label_get_text(root);
        if (label_text && std::strcmp(label_text, text) == 0) {
            return lv_obj_get_parent(root);
        }
    }

    const uint32_t child_count = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < child_count; ++i) {
        if (lv_obj_t* found = find_visible_button_by_label(lv_obj_get_child(root, i), text)) {
            return found;
        }
    }
    return nullptr;
}

lv_obj_t* find_visible_object_by_user_data(lv_obj_t* root, void* user_data) {
    if (!root) {
        return nullptr;
    }

    if (object_is_visible(root) && lv_obj_get_user_data(root) == user_data) {
        return root;
    }

    const uint32_t child_count = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < child_count; ++i) {
        if (lv_obj_t* found = find_visible_object_by_user_data(lv_obj_get_child(root, i), user_data)) {
            return found;
        }
    }
    return nullptr;
}

	bool click_object(lv_obj_t* obj, const char* label) {
	    if (!obj) {
	        std::fprintf(stderr, "[FAIL] Missing clickable object: %s\n", label ? label : "(unnamed)");
	        return false;
	    }
	    if (offscreen_indev) {
	        lv_obj_update_layout(lv_scr_act());
	        lv_area_t coords;
	        lv_obj_get_coords(obj, &coords);
	        touch_state.point.x = static_cast<lv_coord_t>((coords.x1 + coords.x2) / 2);
	        touch_state.point.y = static_cast<lv_coord_t>((coords.y1 + coords.y2) / 2);
	        touch_state.pressed = true;
	        lv_timer_handler();
	        delay(35);
	        lv_timer_handler();
	        touch_state.pressed = false;
	        lv_timer_handler();
	        delay(35);
	        lv_timer_handler();
	        return true;
	    }
	    lv_obj_send_event(obj, LV_EVENT_CLICKED, nullptr);
	    lv_timer_handler();
	    return true;
	}

bool expect_true(bool condition, const char* label) {
    if (!condition) {
        std::fprintf(stderr, "[FAIL] %s\n", label);
        return false;
    }
    std::printf("[OK] %s\n", label);
    return true;
}

void install_handlers() {
    if (lv_obj_t* card = ready_screen.get_bean_card()) {
        lv_obj_add_event_cb(card, EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<intptr_t>(EventBridgeLVGL::EventType::BEAN_CARD)));
    }
    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::PROFILE_SELECT,
                                      [](lv_event_t* e) {
                                          lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
                                          selected_profile = static_cast<int>(
                                              reinterpret_cast<intptr_t>(lv_obj_get_user_data(target)));
                                          refresh_ready();
                                      });
    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::BEAN_CARD,
                                      [](lv_event_t*) { show_scene("beans"); });
    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::BEAN_BACK,
                                      [](lv_event_t*) { show_scene("ready"); });
    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::BEAN_SELECT,
                                      [](lv_event_t* e) {
                                          lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
                                          uint8_t id = static_cast<uint8_t>(
                                              reinterpret_cast<intptr_t>(lv_obj_get_user_data(target)));
                                          beans.set_active_bean(id);
                                          refresh_ready();
                                          show_scene("ready");
                                      });
    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::BEAN_FEEDBACK_FINER,
                                      [](lv_event_t*) {
                                          beans.apply_feedback_to_active(BeanController::Feedback::FINER);
                                          refresh_ready();
                                          show_scene("feedback");
                                      });
    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::BEAN_FEEDBACK_OK,
                                      [](lv_event_t*) { show_scene("ready"); });
    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::BEAN_FEEDBACK_COARSER,
                                      [](lv_event_t*) {
                                          beans.apply_feedback_to_active(BeanController::Feedback::COARSER);
                                          refresh_ready();
                                          show_scene("feedback");
                                      });
    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::BEAN_FEEDBACK_SKIP,
                                      [](lv_event_t*) { show_scene("ready"); });
}

bool run_click_test() {
    bool ok = true;

	    show_scene("ready");
	    ok &= expect_true(ready_screen.is_visible(), "ready screen is visible");
	    ok &= expect_true(beans.get_active_id() == 1, "initial active bean is Monte Alegre");
	    preview_grind_click_count = 0;
	    ok &= click_object(preview_grind_button, "ready grind button");
	    ok &= expect_true(preview_grind_click_count == 1, "ready grind button receives a real coordinate click");
	    ok &= click_object(ready_screen.get_advanced_profile_cell(0), "single profile cell");
	    ok &= expect_true(selected_profile == 0, "single profile cell selects Single");
    ok &= click_object(ready_screen.get_advanced_profile_cell(1), "double profile cell");
    ok &= expect_true(selected_profile == 1, "double profile cell selects Double");
    ok &= click_object(ready_screen.get_advanced_profile_cell(2), "custom profile cell");
    ok &= expect_true(selected_profile == 2, "custom profile cell selects Custom");
	    ok &= click_object(ready_screen.get_advanced_profile_cell(1), "double profile cell");
	    ok &= expect_true(selected_profile == 1, "profile cells keep working before bean selection");
	    selected_profile = USER_PROFILE_COUNT;
	    ready_screen.set_active_tab(selected_profile);
	    show_scene("ready");
	    ok &= expect_true(object_is_visible(ready_screen.get_advanced_panel()),
	                      "advanced ready UI stays visible when stale menu tab is requested");
	    ok &= click_object(preview_grind_button, "grind button from stale menu tab");
	    ok &= expect_true(selected_profile == BeanController::kDoubleProfileIndex,
	                      "grind button recovers stale hidden menu tab in advanced UI");
	    ok &= expect_true(preview_grind_click_count == 2,
	                      "grind button still starts from recovered advanced UI tab");

	    ok &= click_object(ready_screen.get_bean_card(), "active bean card");
    ok &= expect_true(bean_list_screen.is_visible(), "bean card opens bean list");

    ok &= click_object(find_visible_button_by_label(lv_scr_act(), LV_SYMBOL_LEFT), "bean list back");
    ok &= expect_true(ready_screen.is_visible(), "bean list back returns to ready");

    ok &= click_object(ready_screen.get_bean_card(), "active bean card");
    ok &= expect_true(bean_list_screen.is_visible(), "bean list opens again");
    ok &= click_object(find_visible_object_by_user_data(
                           bean_list_screen.get_screen(),
                           reinterpret_cast<void*>(static_cast<intptr_t>(2))),
                       "second bean row");
    ok &= expect_true(ready_screen.is_visible(), "selecting a bean returns to ready");
    ok &= expect_true(beans.get_active_id() == 2, "selecting row updates active bean");

    show_scene("feedback");
    const BeanRecord* active = beans.get_active_bean();
    const uint16_t start_grind_size =
        active ? active->mahlgrad_x2[BeanController::kDoubleProfileIndex] : 0;
    ok &= expect_true(feedback_screen.is_visible(), "feedback screen is visible");

    ok &= click_object(find_visible_button_by_label(lv_scr_act(), "-0.5\nFiner"), "finer feedback");
    active = beans.get_active_bean();
    ok &= expect_true(feedback_screen.is_visible(), "finer stays on feedback screen");
    ok &= expect_true(active &&
                          active->mahlgrad_x2[BeanController::kDoubleProfileIndex] ==
                              start_grind_size - 1,
                      "finer decreases stored grind size by 0.5");

    ok &= click_object(find_visible_button_by_label(lv_scr_act(), "-0.5\nFiner"), "finer feedback again");
    active = beans.get_active_bean();
    ok &= expect_true(active &&
                          active->mahlgrad_x2[BeanController::kDoubleProfileIndex] ==
                              start_grind_size - 2,
                      "finer can be pressed repeatedly");

    ok &= click_object(find_visible_button_by_label(lv_scr_act(), "+0.5\nCoarser"), "coarser feedback");
    active = beans.get_active_bean();
    ok &= expect_true(active &&
                          active->mahlgrad_x2[BeanController::kDoubleProfileIndex] ==
                              start_grind_size - 1,
                      "coarser increases stored grind size by 0.5");

    ok &= click_object(find_visible_button_by_label(lv_scr_act(), "OK"), "feedback OK");
    ok &= expect_true(ready_screen.is_visible(), "OK returns to ready");

    show_scene("feedback");
    active = beans.get_active_bean();
    const uint16_t before_skip =
        active ? active->mahlgrad_x2[BeanController::kDoubleProfileIndex] : 0;
    ok &= click_object(find_visible_button_by_label(lv_scr_act(), "Skip"), "feedback skip");
    active = beans.get_active_bean();
    ok &= expect_true(ready_screen.is_visible(), "Skip returns to ready");
    ok &= expect_true(active &&
                          active->mahlgrad_x2[BeanController::kDoubleProfileIndex] == before_skip,
                      "Skip leaves stored grind size unchanged");

    while (beans.count() > 0) {
        const BeanRecord* bean = beans.get_bean_at(0);
        if (!bean || !beans.delete_bean(bean->id)) {
            ok = false;
            break;
        }
    }
    refresh_ready();
    show_scene("ready");
    ok &= expect_true(!beans.has_active_bean(), "test setup has no active bean");
    ok &= click_object(ready_screen.get_advanced_profile_cell(0), "single cell without active bean");
    ok &= expect_true(selected_profile == 0, "single profile works without active bean");
    ok &= click_object(ready_screen.get_advanced_profile_cell(2), "custom cell without active bean");
    ok &= expect_true(selected_profile == 2, "custom profile works without active bean");

    return ok;
}

bool write_offscreen_ppm(const char* path) {
    if (!path || !path[0]) {
        return false;
    }

    if (!offscreen_display || offscreen_framebuffer.empty()) {
        std::fprintf(stderr, "Offscreen display framebuffer is unavailable\n");
        return false;
    }

    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(offscreen_display);
    for (int i = 0; i < 4; ++i) {
        lv_timer_handler();
    }

    FILE* file = std::fopen(path, "wb");
    if (!file) {
        std::fprintf(stderr, "Failed to open snapshot output: %s\n", path);
        return false;
    }

    std::fprintf(file, "P6\n%d %d\n255\n", HW_DISPLAY_WIDTH_PX, HW_DISPLAY_HEIGHT_PX);
    for (uint32_t y = 0; y < HW_DISPLAY_HEIGHT_PX; ++y) {
        const uint16_t* row = offscreen_framebuffer.data() +
                              (static_cast<size_t>(y) * HW_DISPLAY_WIDTH_PX);
        for (uint32_t x = 0; x < HW_DISPLAY_WIDTH_PX; ++x) {
            uint16_t raw = row[x];
#if LV_COLOR_16_SWAP
            raw = static_cast<uint16_t>((raw << 8) | (raw >> 8));
#endif
            const uint8_t rgb[3] = {
                static_cast<uint8_t>(((raw >> 11) & 0x1F) * 255 / 31),
                static_cast<uint8_t>(((raw >> 5) & 0x3F) * 255 / 63),
                static_cast<uint8_t>((raw & 0x1F) * 255 / 31),
            };
            std::fwrite(rgb, 1, sizeof(rgb), file);
        }
    }

    std::fclose(file);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const char* scene = argc > 1 ? argv[1] : "ready";
    const char* screenshot_path = nullptr;
    bool click_test = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot_path = argv[++i];
        } else if (std::strcmp(argv[i], "--click-test") == 0) {
            click_test = true;
        } else if (argv[i][0] != '-') {
            scene = argv[i];
        }
    }

    lv_init();
    if (screenshot_path || click_test) {
        if (!create_offscreen_display()) {
            return 1;
        }
    } else if (!create_interactive_display()) {
        return 1;
    }
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(THEME_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);

    seed_beans();
    ready_screen.create();
    bean_list_screen.create();
    feedback_screen.create();
    create_preview_grind_button();
    refresh_ready();
    install_handlers();
    show_scene(scene);

    if (click_test) {
        return run_click_test() ? 0 : 1;
    }

    if (screenshot_path) {
        return write_offscreen_ppm(screenshot_path) ? 0 : 1;
    }

    while (true) {
        lv_timer_handler();
        delay(5);
    }

    return 0;
}
