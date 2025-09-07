#pragma once

#include "esphome/core/component.h"
#include "esphome/components/output/float_output.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <bitset>
#include <set>

#ifdef USE_ESP32
#include "driver/ledc.h"
#include "driver/mcpwm.h"
#include "soc/ledc_periph.h"
#include "soc/mcpwm_periph.h"
#endif

namespace esphome {
namespace mcpwm_unified {

extern const char *const TAG;

enum class DriverType {
  AUTO,
  LEDC,
  MCPWM
};

enum class AllocatedDriver {
  NONE,
  LEDC,
  MCPWM
};

class McpwmUnifiedOutput : public output::FloatOutput, public Component {
 public:
  void set_pin(InternalGPIOPin *pin) { this->pin_ = pin; }
  void set_frequency(float frequency) { this->frequency_ = frequency; }
  void set_driver(const std::string &driver);
  void set_channel(uint8_t channel) { this->preferred_channel_ = channel; }
  void set_mcpwm_unit(uint8_t unit) { this->mcpwm_unit_ = unit; }
  void set_mcpwm_timer(uint8_t timer) { this->mcpwm_timer_ = timer; }
  void set_mcpwm_operator(uint8_t op) { this->mcpwm_operator_ = op; }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

 protected:
  void write_state(float state) override;

 private:
  InternalGPIOPin *pin_{nullptr};
  float frequency_{50000.0f};
  DriverType driver_type_{DriverType::AUTO};
  AllocatedDriver allocated_driver_{AllocatedDriver::NONE};
  
  // Preferred configuration
  optional<uint8_t> preferred_channel_;
  uint8_t mcpwm_unit_{0};
  uint8_t mcpwm_timer_{0};
  uint8_t mcpwm_operator_{0}; // 0 = A, 1 = B
  
  // Allocated resources
  uint8_t allocated_channel_{0};
  ledc_timer_t ledc_timer_{LEDC_TIMER_0};
  ledc_channel_t ledc_channel_{LEDC_CHANNEL_0};
  mcpwm_unit_t allocated_mcpwm_unit_{MCPWM_UNIT_0};
  mcpwm_timer_t allocated_mcpwm_timer_{MCPWM_TIMER_0};
  mcpwm_operator_t allocated_mcpwm_operator_{MCPWM_OPR_A};

  // Static resource tracking
  static std::bitset<8> ledc_channels_used_;
  static std::bitset<2> mcpwm_units_used_[3][2]; // [timer][operator]
  static std::set<uint8_t> gpio_pins_used_;
  
  bool allocate_ledc_channel();
  bool allocate_mcpwm_channel();
  bool setup_ledc();
  bool setup_mcpwm();
  void write_ledc_state(float state);
  void write_mcpwm_state(float state);
  uint32_t frequency_to_ledc_resolution(float freq);
};

}  // namespace mcpwm_unified
}  // namespace esphome