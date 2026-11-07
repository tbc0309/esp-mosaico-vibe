#pragma once
#include "ported/features/pixel_screen/core.h"

// Called from the application task. Playback is owned by the audio worker.
void printSoundPoll(pixel_screen::State state);
