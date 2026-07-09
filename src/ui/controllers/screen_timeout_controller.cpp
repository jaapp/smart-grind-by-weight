#include "screen_timeout_controller.h"

#include "../../config/constants.h"
#include "../../hardware/display_manager.h"
#include "../../hardware/hardware_manager.h"
#include "../assets/boot_logo.h"
#include "../ui_manager.h"

ScreenTimeoutController::ScreenTimeoutController(UIManager* manager)
    : ui_manager_(manager) {}

void ScreenTimeoutController::register_events() {}

void ScreenTimeoutController::refresh_settings() {
    if (ui_manager_ && ui_manager_->menu_controller_) {
        dim_timeout_ms_ = ui_manager_->menu_controller_->get_screensaver_timeout_ms();
        off_timeout_ms_ = ui_manager_->menu_controller_->get_screensaver_off_timeout_ms();
        mode_ = ui_manager_->menu_controller_->get_screensaver_mode();
    } else {
        dim_timeout_ms_ = USER_SCREEN_DIM_TIMEOUT_MS;
        off_timeout_ms_ = USER_SCREEN_OFF_TIMEOUT_MS;
        mode_ = USER_SCREEN_SAVER_MODE_DEFAULT;
    }
    settings_loaded_ = true;
}

void ScreenTimeoutController::update() {
    if (!ui_manager_) {
        return;
    }

    // Load the cached settings once; thereafter update() stays off NVS. The menu handlers call
    // refresh_settings() when the user changes the timeouts or mode.
    if (!settings_loaded_) {
        refresh_settings();
    }

    auto* hardware = ui_manager_->hardware_manager;
    if (!hardware) {
        return;
    }

    auto* display = hardware->get_display();
    if (!display) {
        return;
    }

    auto* touch_driver = display->get_touch_driver();
    if (!touch_driver) {
        return;
    }

    // Never engage during a grind; restore immediately if we were engaged.
    if (ui_manager_->state_machine && ui_manager_->state_machine->is_state(UIState::GRINDING)) {
        if (stage_ != Stage::ACTIVE) {
            apply_stage(Stage::ACTIVE, display);
        }
        return;
    }

    uint32_t ms_since_touch = touch_driver->get_ms_since_last_touch();
    auto* sensor = hardware->get_weight_sensor();

    bool dim_enabled = (dim_timeout_ms_ != USER_SCREEN_SAVER_TIMEOUT_NEVER_MS);
    bool off_enabled = (off_timeout_ms_ != USER_SCREEN_SAVER_TIMEOUT_NEVER_MS);

    // Weight movement within a stage's window counts as activity for that stage.
    auto weight_active_within = [&](uint32_t window_ms) {
        return sensor && sensor->weight_range_exceeds(window_ms, USER_WEIGHT_ACTIVITY_THRESHOLD_G);
    };

    Stage desired = Stage::ACTIVE;
    if (off_enabled && ms_since_touch >= off_timeout_ms_ && !weight_active_within(off_timeout_ms_)) {
        desired = Stage::OFF;
    } else if (dim_enabled && ms_since_touch >= dim_timeout_ms_ && !weight_active_within(dim_timeout_ms_)) {
        desired = Stage::DIMMED;
    }

    if (desired != stage_) {
        apply_stage(desired, display);
    }
}

void ScreenTimeoutController::apply_stage(Stage stage, DisplayManager* display) {
    switch (stage) {
        case Stage::ACTIVE:
            hide_logo_overlay();
            display->set_brightness(get_normal_brightness());
            break;

        case Stage::DIMMED:
            if (mode_ == USER_SCREEN_SAVER_MODE_LOGO) {
                show_logo_overlay();
            } else {
                hide_logo_overlay();
            }
            display->set_brightness(get_screensaver_brightness());
            break;

        case Stage::OFF:
            // Backlight fully off; free the overlay (nothing is visible anyway).
            hide_logo_overlay();
            display->set_brightness(0.0f);
            break;
    }
    stage_ = stage;
}

// Screensaver engage animation timing: fade the screen to dark, then slowly
// bring the logo up once it's dark.
static constexpr uint32_t kSaverFadeToDarkMs = 500;
static constexpr uint32_t kSaverLogoFadeInMs = 1200;

static void saver_bg_opa_anim_cb(void* var, int32_t value) {
    lv_obj_set_style_bg_opa(static_cast<lv_obj_t*>(var), static_cast<lv_opa_t>(value), LV_PART_MAIN);
}

static void saver_logo_opa_anim_cb(void* var, int32_t value) {
    lv_obj_set_style_opa(static_cast<lv_obj_t*>(var), static_cast<lv_opa_t>(value), LV_PART_MAIN);
}

void ScreenTimeoutController::show_logo_overlay() {
    if (saver_screen_) {
        return;
    }

    // Black cover with the boot logo centered — same look as the splash. Starts
    // transparent and fades to dark; the logo then fades in slowly on top. Not
    // clickable, so the wake tap passes through to the UI beneath (matching the
    // plain-dim behaviour).
    saver_screen_ = lv_obj_create(lv_scr_act());
    lv_obj_set_size(saver_screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(saver_screen_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(saver_screen_, lv_color_hex(THEME_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(saver_screen_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(saver_screen_, 0, 0);
    lv_obj_set_style_pad_all(saver_screen_, 0, 0);
    lv_obj_clear_flag(saver_screen_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(saver_screen_, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* logo = lv_image_create(saver_screen_);
    lv_image_set_src(logo, &boot_logo);
    lv_obj_center(logo);
    lv_obj_set_style_opa(logo, LV_OPA_TRANSP, LV_PART_MAIN);

    // Above every screen and the nav bar.
    lv_obj_move_foreground(saver_screen_);

    // Stage the fades: screen -> dark, then logo in. Deleting the overlay on wake
    // also removes any running animations on it and its children.
    lv_anim_t fade_dark;
    lv_anim_init(&fade_dark);
    lv_anim_set_var(&fade_dark, saver_screen_);
    lv_anim_set_exec_cb(&fade_dark, saver_bg_opa_anim_cb);
    lv_anim_set_values(&fade_dark, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&fade_dark, kSaverFadeToDarkMs);
    lv_anim_start(&fade_dark);

    lv_anim_t fade_logo;
    lv_anim_init(&fade_logo);
    lv_anim_set_var(&fade_logo, logo);
    lv_anim_set_exec_cb(&fade_logo, saver_logo_opa_anim_cb);
    lv_anim_set_values(&fade_logo, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&fade_logo, kSaverLogoFadeInMs);
    lv_anim_set_delay(&fade_logo, kSaverFadeToDarkMs);
    lv_anim_start(&fade_logo);
}

void ScreenTimeoutController::hide_logo_overlay() {
    if (!saver_screen_) {
        return;
    }
    lv_obj_delete(saver_screen_);  // deletes the child logo image too
    saver_screen_ = nullptr;
}

float ScreenTimeoutController::get_normal_brightness() const {
    if (ui_manager_ && ui_manager_->menu_controller_) {
        return ui_manager_->menu_controller_->get_normal_brightness();
    }
    return USER_SCREEN_BRIGHTNESS_NORMAL;
}

float ScreenTimeoutController::get_screensaver_brightness() const {
    if (ui_manager_ && ui_manager_->menu_controller_) {
        return ui_manager_->menu_controller_->get_screensaver_brightness();
    }
    return USER_SCREEN_BRIGHTNESS_DIMMED;
}
