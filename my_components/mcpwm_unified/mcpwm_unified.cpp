#include "mcpwm_unified.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

#ifdef USE_ESP32

namespace esphome {
namespace mcpwm_unified {

static const char *const TAG = "mcpwm_unified";

// Static resource tracking initialization
std::bitset<8> McpwmUnifiedOutput::ledc_channels_used_;
std::bitset<2> McpwmUnifiedOutput::mcpwm_units_used_[3][2];
std::set<uint8_t> McpwmUnifiedOutput::gpio_pins_used_;

void McpwmUnifiedOutput::set_driver(const std::string &driver) {
  if (driver == "auto") {
    this->driver_type_ = DriverType::AUTO;
  } else if (driver == "ledc") {
    this->driver_type_ = DriverType::LEDC;
  } else if (driver == "mcpwm") {
    this->driver_type_ = DriverType::MCPWM;
  }
}

void McpwmUnifiedOutput::setup() {
  ESP_LOGCONFIG(TAG, "Setting up MCPWM Unified Output...");
  
  if (this->pin_ == nullptr) {
    ESP_LOGE(TAG, "Pin not configured!");
    this->mark_failed();
    return;
  }

  // Check for GPIO conflicts
  uint8_t pin_num = this->pin_->get_pin();
  if (gpio_pins_used_.count(pin_num)) {
    ESP_LOGE(TAG, "GPIO %d already in use by another PWM output", pin_num);
    this->mark_failed();
    return;
  }

  // Attempt allocation based on driver preference
  bool allocation_success = false;
  
  if (this->driver_type_ == DriverType::LEDC) {
    allocation_success = this->allocate_ledc_channel();
  } else if (this->driver_type_ == DriverType::MCPWM) {
    allocation_success = this->allocate_mcpwm_channel();
  } else {  // AUTO
    // Try LEDC first, then MCPWM
    allocation_success = this->allocate_ledc_channel();
    if (!allocation_success) {
      allocation_success = this->allocate_mcpwm_channel();
    }
  }

  if (!allocation_success) {
    ESP_LOGE(TAG, "Failed to allocate PWM channel for GPIO %d", pin_num);
    this->mark_failed();
    return;
  }

  // Reserve the GPIO pin
  gpio_pins_used_.insert(pin_num);

  // Setup the allocated driver
  if (this->allocated_driver_ == AllocatedDriver::LEDC) {
    if (!this->setup_ledc()) {
      ESP_LOGE(TAG, "Failed to setup LEDC");
      this->mark_failed();
      return;
    }
  } else if (this->allocated_driver_ == AllocatedDriver::MCPWM) {
    if (!this->setup_mcpwm()) {
      ESP_LOGE(TAG, "Failed to setup MCPWM");
      this->mark_failed();
      return;
    }
  }

  ESP_LOGD(TAG, "Successfully setup PWM output on GPIO %d using %s", 
           pin_num, this->allocated_driver_ == AllocatedDriver::LEDC ? "LEDC" : "MCPWM");
}

bool McpwmUnifiedOutput::allocate_ledc_channel() {
  // Try to allocate preferred channel if specified
  if (this->preferred_channel_.has_value() && this->preferred_channel_.value() < 8) {
    uint8_t channel = this->preferred_channel_.value();
    if (!ledc_channels_used_[channel]) {
      ledc_channels_used_[channel] = true;
      this->allocated_channel_ = channel;
      this->ledc_channel_ = static_cast<ledc_channel_t>(channel);
      this->allocated_driver_ = AllocatedDriver::LEDC;
      return true;
    }
  }

  // Find any available LEDC channel
  for (uint8_t i = 0; i < 8; i++) {
    if (!ledc_channels_used_[i]) {
      ledc_channels_used_[i] = true;
      this->allocated_channel_ = i;
      this->ledc_channel_ = static_cast<ledc_channel_t>(i);
      this->allocated_driver_ = AllocatedDriver::LEDC;
      return true;
    }
  }
  
  return false;
}

bool McpwmUnifiedOutput::allocate_mcpwm_channel() {
  // Try to allocate preferred unit/timer/operator if specified
  if (this->mcpwm_unit_ < 2 && this->mcpwm_timer_ < 3) {
    if (!mcpwm_units_used_[this->mcpwm_timer_][this->mcpwm_operator_][this->mcpwm_unit_]) {
      mcpwm_units_used_[this->mcpwm_timer_][this->mcpwm_operator_][this->mcpwm_unit_] = true;
      this->allocated_mcpwm_unit_ = static_cast<mcpwm_unit_t>(this->mcpwm_unit_);
      this->allocated_mcpwm_timer_ = static_cast<mcpwm_timer_t>(this->mcpwm_timer_);
      this->allocated_mcpwm_operator_ = static_cast<mcpwm_operator_t>(this->mcpwm_operator_);
      this->allocated_driver_ = AllocatedDriver::MCPWM;
      return true;
    }
  }

  // Find any available MCPWM channel
  for (uint8_t unit = 0; unit < 2; unit++) {
    for (uint8_t timer = 0; timer < 3; timer++) {
      for (uint8_t op = 0; op < 2; op++) {
        if (!mcpwm_units_used_[timer][op][unit]) {
          mcpwm_units_used_[timer][op][unit] = true;
          this->allocated_mcpwm_unit_ = static_cast<mcpwm_unit_t>(unit);
          this->allocated_mcpwm_timer_ = static_cast<mcpwm_timer_t>(timer);
          this->allocated_mcpwm_operator_ = static_cast<mcpwm_operator_t>(op);
          this->allocated_driver_ = AllocatedDriver::MCPWM;
          return true;
        }
      }
    }
  }
  
  return false;
}

bool McpwmUnifiedOutput::setup_ledc() {
  // Calculate optimal resolution for frequency
  uint32_t resolution = this->frequency_to_ledc_resolution(this->frequency_);
  
  // Configure LEDC timer
  ledc_timer_config_t ledc_timer = {
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .duty_resolution = static_cast<ledc_timer_bit_t>(resolution),
    .timer_num = this->ledc_timer_,
    .freq_hz = static_cast<uint32_t>(this->frequency_),
    .clk_cfg = LEDC_AUTO_CLK
  };
  
  esp_err_t err = ledc_timer_config(&ledc_timer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(err));
    return false;
  }

