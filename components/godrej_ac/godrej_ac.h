#pragma once

#include <cmath>
#include "esphome/core/hal.h"  // millis()
#include "esphome/components/climate_ir/climate_ir.h"
#include "esphome/components/select/select.h"

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
//
// This build also DECODES received frames (so using the physical remote keeps
// Home Assistant in sync) and can act as a general-purpose IR repeater.
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

// Raw timings (microseconds).
static const uint32_t GODREJ_HDR_MARK = 9105;
static const uint32_t GODREJ_HDR_SPACE = 4553;
static const uint32_t GODREJ_BIT_MARK = 632;
static const uint32_t GODREJ_ONE_SPACE = 1711;
static const uint32_t GODREJ_ZERO_SPACE = 579;
static const uint32_t GODREJ_GAP_SPACE = 20237;
static const uint32_t GODREJ_FOOTER_SPACE = 25316;

class GodrejAC : public climate_ir::ClimateIR {
 public:
  GodrejAC()
      : climate_ir::ClimateIR(GODREJ_TEMP_MIN, GODREJ_TEMP_MAX, 1.0f, false, false,
                              {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW,
                               climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH},
                              {}) {}

  // Called by the swing select entity; stores the angle and resends current
  // state. A no-op resend (same index) is skipped — this is what stops the
  // RX -> select -> set_swing_index path from re-transmitting received frames.
  void set_swing_index(int index) {
    if (index < 0 || index >= GODREJ_SWING_COUNT)
      return;
    if (index == this->swing_index_)
      return;
    this->swing_index_ = index;
    this->transmit_state();
  }
  int get_swing_index() const { return this->swing_index_; }

  // Lets received frames update the HA swing select entity too. Optional.
  void set_swing_select(select::Select *sel) { this->swing_select_ = sel; }

  // Repeater mode: re-blast every received IR frame back out the LED.
  void set_repeater(bool enabled) { this->repeater_ = enabled; }
  bool get_repeater() const { return this->repeater_; }

 protected:
  int swing_index_{1};  // default: pattern[1] = all-zero (fixed/off)
  bool repeater_{false};
  uint32_t suppress_until_{0};  // ignore RX until this millis() (our own echo)
  select::Select *swing_select_{nullptr};

  // Block RX for a moment so our own transmissions don't get decoded/repeated.
  void arm_suppression_() { this->suppress_until_ = millis() + 400; }

  void transmit_state() override {
    this->arm_suppression_();

    uint8_t b[67];
    for (int i = 0; i < 67; i++)
      b[i] = GODREJ_TEMPLATE[i];

    // power
    uint8_t power = (this->mode != climate::CLIMATE_MODE_OFF) ? 1 : 0;
    b[3] = power;
    b[22] = power;

    // temperature (degC - 16) into bits 8..11, LSB-first
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
    data->mark(GODREJ_HDR_MARK);
    data->space(GODREJ_HDR_SPACE);
    for (int i = 0; i < 35; i++) {  // block0
      data->mark(GODREJ_BIT_MARK);
      data->space(b[i] ? GODREJ_ONE_SPACE : GODREJ_ZERO_SPACE);
    }
    data->mark(GODREJ_BIT_MARK);
    data->space(GODREJ_GAP_SPACE);  // inter-block gap
    for (int i = 35; i < 67; i++) {  // block1
      data->mark(GODREJ_BIT_MARK);
      data->space(b[i] ? GODREJ_ONE_SPACE : GODREJ_ZERO_SPACE);
    }
    data->mark(GODREJ_BIT_MARK);
    data->space(GODREJ_FOOTER_SPACE);  // footer
    transmit.perform();
  }

  // ---- RX: decode incoming frames + optional repeat ----------------------
  bool on_receive(remote_base::RemoteReceiveData data) override {
    // Ignore anything that arrives right after we transmitted: that is our own
    // signal bouncing back, and re-handling it would create a feedback loop.
    if (millis() < this->suppress_until_)
      return false;

    bool handled = this->decode_and_apply_(data);

    if (this->repeater_) {
      data.reset();  // rewind to the start of the captured timings
      this->repeat_raw_(data);
    }
    return handled;
  }

