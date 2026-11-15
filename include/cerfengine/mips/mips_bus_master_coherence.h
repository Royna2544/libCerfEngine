#pragma once

#include "../engine.h"

namespace cerfengine {

class MipsTranslationCache;
class PhysicalMap;

class MipsBusMasterCoherence : public EngineService {
public:
    using EngineService::EngineService;
    bool ShouldRegister() override { return engine_.GetSetup().isa == Isa::kMips; }
    void OnReady() override;
    void AfterWrite(uint32_t pa, uint32_t bytes);

private:
    MipsTranslationCache* translations_ = nullptr;
    PhysicalMap* map_ = nullptr;
};

}
