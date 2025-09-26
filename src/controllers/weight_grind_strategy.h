#pragma once

#include "grind_strategy.h"

class WeightGrindStrategy : public IGrindStrategy {
public:
    WeightGrindStrategy() = default;

    void on_enter(const GrindSessionDescriptor& session,
                  GrindStrategyContext& context,
                  const GrindLoopData& loop_data) override;

    bool update(const GrindSessionDescriptor& session,
                GrindStrategyContext& context,
                const GrindLoopData& loop_data) override;

    void on_exit(const GrindSessionDescriptor& session,
                 GrindStrategyContext& context) override;

    int progress_percent(const GrindSessionDescriptor& session,
                         const GrindController& controller) const override;

    const char* name() const override { return "Weight"; }

private:
    float get_effective_flow_rate(const GrindController& controller) const;
    float calculate_pulse_duration_ms(const GrindController& controller, float error_grams) const;
    void run_predictive_phase(GrindController& controller, const GrindLoopData& loop_data) const;
    void run_pulse_decision_phase(GrindController& controller, const GrindLoopData& loop_data) const;
    void run_pulse_execute_phase(GrindController& controller, const GrindLoopData& loop_data) const;
    void run_pulse_settling_phase(GrindController& controller, const GrindLoopData& loop_data) const;
};
