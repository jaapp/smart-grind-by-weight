#pragma once
#include <lvgl.h>

class UIManager;
enum class UIState;
struct GrindEventData;

// Controls grind/pulse buttons, state transitions, chart updates, and auto-return timers

class GrindingUIController {
public:
    explicit GrindingUIController(UIManager* manager);

    void build_controls();
    void register_events();

    void on_state_changed(UIState new_state);
    void update(UIState current_state);

    void handle_grind_button();
    void handle_pulse_button();
    void handle_layout_toggle();
    void handle_purge_confirm_continue();

    void update_grind_button_icon();
    void update_button_layout();
    void update_grinding_targets();
    void reset_grind_complete_timer();

    void handle_grind_event(const GrindEventData& event_data);
    static void dispatch_event(const GrindEventData& event_data);

private:
    void enter_ready_state();
    void enter_edit_state();
    void enter_grinding_state();
    void enter_grind_complete_state();
    void enter_grind_timeout_state();
    void enter_menu_state();

    void start_grind_complete_timer();
    void start_grind_timeout_timer();
    void cancel_timers();

    static void grind_complete_timer_cb(lv_timer_t* timer);
    static void grind_timeout_timer_cb(lv_timer_t* timer);

    static GrindingUIController* instance_;

    UIManager* ui_manager_;
    lv_obj_t* grind_button_ = nullptr;
    lv_obj_t* grind_icon_ = nullptr;
    lv_obj_t* pulse_button_ = nullptr;
    lv_obj_t* pulse_icon_ = nullptr;
    lv_timer_t* grind_complete_timer_ = nullptr;
    lv_timer_t* grind_timeout_timer_ = nullptr;
    bool chart_updates_enabled_ = false;
    float final_grind_weight_ = 0.0f;
    int final_grind_progress_ = 0;
    float error_grind_weight_ = 0.0f;
    int error_grind_progress_ = 0;
    char error_message_[32] = {0};
};
