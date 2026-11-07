#pragma once
#include "ported/features/pixel_screen/core.h"
namespace pixel_screen {
// Shared state artwork/palette; physical resolution and transport are separate modules.
void renderScene(const Snapshot &source,unsigned phase,uint8_t *indexes);
}
