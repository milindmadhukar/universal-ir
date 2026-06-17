#pragma once

#include <cmath>
#include "esphome/components/climate_ir/climate_ir.h"

namespace esphome {
namespace godrej_ac {

// ---------------------------------------------------------------------------
// Godrej AC — LG-extended IR protocol, fully reverse-engineered from capture.
// Frame = 67 data bits (LSB-first), split into two blocks with a long gap.
//   header(9105us/4553us) -> block0[35 bits] -> gap(632/20237) ->
//   block1[32 bits] -> footer(632/25316)
//   bit mark ~632us; space 579us = 0, 1711us = 1; carrier 38kHz.
// Field map (data-bit index, LSB-first):
//   power : bits 3 & 22         (1=on, 0=off)
//   fan   : bits 4-5            (Low=3, Med=0, High=1, Auto=2)
//   temp  : bits 8-11           (value = degC - 16)
//   swing : bits 6,35,36,37,38  (10 captured patterns)
//   cksum : bits 63-66          (= (nibble0 + temp_nibble + 12) & 0xF)
// Synthesis verified bit-for-bit against 70 captured frames.
// ---------------------------------------------------------------------------

static const float GODREJ_TEMP_MIN = 16.0f;
static const float GODREJ_TEMP_MAX = 30.0f;

// Constant baseline frame (cool, on). Variable fields are overwritten below.
static const uint8_t GODREJ_TEMPLATE[67] = {
    1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0,
    0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0};

// 10 swing patterns: {bit6, bit35, bit36, bit37, bit38}
static const uint8_t GODREJ_SWING[10][5] = {
    {1, 1, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 1, 1, 0, 0},
    {0, 0, 0, 1, 0}, {0, 1, 0, 1, 0}, {0, 0, 1, 1, 0}, {1, 1, 1, 1, 0},
    {1, 1, 0, 0, 1}, {1, 1, 1, 0, 1}};
static const int GODREJ_SWING_COUNT = 10;

class GodrejAC : public climate_ir::ClimateIR {
 public:
  GodrejAC()
      : climate_ir::ClimateIR(GODREJ_TEMP_MIN, GODREJ_TEMP_MAX, 1.0f, false, false,
                              {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW,
                               climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH},
                              {}) {}

  // Called by the swing select entity; stores the angle and resends current state.
  void set_swing_index(int index) {
    if (index < 0 || index >= GODREJ_SWING_COUNT)
      return;
    this->swing_index_ = index;
    this->transmit_state();
  }
  int get_swing_index() const { return this->swing_index_; }

 protected:
  int swing_index_{1};  // default: pattern[1] = all-zero (fixed/off)

  void transmit_state() override {
    uint8_t b[67];
    for (int i = 0; i < 67; i++)
      b[i] = GODREJ_TEMPLATE[i];

    // power
    uint8_t power = (this->mode != climate::CLIMATE_MODE_OFF) ? 1 : 0;
    b[3] = power;
    b[22] = power;

    // temperature (degC - 15) into bits 8..11, LSB-first
    int temp = (int) lroundf(this->target_temperature);
    if (temp < (int) GODREJ_TEMP_MIN)
      temp = (int) GODREJ_TEMP_MIN;
    if (temp > (int) GODREJ_TEMP_MAX)
      temp = (int) GODREJ_TEMP_MAX;
    int tf = temp - 16;
    b[8] = tf & 1;
    b[9] = (tf >> 1) & 1;
    b[10] = (tf >> 2) & 1;
    b[11] = (tf >> 3) & 1;

    // fan into bits 4..5
    int fan = 2;  // auto
    if (this->fan_mode.has_value()) {
      switch (this->fan_mode.value()) {
        case climate::CLIMATE_FAN_LOW:
          fan = 3;
          break;
        case climate::CLIMATE_FAN_MEDIUM:
          fan = 0;
          break;
        case climate::CLIMATE_FAN_HIGH:
          fan = 1;
          break;
        default:
          fan = 2;  // auto
          break;
      }
    }
    b[4] = fan & 1;
    b[5] = (fan >> 1) & 1;

    // swing pattern
    const uint8_t *sw = GODREJ_SWING[this->swing_index_];
    b[6] = sw[0];
    b[35] = sw[1];
    b[36] = sw[2];
    b[37] = sw[3];
    b[38] = sw[4];

    // checksum = (nibble0 + temp_nibble + 12) & 0xF into bits 63..66, LSB-first
    int n0 = b[0] + 2 * b[1] + 4 * b[2] + 8 * b[3];
    int tn = b[8] + 2 * b[9] + 4 * b[10] + 8 * b[11];
    int chk = (n0 + tn + 12) & 0xF;
    b[63] = chk & 1;
    b[64] = (chk >> 1) & 1;
    b[65] = (chk >> 2) & 1;
    b[66] = (chk >> 3) & 1;

    // render to raw IR timings
    auto transmit = this->transmitter_->transmit();
    auto *data = transmit.get_data();
    data->set_carrier_frequency(38000);
    data->mark(9105);
    data->space(4553);
    for (int i = 0; i < 35; i++) {  // block0
      data->mark(632);
      data->space(b[i] ? 1711 : 579);
    }
    data->mark(632);
    data->space(20237);  // inter-block gap
    for (int i = 35; i < 67; i++) {  // block1
      data->mark(632);
      data->space(b[i] ? 1711 : 579);
    }
    data->mark(632);
    data->space(25316);  // footer
    transmit.perform();
  }
};

}  // namespace godrej_ac
}  // namespace esphome
