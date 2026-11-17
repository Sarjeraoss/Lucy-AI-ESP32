#pragma once
#include <stdint.h>
void speakerBegin(uint8_t volume);
void speakerVolume(uint8_t volume);
void speakerGreeting();
void speakerCat(uint8_t behaviour, uint8_t variant);
void speakerTick();
bool speakerReady();
bool speakerBusy();
