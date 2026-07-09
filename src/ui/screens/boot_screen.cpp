#include "boot_screen.h"
#include "../../config/constants.h"
#include "../assets/boot_logo.h"

void BootScreen::create() {
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
    lv_obj_align(screen, LV_ALIGN_CENTER, 0, 0);

    // Opaque black so the splash fully covers any boot-time render artifact
    lv_obj_set_style_bg_color(screen, lv_color_hex(THEME_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_CLICKABLE);

    logo = lv_image_create(screen);
    lv_image_set_src(logo, &boot_logo);
    lv_obj_align(logo, LV_ALIGN_CENTER, USER_BOOT_LOGO_OFFSET_X, USER_BOOT_LOGO_OFFSET_Y);
    // Start invisible; the boot sequence fades the logo in on its first tick
    lv_obj_set_style_opa(logo, LV_OPA_TRANSP, LV_PART_MAIN);

    visible = false;
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
}

void BootScreen::show() {
    if (!screen) return;
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(screen); // keep the splash above all other screens during boot
    visible = true;
}

void BootScreen::hide() {
    if (!screen) return;
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = false;
}

void BootScreen::destroy() {
    if (!screen) return;
    lv_obj_delete(screen); // deletes the child logo image too
    screen = nullptr;
    logo = nullptr;
    visible = false;
}
