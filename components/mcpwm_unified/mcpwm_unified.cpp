#include "mcpwm_unified.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <algorithm>
#include <bitset>
#include <cmath>
#include <set>

#ifdef USE_ESP32

namespace esphome {
namespace mcpwm_unified {

const char *const TAG = "mcpwm_unified";

// Static resource tracking initialization
std::bitset<8> McpwmUnifiedOutput::ledc_channels_used_;
std::set<uint8_t> McpwmUnifiedOutput::gpio_pins_used_;

namespace {

// MCPWM timers count at 10 MHz: 256 duty steps at 39 kHz, and any frequency from about
// 153 Hz (65536 ticks) up fits the 16-bit period. All timers in a group share one
// prescaler, so they all use this resolution.
constexpr uint32_t MCPWM_RESOLUTION_HZ = 10 * 1000 * 1000;
constexpr uint32_t MCPWM_MAX_PERIOD_TICKS = 65535;

// Outputs at the same frequency share a timer; each operator follows one timer and
// drives two generators (outputs). Tracking this keeps two outputs from overwriting
// each other's frequency.
struct McpwmTimerSlot {
  bool used{false};
  uint32_t frequency{0};
  mcpwm_timer_handle_t handle{nullptr};
};
struct McpwmOperatorSlot {
  bool used{false};
  int timer{-1};
  uint8_t generators{0};
  mcpwm_oper_handle_t handle{nullptr};
};
struct McpwmGroupSlots {
  McpwmTimerSlot timers[SOC_MCPWM_TIMERS_PER_GROUP];
  McpwmOperatorSlot operators[SOC_MCPWM_OPERATORS_PER_GROUP];
};
McpwmGroupSlots mcpwm_groups[SOC_MCPWM_GROUPS];

// LEDC channels on the same timer share its frequency, so each frequency gets its own timer
struct LedcTimerSlot {
  bool used{false};
  uint32_t frequency{0};
};
LedcTimerSlot ledc_timers[LEDC_TIMER_MAX];

}  // namespace

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
    this->mark_failed(LOG_STR("Pin not configured"));
    return;
  }

  // Check for GPIO conflicts
  uint8_t pin_num = this->pin_->get_pin();
  if (gpio_pins_used_.count(pin_num)) {
    ESP_LOGE(TAG, "GPIO %d already in use by another PWM output", pin_num);
    ESP_LOGE(TAG, "Debug: Current GPIO usage:");
    for (uint8_t used_pin : gpio_pins_used_) {
      ESP_LOGE(TAG, "  GPIO %d: in use", used_pin);
    }
    ESP_LOGE(TAG, "Solution: Use a different GPIO pin or remove duplicate configuration");
    this->set_error_and_fail("GPIO " + std::to_string(pin_num) + " already in use");
    return;
  }

  // Attempt allocation based on driver preference
  bool allocation_success = false;
  
  ESP_LOGD(TAG, "Attempting channel allocation for GPIO %d with driver preference: %s",
           pin_num, this->driver_type_ == DriverType::LEDC ? "LEDC" :
                    this->driver_type_ == DriverType::MCPWM ? "MCPWM" : "AUTO");
  
  if (this->driver_type_ == DriverType::LEDC) {
    ESP_LOGD(TAG, "Trying LEDC allocation (forced)...");
    allocation_success = this->allocate_ledc_channel();
    if (!allocation_success) {
      ESP_LOGE(TAG, "LEDC allocation failed - all 8 LEDC channels in use");
    }
  } else if (this->driver_type_ == DriverType::MCPWM) {
    ESP_LOGD(TAG, "Trying MCPWM allocation (forced)...");
    allocation_success = this->allocate_mcpwm_channel();
    if (!allocation_success) {
      ESP_LOGE(TAG, "MCPWM allocation failed - all 12 MCPWM channels in use");
    }
  } else {  // AUTO
    // Frequency-based allocation: prefer MCPWM for >16kHz, LEDC for <=16kHz
    bool prefer_mcpwm = this->frequency_ > 16000.0f;
    
    if (prefer_mcpwm) {
      ESP_LOGD(TAG, "Frequency %.1f Hz > 16kHz, trying MCPWM allocation first (auto)...", this->frequency_);
      allocation_success = this->allocate_mcpwm_channel();
      if (!allocation_success) {
        ESP_LOGD(TAG, "MCPWM allocation failed, trying LEDC fallback (auto)...");
        allocation_success = this->allocate_ledc_channel();
        if (!allocation_success) {
          ESP_LOGE(TAG, "Both MCPWM and LEDC allocation failed - all 20 channels in use");
        }
      }
    } else {
      ESP_LOGD(TAG, "Frequency %.1f Hz <= 16kHz, trying LEDC allocation first (auto)...", this->frequency_);
      allocation_success = this->allocate_ledc_channel();
      if (!allocation_success) {
        ESP_LOGD(TAG, "LEDC allocation failed, trying MCPWM fallback (auto)...");
        allocation_success = this->allocate_mcpwm_channel();
        if (!allocation_success) {
          ESP_LOGE(TAG, "Both LEDC and MCPWM allocation failed - all 20 channels in use");
        }
      }
    }
  }

