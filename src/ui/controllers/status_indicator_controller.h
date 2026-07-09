#pragma once
#include <lvgl.h>

class UIManager;

// Global top navigation bar across every non-immersive screen. Left side: a back
// arrow (contextual "back", dispatches MENUBAR_BACK) and a screen title. Right side:
// status icons — Bluetooth connection (color-coded) and a diagnostic warning icon.
// Lives on lv_scr_act(); shown/hidden and titled per UI state by UIManager.
class StatusIndicatorController {
public:
    explicit StatusIndicatorController(UIManager* manager);

    void build();
    void update();

    // Set the contextual screen title shown next to the back arrow ("" to clear).
    void set_title(const char* title);
    // Show/hide the whole bar (hidden on immersive states: BOOT, GRINDING, ...).
    void set_visible(bool visible);
    // Show/hide the back arrow (hidden on READY and forced-completion screens).
    void set_back_visible(bool visible);
    // Keep the bar on top after a screen calls lv_obj_move_foreground().
    void bring_to_front();

private:
    void update_ble_status_icon();
    void update_warning_icon();
    void reflow_title();  // re-anchor the title to the back arrow / left edge

    UIManager* ui_manager_;
    lv_obj_t* bar_ = nullptr;
    lv_obj_t* back_button_ = nullptr;
    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* ble_status_icon_ = nullptr;
    lv_obj_t* warning_icon_ = nullptr;
    bool back_visible_ = false;
};
