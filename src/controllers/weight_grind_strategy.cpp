#include "weight_grind_strategy.h"

#include "grind_controller.h"
#include "../hardware/WeightSensor.h"
#include "../hardware/grinder.h"
#include "../logging/grind_logging.h"
#include "../config/system_config.h"
#include "../config/constants.h"
#include "../config/user_config.h"
#include <Arduino.h>

namespace {

float get_effective_flow_rate(const GrindController& controller) {
    float flow_rate = controller.pulse_flow_rate;

    if (flow_rate < HW_FLOW_RATE_MIN_SANE_GPS) {
        flow_rate = HW_FLOW_RATE_REFERENCE_GPS;
    } else if (flow_rate > HW_FLOW_RATE_MAX_SANE_GPS) {
        flow_rate = HW_FLOW_RATE_MAX_SANE_GPS;
    }

    return flow_rate;
}

float calculate_pulse_duration_ms(const GrindController& controller, float error_grams) {
    float effective_flow_rate = get_effective_flow_rate(controller);
    float error_factor = (error_grams < 0.05f) ? SYS_GRIND_SMALL_ERROR_FACTOR : 1.0f;
    float base_duration = (error_grams * error_factor / effective_flow_rate) * 1000.0f;
    float final_duration = max(HW_MOTOR_MIN_PULSE_DURATION_MS, min(base_duration, HW_MOTOR_MAX_PULSE_DURATION_MS));
    return final_duration;
}

bool predictive_control(GrindController& controller, const GrindLoopData& loop_data) {
    if (!controller.flow_start_confirmed) {
        const uint32_t flow_detection_window_ms = 500;
        float current_flow_rate = controller.weight_sensor->get_flow_rate(flow_detection_window_ms);

        if (current_flow_rate >= USER_GRIND_FLOW_DETECTION_THRESHOLD_GPS) {
            controller.grind_latency_ms = loop_data.now - controller.phase_start_time;
            controller.flow_start_confirmed = true;
            BLE_LOG("[PREDICTIVE] Flow start CONFIRMED! Latency: %lums, Flow: %.2fg/s\n", controller.grind_latency_ms, current_flow_rate);
        }
    }

    if (controller.flow_start_confirmed) {
        const uint32_t flow_rate_calc_window_ms = 1500;

        if (loop_data.now > (controller.phase_start_time + controller.grind_latency_ms + flow_rate_calc_window_ms)) {
            float current_flow_rate = controller.weight_sensor->get_flow_rate(flow_rate_calc_window_ms);

            if (current_flow_rate > USER_GRIND_FLOW_DETECTION_THRESHOLD_GPS) {
                controller.motor_stop_target_weight = ((controller.grind_latency_ms * USER_LATENCY_TO_COAST_RATIO) /
                                                     (float)SYS_MS_PER_SECOND) * current_flow_rate;
            }
        }
    }

    if (loop_data.current_weight >= (controller.target_weight - controller.motor_stop_target_weight)) {
        controller.grinder->stop();
        controller.predictive_end_weight = loop_data.current_weight;
        controller.pulse_flow_rate = controller.weight_sensor->get_flow_rate_95th_percentile(2500);
        return true;
    }

    return false;
}

void pulse_control(GrindController& controller, const GrindLoopData& loop_data) {
    float settled_weight;
    if (!controller.weight_sensor->check_settling_complete(HW_SCALE_PRECISION_SETTLING_TIME_MS, &settled_weight)) {
        return;
    }

    float current_weight = settled_weight;
    float conservative_target = controller.target_weight - USER_GRIND_ACCURACY_TOLERANCE_G;
    float error = conservative_target - current_weight;

    if (controller.coast_time_ms == 0) {
        controller.coast_time_ms = SYS_TIMEOUT_MEDIUM_MS;
    }

    if (controller.target_weight - current_weight < USER_GRIND_ACCURACY_TOLERANCE_G ||
        controller.pulse_attempts >= USER_GRIND_MAX_PULSE_ATTEMPTS) {
        controller.switch_phase(GrindPhase::FINAL_SETTLING, loop_data);
        return;
    }

    controller.pulse_history[controller.pulse_attempts].start_weight = current_weight;
    controller.pulse_history[controller.pulse_attempts].end_weight = current_weight;
    controller.pulse_history[controller.pulse_attempts].settle_time_ms = controller.coast_time_ms;
    controller.current_pulse_duration_ms = calculate_pulse_duration_ms(controller, error);
    controller.pulse_history[controller.pulse_attempts].duration_ms = controller.current_pulse_duration_ms;

    controller.switch_phase(GrindPhase::PULSE_EXECUTE, loop_data);
    controller.grinder->start_pulse_rmt(static_cast<uint32_t>(controller.current_pulse_duration_ms));

#if HW_LOADCELL_USE_MOCK_DRIVER
    if (controller.weight_sensor && controller.weight_sensor->get_adc_driver()) {
        controller.weight_sensor->get_adc_driver()->simulate_pulse(static_cast<uint32_t>(controller.current_pulse_duration_ms));
    }
#endif

    controller.pulse_attempts++;
}

} // namespace

void WeightGrindStrategy::on_enter(const GrindSessionDescriptor&, GrindStrategyContext&, const GrindLoopData&) {
    // No additional setup required yet; controller handles initialization
}

bool WeightGrindStrategy::update(const GrindSessionDescriptor&,
                                 GrindStrategyContext& context,
                                 const GrindLoopData& loop_data) {
    auto* controller = context.controller;
    if (!controller) {
        return false;
    }

    switch (controller->phase) {
        case GrindPhase::PREDICTIVE:
            if (predictive_control(*controller, loop_data)) {
                controller->switch_phase(GrindPhase::PULSE_SETTLING, loop_data);
            }
            return true;
        case GrindPhase::PULSE_DECISION:
            pulse_control(*controller, loop_data);
            return true;
        case GrindPhase::PULSE_EXECUTE:
            if (controller->grinder && controller->grinder->is_pulse_complete()) {
                controller->switch_phase(GrindPhase::PULSE_SETTLING, loop_data);
            }
            return true;
        case GrindPhase::PULSE_SETTLING:
            if (loop_data.now - controller->phase_start_time >= controller->grind_latency_ms + HW_MOTOR_SETTLING_TIME_MS) {
                if (controller->weight_sensor &&
                    controller->weight_sensor->check_settling_complete(HW_MOTOR_SETTLING_TIME_MS)) {
                    controller->switch_phase(GrindPhase::PULSE_DECISION, loop_data);
                }
            }
            return true;
        default:
            return false;
    }
}

void WeightGrindStrategy::on_exit(const GrindSessionDescriptor&, GrindStrategyContext&) {
    // No teardown required for weight strategy yet
}

int WeightGrindStrategy::progress_percent(const GrindSessionDescriptor& session,
                                          const GrindController& controller) const {
    (void)session;
    (void)controller;
    return 0;
}
