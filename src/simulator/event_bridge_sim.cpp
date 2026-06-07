#include "../ui/event_bridge_lvgl.h"

#include <utility>

UIManager* EventBridgeLVGL::ui_manager = nullptr;
std::array<EventBridgeLVGL::EventHandler, static_cast<size_t>(EventBridgeLVGL::EventType::COUNT)> EventBridgeLVGL::custom_handlers{};

void EventBridgeLVGL::set_ui_manager(UIManager* mgr) {
    ui_manager = mgr;
}

void EventBridgeLVGL::dispatch_event(lv_event_t* e) {
    EventType event_type = static_cast<EventType>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    handle_event(event_type, e);
}

void EventBridgeLVGL::profile_long_press_handler(lv_event_t* e) {
    handle_event(EventType::PROFILE_LONG_PRESS, e);
}

void EventBridgeLVGL::handle_event(EventType event_type, lv_event_t* e) {
    const size_t index = static_cast<size_t>(event_type);
    if (index < custom_handlers.size() && custom_handlers[index]) {
        custom_handlers[index](e);
    }
}

void EventBridgeLVGL::register_handler(EventType event_type, EventHandler handler) {
    custom_handlers[static_cast<size_t>(event_type)] = std::move(handler);
}