  // Configure LEDC channel
  ledc_channel_config_t ledc_channel = {
    .gpio_num = static_cast<int>(this->pin_->get_pin()),
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel = this->ledc_channel_,
    .intr_type = LEDC_INTR_DISABLE,
    .timer_sel = this->ledc_timer_,
    .duty = 0,
    .hpoint = 0
  };
  
  err = ledc_channel_config(&ledc_channel);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(err));
    return false;
  }

  return true;
}

bool McpwmUnifiedOutput::setup_mcpwm() {
  // Initialize MCPWM GPIO
  mcpwm_io_signals_t io_signal;
  if (this->allocated_mcpwm_operator_ == MCPWM_OPR_A) {
    io_signal = (this->allocated_mcpwm_timer_ == MCPWM_TIMER_0) ? MCPWM0A :
                (this->allocated_mcpwm_timer_ == MCPWM_TIMER_1) ? MCPWM1A : MCPWM2A;
  } else {
    io_signal = (this->allocated_mcpwm_timer_ == MCPWM_TIMER_0) ? MCPWM0B :
                (this->allocated_mcpwm_timer_ == MCPWM_TIMER_1) ? MCPWM1B : MCPWM2B;
  }
  
  esp_err_t err = mcpwm_gpio_init(this->allocated_mcpwm_unit_, io_signal, this->pin_->get_pin());
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "MCPWM GPIO init failed: %s", esp_err_to_name(err));
    return false;
  }

  // Configure MCPWM
  mcpwm_config_t pwm_config = {
    .frequency = static_cast<uint32_t>(this->frequency_),
    .cmpr_a = 0,
    .cmpr_b = 0,
    .counter_mode = MCPWM_UP_COUNTER,
    .duty_mode = MCPWM_DUTY_MODE_0,
  };
  
  err = mcpwm_init(this->allocated_mcpwm_unit_, this->allocated_mcpwm_timer_, &pwm_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "MCPWM init failed: %s", esp_err_to_name(err));
    return false;
  }

  return true;
}

