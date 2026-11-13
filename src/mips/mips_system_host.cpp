#include <cerfengine/mips/mips_system_host.h>

namespace cerfengine {

MipsCp0Decision MipsSystemHost::MipsCp0(uint32_t rd, uint32_t sel) {
    return sel == 0u ? MipsCp0(rd) : MipsCp0Decision{};
}

}