  // Parse a Godrej frame straight from the raw timings and push it to HA.
  // Returns false (without touching state) for anything that isn't a valid
  // Godrej frame, so unrelated remotes don't corrupt the climate state.
  bool decode_and_apply_(remote_base::RemoteReceiveData &data) {
    uint8_t b[67];

    if (!data.expect_item(GODREJ_HDR_MARK, GODREJ_HDR_SPACE))
      return false;

    for (int i = 0; i < 35; i++) {  // block0
      if (!data.expect_mark(GODREJ_BIT_MARK))
        return false;
      if (data.expect_space(GODREJ_ONE_SPACE))
        b[i] = 1;
      else if (data.expect_space(GODREJ_ZERO_SPACE))
        b[i] = 0;
      else
        return false;
    }

    if (!data.expect_mark(GODREJ_BIT_MARK))
      return false;
    if (!data.expect_space(GODREJ_GAP_SPACE))
      return false;

    for (int i = 35; i < 67; i++) {  // block1
      if (!data.expect_mark(GODREJ_BIT_MARK))
        return false;
      if (data.expect_space(GODREJ_ONE_SPACE))
        b[i] = 1;
      else if (data.expect_space(GODREJ_ZERO_SPACE))
        b[i] = 0;
      else
        return false;
    }
    // (footer mark/space intentionally not required: the receiver's idle
    //  window clips the long 25ms footer space, which is fine for decoding.)

    // Validate the checksum before trusting any field.
    int n0 = b[0] + 2 * b[1] + 4 * b[2] + 8 * b[3];
    int tn = b[8] + 2 * b[9] + 4 * b[10] + 8 * b[11];
    int want = (n0 + tn + 12) & 0xF;
    int got = b[63] + 2 * b[64] + 4 * b[65] + 8 * b[66];
    if (want != got)
      return false;

    // power
    bool on = (b[3] != 0) && (b[22] != 0);
    // temperature
    float temp = (float) (tn + 16);
    if (temp < GODREJ_TEMP_MIN)
      temp = GODREJ_TEMP_MIN;
    if (temp > GODREJ_TEMP_MAX)
      temp = GODREJ_TEMP_MAX;
    // fan
    int fan = b[4] + 2 * b[5];
    // swing: match the 5-bit pattern against the known table
    int sw_idx = -1;
    for (int i = 0; i < GODREJ_SWING_COUNT; i++) {
      if (GODREJ_SWING[i][0] == b[6] && GODREJ_SWING[i][1] == b[35] &&
          GODREJ_SWING[i][2] == b[36] && GODREJ_SWING[i][3] == b[37] &&
          GODREJ_SWING[i][4] == b[38]) {
        sw_idx = i;
        break;
      }
    }

    // Apply to climate state. We only reverse-engineered the cool frame, so a
    // powered-on frame is reported as COOL.
    this->mode = on ? climate::CLIMATE_MODE_COOL : climate::CLIMATE_MODE_OFF;
    this->target_temperature = temp;
    switch (fan) {
      case 3:
        this->fan_mode = climate::CLIMATE_FAN_LOW;
        break;
      case 0:
        this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
        break;
      case 1:
        this->fan_mode = climate::CLIMATE_FAN_HIGH;
        break;
      default:
        this->fan_mode = climate::CLIMATE_FAN_AUTO;
        break;
    }

    if (sw_idx >= 0) {
      this->swing_index_ = sw_idx;  // set first, so the select callback no-ops
      if (this->swing_select_ != nullptr) {
        auto call = this->swing_select_->make_call();
        call.set_index((size_t) sw_idx);
        call.perform();
      }
    }

    this->publish_state();
    return true;
  }

  // Re-emit the exact captured timings (general-purpose IR repeater).
  void repeat_raw_(remote_base::RemoteReceiveData &data) {
    uint32_t n = data.size();
    if (n == 0)
      return;
    this->arm_suppression_();
    auto transmit = this->transmitter_->transmit();
    auto *out = transmit.get_data();
    out->set_carrier_frequency(38000);
    for (uint32_t i = 0; i < n; i++) {
      int32_t v = data.peek(i);
      if (v >= 0)
        out->mark((uint32_t) v);
      else
        out->space((uint32_t) (-v));
    }
    transmit.perform();
  }
};

}  // namespace godrej_ac
}  // namespace esphome