  if (!allocation_success) {
    ESP_LOGE(TAG, "Failed to allocate PWM channel for GPIO %d", pin_num);
    this->log_resource_usage();
    
    // Create specific failure reason based on driver type
    std::string reason;
    if (this->driver_type_ == DriverType::LEDC) {
      reason = "All 8 LEDC channels exhausted";
    } else if (this->driver_type_ == DriverType::MCPWM) {
      reason = "All 12 MCPWM channels exhausted";  
    } else {
      reason = "All 20 PWM channels exhausted (8 LEDC + 12 MCPWM)";
    }
    
    this->set_error_and_fail(reason);
    return;
  }

  // Reserve the GPIO pin
  gpio_pins_used_.insert(pin_num);

  // Setup the allocated driver
  if (this->allocated_driver_ == AllocatedDriver::LEDC) {
    ESP_LOGD(TAG, "Setting up LEDC driver (Channel %d, Timer %d, Frequency %.1f Hz)", 
             this->allocated_channel_, this->ledc_timer_, this->frequency_);
    std::string ledc_failure_reason;
    if (!this->setup_ledc(ledc_failure_reason)) {
      ESP_LOGE(TAG, "Failed to setup LEDC for GPIO %d", pin_num);
      ESP_LOGE(TAG, "Debug: LEDC Channel %d, Timer %d, Frequency %.1f Hz", 
               this->allocated_channel_, this->ledc_timer_, this->frequency_);
      ESP_LOGE(TAG, "Possible causes: Invalid frequency, GPIO not PWM capable, hardware conflict");
      this->set_error_and_fail("LEDC setup failed for GPIO " + std::to_string(pin_num) + ": " + ledc_failure_reason);
      return;
    }
  } else if (this->allocated_driver_ == AllocatedDriver::MCPWM) {
    ESP_LOGD(TAG, "Setting up MCPWM driver (Group %d, Operator %d, Frequency %.1f Hz)",
             this->mcpwm_group_, this->mcpwm_operator_slot_, this->frequency_);
    std::string mcpwm_failure_reason;
    if (!this->setup_mcpwm(mcpwm_failure_reason)) {
      ESP_LOGE(TAG, "Failed to setup MCPWM for GPIO %d", pin_num);
      ESP_LOGE(TAG, "Debug: Group %d, Operator %d, Frequency %.1f Hz",
               this->mcpwm_group_, this->mcpwm_operator_slot_, this->frequency_);
      ESP_LOGE(TAG, "Possible causes: Invalid frequency, GPIO not MCPWM capable, timer conflict");
      this->set_error_and_fail("MCPWM setup failed for GPIO " + std::to_string(pin_num) + ": " + mcpwm_failure_reason);
      return;
    }
  }

  ESP_LOGD(TAG, "Successfully setup PWM output on GPIO %d using %s", 
           pin_num, this->allocated_driver_ == AllocatedDriver::LEDC ? "LEDC" : "MCPWM");
}

bool McpwmUnifiedOutput::allocate_ledc_channel() {
  // Find a timer already running at this frequency, or a free one
  uint32_t frequency = static_cast<uint32_t>(this->frequency_);
  int timer = -1;
  for (int t = 0; t < LEDC_TIMER_MAX && timer < 0; t++) {
    if (ledc_timers[t].used && ledc_timers[t].frequency == frequency) {
      timer = t;
    }
  }
  for (int t = 0; t < LEDC_TIMER_MAX && timer < 0; t++) {
    if (!ledc_timers[t].used) {
      timer = t;
    }
  }
  if (timer < 0) {
    ESP_LOGD(TAG, "No LEDC timer free for %" PRIu32 " Hz", frequency);
    return false;
  }

  // Try to allocate preferred channel if specified, else any free channel
  int channel = -1;
  if (this->preferred_channel_.has_value() && this->preferred_channel_.value() < 8 &&
      !ledc_channels_used_[this->preferred_channel_.value()]) {
    channel = this->preferred_channel_.value();
  }
  for (uint8_t i = 0; i < 8 && channel < 0; i++) {
    if (!ledc_channels_used_[i]) {
      channel = i;
    }
  }
  if (channel < 0) {
    return false;
  }

  ledc_timers[timer].used = true;
  ledc_timers[timer].frequency = frequency;
  ledc_channels_used_[channel] = true;
  this->allocated_channel_ = channel;
  this->ledc_channel_ = static_cast<ledc_channel_t>(channel);
  this->ledc_timer_ = static_cast<ledc_timer_t>(timer);
  this->allocated_driver_ = AllocatedDriver::LEDC;
  return true;
}

