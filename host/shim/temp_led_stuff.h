// The desktop's stand-in for code/src/temp_led_stuff.h (preset_test): the
// LEDs drawn nowhere.
#pragma once
#include <cstdint>

namespace chompi
{
inline void SetPthLed(int, uint8_t, uint8_t, uint8_t) {}
inline void SetSmtLed(int, uint8_t, uint8_t, uint8_t) {}
inline void SetPthLedFloat(int, float, float, float) {}
inline void SetSmtLedFloat(int, float, float, float) {}
inline void fill_led_data() {}
} // namespace chompi
