#pragma once
#include <lvgl.h>
#include <functional>

// Enum for different types of blocking operations
enum class BlockingOperation {
    TARING,
    CALIBRATING,
    SETTLING,
    BLE_ENABLING,
    LOADING_STATISTICS,
    CUSTOM
};

// Callback type for when operation completes
using OperationCallback = std::function<void()>;

class BlockingOperationOverlay {
private:
    lv_obj_t* overlay;
    lv_obj_t* label;
    lv_timer_t* operation_timer;
    OperationCallback completion_callback;
    OperationCallback operation_callback;
    bool is_visible;
    
    static BlockingOperationOverlay* g_instance;

public:
    static BlockingOperationOverlay& getInstance() {
        if (!g_instance) {
            g_instance = new BlockingOperationOverlay();
        }
        return *g_instance;
    }
    
    void init();
    void show_and_execute(BlockingOperation op_type, 
                          OperationCallback operation_func,
                          OperationCallback completion_func = nullptr,
                          const char* custom_message = nullptr);
    void hide_and_complete();
    bool is_operation_active() const { return is_visible; }
    
    // Direct show/hide methods for non-blocking operations
    void show(const char* message);
    void hide();
    
private:
    const char* get_operation_message(BlockingOperation op_type, const char* custom_message);
    static void operation_timer_cb(lv_timer_t* timer);
};