bool McpwmUnifiedOutput::allocate_mcpwm_channel() {
  uint32_t frequency = static_cast<uint32_t>(this->frequency_);
  uint32_t period_ticks = frequency > 0 ? MCPWM_RESOLUTION_HZ / frequency : 0;
  if (period_ticks < 2 || period_ticks > MCPWM_MAX_PERIOD_TICKS) {
    ESP_LOGD(TAG, "%" PRIu32 " Hz is outside the MCPWM range", frequency);
    return false;
  }

  int first_group = this->mcpwm_unit_ < SOC_MCPWM_GROUPS ? this->mcpwm_unit_ : 0;
  auto take = [this, period_ticks](int group, int op) {
    mcpwm_groups[group].operators[op].generators++;
    this->mcpwm_group_ = group;
    this->mcpwm_operator_slot_ = op;
    this->mcpwm_period_ticks_ = period_ticks;
    this->allocated_driver_ = AllocatedDriver::MCPWM;
    return true;
  };

  // First choice: a spare generator on an operator whose timer runs at this frequency
  for (int i = 0; i < SOC_MCPWM_GROUPS; i++) {
    int group = (first_group + i) % SOC_MCPWM_GROUPS;
    auto &slots = mcpwm_groups[group];
    for (int op = 0; op < SOC_MCPWM_OPERATORS_PER_GROUP; op++) {
      auto &oper = slots.operators[op];
      if (oper.used && oper.generators < SOC_MCPWM_GENERATORS_PER_OPERATOR &&
          slots.timers[oper.timer].frequency == frequency) {
        return take(group, op);
      }
    }
  }

  // Otherwise a free operator, on a timer at this frequency or a free timer
  for (int i = 0; i < SOC_MCPWM_GROUPS; i++) {
    int group = (first_group + i) % SOC_MCPWM_GROUPS;
    auto &slots = mcpwm_groups[group];
    int op = -1;
    for (int o = 0; o < SOC_MCPWM_OPERATORS_PER_GROUP && op < 0; o++) {
      if (!slots.operators[o].used) {
        op = o;
      }
    }
    int timer = -1;
    for (int t = 0; t < SOC_MCPWM_TIMERS_PER_GROUP && timer < 0; t++) {
      if (slots.timers[t].used && slots.timers[t].frequency == frequency) {
        timer = t;
      }
    }
    for (int t = 0; t < SOC_MCPWM_TIMERS_PER_GROUP && timer < 0; t++) {
      if (!slots.timers[t].used) {
        timer = t;
      }
    }
    if (op < 0 || timer < 0) {
      continue;
    }
    slots.timers[timer].used = true;
    slots.timers[timer].frequency = frequency;
    slots.operators[op].used = true;
    slots.operators[op].timer = timer;
    return take(group, op);
  }

  return false;
}

bool McpwmUnifiedOutput::setup_ledc(std::string &failure_reason) {
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
    failure_reason = std::string("LEDC timer config failed: ") + esp_err_to_name(err);
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
    failure_reason = std::string("LEDC channel config failed: ") + esp_err_to_name(err);
    return false;
  }

  return true;
}

