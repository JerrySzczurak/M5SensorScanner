#pragma once

#include <Arduino.h>

#define TEXT_INPUT_MAX_LEN 32

// Starts text input mode. `prompt` is shown as a small title above the ring
// (e.g. "Sensor name:"). Call once when entering the feature.
void textInputBegin(const char* prompt);

// Call every loop() iteration while input mode is active (calls
// M5Dial.update() internally - safe to call M5Dial.update() elsewhere too).
// Returns true exactly once, the moment the user confirms the whole string
// via the ring's "OK" item - textInputGetResult() is valid at that point.
bool textInputUpdate();

// Valid only right after textInputUpdate() returned true.
const char* textInputGetResult();

// True while input mode is running (i.e. before OK is confirmed).
bool textInputIsActive();
