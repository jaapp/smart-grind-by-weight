#include "hardware_manager.h"
#include "../controllers/grind_controller.h"
#include <Arduino.h>
#include "../config/constants.h"
#include "../config/pins.h"

void HardwareManager::init() {
    preferences.begin("grinder", false);
    display_manager.init();
    weight_sensor.init(&preferences);
    grinder.init(HW_MOTOR_RELAY_PIN);
    
    grind_controller = nullptr; // Will be set later
    initialized = true;
}

void HardwareManager::update() {
    if (!initialized) return;
    
    // All hardware components now updated independently by TaskScheduler:
    // - weight_sensor.update() in "weight_sensor" task (10ms)  
    // - display_manager.update() in "ui_display" task (16ms)
    // No need for grinding mode switching - load cell runs at constant high speed
}


