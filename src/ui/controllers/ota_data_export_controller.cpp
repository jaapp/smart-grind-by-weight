#include "ota_data_export_controller.h"

#include "arduino_compat.h"
#include <cstring>

#include "../ui_manager.h"

OtaDataExportController::OtaDataExportController(UIManager* manager)
    : ui_manager_(manager), data_export_active_(false) {
    expected_build_[0] = '\0';
}

void OtaDataExportController::register_events() {
    if (!ui_manager_) {
        return;
    }

    if (auto* ok_btn = ui_manager_->ota_update_failed_screen.get_ok_button()) {
        lv_obj_add_event_cb(ok_btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
                return;
            }
            if (auto* controller = static_cast<OtaDataExportController*>(lv_event_get_user_data(e))) {
                controller->handle_failure_acknowledged();
            }
        }, LV_EVENT_CLICKED, this);
    }
}

bool OtaDataExportController::update() {
    if (!ui_manager_ || !ui_manager_->bluetooth_manager) {
        return false;
    }

    auto* bluetooth = ui_manager_->bluetooth_manager;

    if (bluetooth->is_updating()) {
        if (!ui_manager_->state_machine->is_state(UIState::OTA_UPDATE)) {
            ui_manager_->ota_screen.show_ota_mode();
            ui_manager_->switch_to_state(UIState::OTA_UPDATE);
        } else {
            int progress = static_cast<int>(bluetooth->get_ota_progress());
            ui_manager_->ota_screen.update_progress(progress);
        }
        return true;
    }

    if (bluetooth->is_data_export_active()) {
        if (!data_export_active_) {
            start_data_export_ui();
        }
        poll_data_export();
    } else if (data_export_active_) {
        stop_data_export_ui();
    }

    return false;
}

void OtaDataExportController::update_progress(int percent) {
    if (!ui_manager_ || !ui_manager_->state_machine) {
        return;
    }

    if (ui_manager_->state_machine->is_state(UIState::OTA_UPDATE)) {
        ui_manager_->ota_screen.update_progress(percent);
    }
}

void OtaDataExportController::update_status(const char* status) {
    if (!ui_manager_ || !ui_manager_->state_machine || !status) {
        return;
    }

    if (ui_manager_->state_machine->is_state(UIState::OTA_UPDATE)) {
        ui_manager_->ota_screen.update_status(status);
    }
}

void OtaDataExportController::show_failure_warning(const char* expected_build) {
    set_failure_info(expected_build);
    if (!ui_manager_) {
        return;
    }

    ui_manager_->switch_to_state(UIState::OTA_UPDATE_FAILED);
}

void OtaDataExportController::set_failure_info(const char* expected_build) {
    if (!expected_build) {
        clear_failure_info();
        return;
    }

    std::strncpy(expected_build_, expected_build, sizeof(expected_build_) - 1);
    expected_build_[sizeof(expected_build_) - 1] = '\0';
}

void OtaDataExportController::show_failure_screen() {
    if (!ui_manager_) {
        return;
    }

    ui_manager_->ota_update_failed_screen.show(expected_build_);
}

void OtaDataExportController::handle_failure_acknowledged() {
    if (!ui_manager_) {
        return;
    }

    ui_manager_->ota_update_failed_screen.hide();
    clear_failure_info();
    ui_manager_->switch_to_state(UIState::READY);
}

void OtaDataExportController::start_data_export_ui() {
    if (!ui_manager_ || !ui_manager_->bluetooth_manager) {
        return;
    }

    if (ui_manager_->bluetooth_manager->is_data_export_active()) {
        data_export_active_ = true;
        ui_manager_->ota_screen.show_data_export_mode();
        ui_manager_->switch_to_state(UIState::OTA_UPDATE);
    }
}

void OtaDataExportController::poll_data_export() {
    if (!ui_manager_ || !ui_manager_->bluetooth_manager) {
        return;
    }

    auto* bluetooth = ui_manager_->bluetooth_manager;

    if (!bluetooth->is_data_export_active()) {
        stop_data_export_ui();
        return;
    }

    float progress = bluetooth->get_data_export_progress();
    int percent = static_cast<int>(progress);

    ui_manager_->ota_screen.update_progress(percent);
    ui_manager_->ota_screen.update_status("Sending data....");
}

void OtaDataExportController::stop_data_export_ui() {
    if (!ui_manager_) {
        return;
    }

    if (!data_export_active_) {
        return;
    }

    data_export_active_ = false;

    if (auto* bluetooth = ui_manager_->bluetooth_manager) {
        Serial.printf("UI: Data export ended - progress was at %d%%\n",
                      static_cast<int>(bluetooth->get_data_export_progress()));
        bluetooth->stop_data_export();
    }

    ui_manager_->ota_screen.hide();
    ui_manager_->switch_to_state(UIState::READY);
}

void OtaDataExportController::clear_failure_info() {
    expected_build_[0] = '\0';
}