bool McpwmUnifiedOutput::setup_mcpwm(std::string &failure_reason) {
  auto &slots = mcpwm_groups[this->mcpwm_group_];
  auto &oper = slots.operators[this->mcpwm_operator_slot_];
  auto &timer = slots.timers[oper.timer];
  auto fail = [&failure_reason](const char *step, esp_err_t err) {
    ESP_LOGE(TAG, "MCPWM %s failed: %s", step, esp_err_to_name(err));
    failure_reason = std::string("MCPWM ") + step + " failed: " + esp_err_to_name(err);
    return false;
  };
  esp_err_t err;

  // The first output at a frequency creates and starts the shared timer
  if (timer.handle == nullptr) {
    mcpwm_timer_config_t timer_config = {};
    timer_config.group_id = this->mcpwm_group_;
    timer_config.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT;
    timer_config.resolution_hz = MCPWM_RESOLUTION_HZ;
    timer_config.count_mode = MCPWM_TIMER_COUNT_MODE_UP;
    timer_config.period_ticks = this->mcpwm_period_ticks_;
    if ((err = mcpwm_new_timer(&timer_config, &timer.handle)) != ESP_OK) return fail("timer", err);
    if ((err = mcpwm_timer_enable(timer.handle)) != ESP_OK) return fail("timer enable", err);
    if ((err = mcpwm_timer_start_stop(timer.handle, MCPWM_TIMER_START_NO_STOP)) != ESP_OK) return fail("timer start", err);
  }

  if (oper.handle == nullptr) {
    mcpwm_operator_config_t operator_config = {};
    operator_config.group_id = this->mcpwm_group_;
    if ((err = mcpwm_new_operator(&operator_config, &oper.handle)) != ESP_OK) return fail("operator", err);
    if ((err = mcpwm_operator_connect_timer(oper.handle, timer.handle)) != ESP_OK) return fail("operator connect", err);
  }

  // Load new compare values at the start of a period so a duty change never cuts a pulse short
  mcpwm_comparator_config_t comparator_config = {};
  comparator_config.flags.update_cmp_on_tez = true;
  if ((err = mcpwm_new_comparator(oper.handle, &comparator_config, &this->mcpwm_comparator_)) != ESP_OK) {
    return fail("comparator", err);
  }
  if ((err = mcpwm_comparator_set_compare_value(this->mcpwm_comparator_, 0)) != ESP_OK) return fail("compare", err);

  mcpwm_generator_config_t generator_config = {};
  generator_config.gen_gpio_num = this->pin_->get_pin();
  if ((err = mcpwm_new_generator(oper.handle, &generator_config, &this->mcpwm_generator_)) != ESP_OK) {
    return fail("generator", err);
  }
  // Hold the pin low until the first write, as the legacy driver did with a 0% duty
  if ((err = mcpwm_generator_set_force_level(this->mcpwm_generator_, 0, true)) != ESP_OK) return fail("force level", err);

  // High at the start of each period, low when the counter reaches the compare value
  err = mcpwm_generator_set_action_on_timer_event(
      this->mcpwm_generator_,
      MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH));
  if (err != ESP_OK) return fail("timer action", err);
  err = mcpwm_generator_set_action_on_compare_event(
      this->mcpwm_generator_,
      MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, this->mcpwm_comparator_, MCPWM_GEN_ACTION_LOW));
  if (err != ESP_OK) return fail("compare action", err);

  return true;
}

void McpwmUnifiedOutput::write_state(float state) {
  if (state < 0.0f) state = 0.0f;
  if (state > 1.0f) state = 1.0f;

  // Invert the state if requested (0 becomes 1, 1 becomes 0)
  if (this->inverted_) {
    state = 1.0f - state;
  }

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
  if (this->mcpwm_generator_ == nullptr) {
    return;
  }
  // Hold the level for 0% and 100%: a compare value at either end of the period would
  // race the period-start event and could leave a short pulse
  if (state <= 0.0f || state >= 1.0f) {
    esp_err_t err = mcpwm_generator_set_force_level(this->mcpwm_generator_, state >= 1.0f ? 1 : 0, true);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "MCPWM force level failed: %s", esp_err_to_name(err));
    }
    return;
  }

  uint32_t ticks = static_cast<uint32_t>(lroundf(state * static_cast<float>(this->mcpwm_period_ticks_)));
  ticks = std::max<uint32_t>(1, std::min<uint32_t>(ticks, this->mcpwm_period_ticks_ - 1));
  esp_err_t err = mcpwm_comparator_set_compare_value(this->mcpwm_comparator_, ticks);
  if (err == ESP_OK) {
    err = mcpwm_generator_set_force_level(this->mcpwm_generator_, -1, true);  // Back to PWM
  }
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
  ESP_LOGCONFIG(TAG, "  Inverted: %s", this->inverted_ ? "YES" : "NO");
  
  if (this->allocated_driver_ == AllocatedDriver::LEDC) {
    ESP_LOGCONFIG(TAG, "  Driver: LEDC (Channel %d, Timer %d)", this->allocated_channel_, this->ledc_timer_);
    uint32_t resolution = this->frequency_to_ledc_resolution(this->frequency_);
    ESP_LOGCONFIG(TAG, "  Resolution: %" PRIu32 "-bit", resolution);
  } else if (this->allocated_driver_ == AllocatedDriver::MCPWM) {
    ESP_LOGCONFIG(TAG, "  Driver: MCPWM (Group %d, Operator %d, %" PRIu32 " steps per period)",
                  this->mcpwm_group_, this->mcpwm_operator_slot_, this->mcpwm_period_ticks_);
  } else {
    ESP_LOGCONFIG(TAG, "  Driver: Not allocated");
  }
}

