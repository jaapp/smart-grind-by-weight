#pragma once

#include "grind_strategy.h"

class TimeGrindStrategy : public IGrindStrategy {
public:
    TimeGrindStrategy() = default;

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

    const char* name() const override { return "Time"; }
};
