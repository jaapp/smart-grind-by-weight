#pragma once
#include <lvgl.h>
#include <array>
#include <functional>

// Forward declaration
class UIManager;

class EventBridgeLVGL {
public:
    // Event type enumeration for dispatch
    enum class EventType {
        TAB_CHANGE,
        PROFILE_LONG_PRESS,
        GRIND_BUTTON,
        PULSE_BUTTON,
        EDIT_SAVE,
        EDIT_CANCEL,
        EDIT_PLUS,
        EDIT_MINUS,
        SETTINGS_CALIBRATE,
        SETTINGS_RESET,
        SETTINGS_PURGE,
        SETTINGS_MOTOR_TEST,
        SETTINGS_TARE,
        SETTINGS_AUTOTUNE,
        SETTINGS_DIAGNOSTIC_RESET,
        SETTINGS_BACK,
        SETTINGS_REFRESH_STATS,
        BLE_TOGGLE,
        BLE_STARTUP_TOGGLE,
        LOGGING_TOGGLE,
        GRIND_MODE_SWIPE_TOGGLE,
        GRIND_MODE_RADIO_BUTTON,
        GRIND_MODE_AUTO_START_TOGGLE,
        GRIND_MODE_AUTO_RETURN_TOGGLE,
        BRIGHTNESS_NORMAL_SLIDER,
        BRIGHTNESS_NORMAL_SLIDER_RELEASED,
        BRIGHTNESS_SCREENSAVER_SLIDER,
        BRIGHTNESS_SCREENSAVER_SLIDER_RELEASED,
        CAL_OK,
        CAL_CANCEL,
        CAL_PLUS,
        CAL_MINUS,
        CONFIRM,
        CONFIRM_CANCEL,
        COUNT
    };

    using EventHandler = std::function<void(lv_event_t*)>;

    static void set_ui_manager(UIManager* mgr);

    // Single unified event handler
    static void dispatch_event(lv_event_t* e);

    // Special handler for profile long press (needed for ready_screen compatibility)
    static void profile_long_press_handler(lv_event_t* e);

    // Public handle_event for direct access
    static void handle_event(EventType event_type, lv_event_t* e);

    // Controller registration hook
    static void register_handler(EventType event_type, EventHandler handler);

private:
    static UIManager* ui_manager;
    static std::array<EventHandler, static_cast<size_t>(EventType::COUNT)> custom_handlers;
};
