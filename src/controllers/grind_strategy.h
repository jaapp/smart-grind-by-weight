#pragma once

#include "grind_session.h"

struct GrindLoopData;
class GrindController;

struct GrindStrategyContext {
    GrindController* controller = nullptr;
};

class IGrindStrategy {
public:
    virtual ~IGrindStrategy() = default;

    virtual void on_enter(const GrindSessionDescriptor& session,
                          GrindStrategyContext& context,
                          const GrindLoopData& loop_data) = 0;

    virtual bool update(const GrindSessionDescriptor& session,
                        GrindStrategyContext& context,
                        const GrindLoopData& loop_data) = 0;

    virtual void on_exit(const GrindSessionDescriptor& session,
                         GrindStrategyContext& context) = 0;

    virtual int progress_percent(const GrindSessionDescriptor& session,
                                 const GrindController& controller) const = 0;

    virtual const char* name() const = 0;
};
