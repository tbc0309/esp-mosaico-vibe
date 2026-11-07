#pragma once
#include "board_identity.h"
namespace bridge::board {
void begin();
bool bootPressed();
void statusLed(bool enabled);
}
