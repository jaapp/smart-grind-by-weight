#include "event_bridge_lvgl.h"
#include "ui_manager.h"
#include "../config/constants.h"
#include <utility>

UIManager* EventBridgeLVGL::ui_manager = nullptr;
std::array<EventBridgeLVGL::EventHandler, static_cast<size_t>(EventBridgeLVGL::EventType::COUNT)> EventBridgeLVGL::custom_handlers{};

void EventBridgeLVGL::set_ui_manager(UIManager* mgr) {
    ui_manager = mgr;
}

void EventBridgeLVGL::dispatch_event(lv_event_t* e) {
    if (!ui_manager) {
        LOG_BLE("[ERROR] EventBridgeLVGL: UI manager not initialized\n");
        return;
    }
    
    // Get event type from user data
    EventType event_type = static_cast<EventType>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    handle_event(event_type, e);
}

void EventBridgeLVGL::target_long_press_handler(lv_event_t* e) {
    handle_event(EventType::TARGET_LONG_PRESS, e);
}

void EventBridgeLVGL::handle_event(EventType event_type, lv_event_t* e) {
    size_t index = static_cast<size_t>(event_type);
    if (index < custom_handlers.size()) {
        auto& handler = custom_handlers[index];
        if (handler) {
            handler(e);
            return;
        }
    }

    LOG_BLE("[WARNING] EventBridgeLVGL: No handler registered for event type: %d\n",
            static_cast<int>(event_type));
}

void EventBridgeLVGL::register_handler(EventType event_type, EventHandler handler) {
    custom_handlers[static_cast<size_t>(event_type)] = std::move(handler);
}