void McpwmUnifiedOutput::log_resource_usage() {
  ESP_LOGE(TAG, "=== Resource Usage Debug Information ===");
  
  // Log LEDC channel usage
  ESP_LOGE(TAG, "LEDC Channels (0-7):");
  bool ledc_available = false;
  for (int i = 0; i < 8; i++) {
    bool used = ledc_channels_used_[i];
    ESP_LOGE(TAG, "  Channel %d: %s", i, used ? "USED" : "FREE");
    if (!used) ledc_available = true;
  }
  ESP_LOGE(TAG, "LEDC Summary: %s", ledc_available ? "Channels available" : "All channels used");
  
  // Log LEDC timer usage
  for (int t = 0; t < LEDC_TIMER_MAX; t++) {
    if (ledc_timers[t].used) {
      ESP_LOGE(TAG, "  LEDC timer %d: %" PRIu32 " Hz", t, ledc_timers[t].frequency);
    } else {
      ESP_LOGE(TAG, "  LEDC timer %d: FREE", t);
    }
  }

  // Log MCPWM usage: outputs at different frequencies need different timers
  ESP_LOGE(TAG, "MCPWM Groups:");
  bool mcpwm_available = false;
  for (int group = 0; group < SOC_MCPWM_GROUPS; group++) {
    for (int t = 0; t < SOC_MCPWM_TIMERS_PER_GROUP; t++) {
      const auto &timer = mcpwm_groups[group].timers[t];
      if (timer.used) {
        ESP_LOGE(TAG, "  Group %d timer %d: %" PRIu32 " Hz", group, t, timer.frequency);
      } else {
        ESP_LOGE(TAG, "  Group %d timer %d: FREE", group, t);
      }
    }
    for (int op = 0; op < SOC_MCPWM_OPERATORS_PER_GROUP; op++) {
      const auto &oper = mcpwm_groups[group].operators[op];
      ESP_LOGE(TAG, "  Group %d operator %d: %d of %d outputs used", group, op, oper.generators,
               SOC_MCPWM_GENERATORS_PER_OPERATOR);
      if (oper.generators < SOC_MCPWM_GENERATORS_PER_OPERATOR) mcpwm_available = true;
    }
  }
  ESP_LOGE(TAG, "MCPWM Summary: %s", mcpwm_available ? "Outputs available" : "All outputs used");
  
  // Log GPIO usage
  ESP_LOGE(TAG, "GPIO Pins in use:");
  if (gpio_pins_used_.empty()) {
    ESP_LOGE(TAG, "  None");
  } else {
    for (uint8_t pin : gpio_pins_used_) {
      ESP_LOGE(TAG, "  GPIO %d", pin);
    }
  }
  
  // Provide recommendations
  ESP_LOGE(TAG, "=== Troubleshooting Suggestions ===");
  if (!ledc_available && !mcpwm_available) {
    ESP_LOGE(TAG, "All 20 PWM channels exhausted (8 LEDC + 12 MCPWM)");
    ESP_LOGE(TAG, "Solution: Reduce number of PWM outputs or reuse existing ones");
  } else if (!ledc_available && mcpwm_available) {
    ESP_LOGE(TAG, "LEDC channels full, but MCPWM available");
    ESP_LOGE(TAG, "Try: driver: mcpwm or driver: auto");
  } else if (ledc_available && !mcpwm_available) {
    ESP_LOGE(TAG, "MCPWM channels full, but LEDC available");
    ESP_LOGE(TAG, "Try: driver: ledc or driver: auto");
  }
  
  ESP_LOGE(TAG, "Current driver preference: %s", 
           this->driver_type_ == DriverType::LEDC ? "LEDC" :
           this->driver_type_ == DriverType::MCPWM ? "MCPWM" : "AUTO");
  ESP_LOGE(TAG, "==========================================");
}

void McpwmUnifiedOutput::set_error_and_fail(const std::string &error) {
  this->error_message_ = error;
  ESP_LOGE(TAG, "%s", this->error_message_.c_str());
  this->mark_failed();
}

}  // namespace mcpwm_unified
}  // namespace esphome

#endif