void McpwmUnifiedOutput::write_state(float state) {
  if (state < 0.0f) state = 0.0f;
  if (state > 1.0f) state = 1.0f;

  if (this->allocated_driver_ == AllocatedDriver::LEDC) {
    this->write_ledc_state(state);
  } else if (this->allocated_driver_ == AllocatedDriver::MCPWM) {
    this->write_mcpwm_state(state);
  }
}

void McpwmUnifiedOutput::write_ledc_state(float state) {
  uint32_t resolution = this->frequency_to_ledc_resolution(this->frequency_);
  uint32_t max_duty = (1 << resolution) - 1;
  uint32_t duty = static_cast<uint32_t>(state * max_duty);
  
  esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, this->ledc_channel_, duty);
  if (err == ESP_OK) {
    ledc_update_duty(LEDC_LOW_SPEED_MODE, this->ledc_channel_);
  } else {
    ESP_LOGW(TAG, "LEDC set duty failed: %s", esp_err_to_name(err));
  }
}

void McpwmUnifiedOutput::write_mcpwm_state(float state) {
  float duty_percent = state * 100.0f;
  
  esp_err_t err = mcpwm_set_duty(this->allocated_mcpwm_unit_, this->allocated_mcpwm_timer_, 
                                 this->allocated_mcpwm_operator_, duty_percent);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "MCPWM set duty failed: %s", esp_err_to_name(err));
  }
}

uint32_t McpwmUnifiedOutput::frequency_to_ledc_resolution(float freq) {
  // Calculate optimal resolution based on frequency
  // Higher frequencies need lower resolution due to clock limitations
  if (freq >= 40000) return 10;  // 10-bit (1024 levels)
  if (freq >= 20000) return 11;  // 11-bit (2048 levels)
  if (freq >= 10000) return 12;  // 12-bit (4096 levels)
  if (freq >= 5000) return 13;   // 13-bit (8192 levels)
  return 14;  // 14-bit (16384 levels) for lower frequencies
}

void McpwmUnifiedOutput::dump_config() {
  ESP_LOGCONFIG(TAG, "MCPWM Unified Output:");
  ESP_LOGCONFIG(TAG, "  Pin: GPIO%d", this->pin_->get_pin());
  ESP_LOGCONFIG(TAG, "  Frequency: %.1f Hz", this->frequency_);
  
  if (this->allocated_driver_ == AllocatedDriver::LEDC) {
    ESP_LOGCONFIG(TAG, "  Driver: LEDC (Channel %d)", this->allocated_channel_);
    uint32_t resolution = this->frequency_to_ledc_resolution(this->frequency_);
    ESP_LOGCONFIG(TAG, "  Resolution: %d-bit", resolution);
  } else if (this->allocated_driver_ == AllocatedDriver::MCPWM) {
    ESP_LOGCONFIG(TAG, "  Driver: MCPWM (Unit %d, Timer %d, Operator %s)", 
                  this->allocated_mcpwm_unit_, this->allocated_mcpwm_timer_, 
                  this->allocated_mcpwm_operator_ == MCPWM_OPR_A ? "A" : "B");
  } else {
    ESP_LOGCONFIG(TAG, "  Driver: Not allocated");
  }
}

}  // namespace mcpwm_unified
}  // namespace esphome

#endif