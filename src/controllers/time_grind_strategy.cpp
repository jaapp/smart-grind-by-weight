#include "time_grind_strategy.h"
#include "../config/constants.h"
#include "grind_controller.h"
#include "../logging/grind_logging.h"
#include <Arduino.h>

void TimeGrindStrategy::on_enter(const GrindSessionDescriptor&,
                                 GrindStrategyContext& context,
                                 const GrindLoopData&) {
    if (!context.controller) {
        return;
    }
    context.controller->time_grind_start_ms = 0;
}

bool TimeGrindStrategy::update(const GrindSessionDescriptor& session,
                               GrindStrategyContext& context,
                               const GrindLoopData& loop_data) {
    auto* controller = context.controller;
    if (!controller || session.mode != GrindMode::TIME) {
        return false;
    }

    switch (controller->phase) {
        case GrindPhase::TIME_GRINDING: {
            if (controller->time_grind_start_ms == 0) {
                controller->time_grind_start_ms = loop_data.now;
            }

            if (controller->target_time_ms == 0) {
                controller->grinder->stop();
                controller->switch_phase(GrindPhase::FINAL_SETTLING, loop_data);
                return true;
            }

            unsigned long elapsed = loop_data.now - controller->time_grind_start_ms;
            if (elapsed >= controller->target_time_ms) {
                controller->grinder->stop();
                controller->switch_phase(GrindPhase::FINAL_SETTLING, loop_data);
            }
            return true;
        }
        default:
            return false;
    }
}

void TimeGrindStrategy::on_exit(const GrindSessionDescriptor&, GrindStrategyContext& context) {
    if (context.controller) {
        context.controller->time_grind_start_ms = 0;
    }
}

int TimeGrindStrategy::progress_percent(const GrindSessionDescriptor& session,
                                        const GrindController& controller) const {
    if (session.mode != GrindMode::TIME || session.target_time_ms == 0) {
        return 0;
    }

    if (controller.time_grind_start_ms == 0) {
        return 0;
    }

    unsigned long elapsed = millis() - controller.time_grind_start_ms;
    if (elapsed >= session.target_time_ms) {
        return 100;
    }

    int progress = static_cast<int>((static_cast<float>(elapsed) / static_cast<float>(session.target_time_ms)) * 100.0f);
    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;
    return progress;
}
