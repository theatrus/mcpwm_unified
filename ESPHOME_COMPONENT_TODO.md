# ESPHome MCPWM Component Development Todo

## Project Overview
Transform ESP-IDF MCPWM code (combining LEDC + MCPWM drivers) into a unified ESPHome component that provides 20 PWM channels (8 LEDC + 12 MCPWM) for ESP32S3.

## Research Phase ✅

### 1. ESPHome Component Architecture Research ✅
**Key Resources:**
- ESPHome Developer Docs: https://developers.esphome.io/architecture/components/
- External Components Guide: https://esphome.io/components/external_components/
- Configuration Types: https://esphome.io/guides/configuration-types/

**Component Structure:**
```
my_components/
└── mcpwm_unified/
    ├── __init__.py          # Configuration schema and code generation
    ├── mcpwm_unified.h      # C++ header file
    └── mcpwm_unified.cpp    # C++ implementation
```

### 2. Base Classes and Inheritance Patterns ✅
- **FloatOutput**: Base class for variable-level outputs (0.0-1.0 range)
- **Component**: Primary base class with lifecycle methods
- **API Reference**: https://esphome.io/api/classesphome_1_1output_1_1_float_output

### 3. Existing PWM Component Analysis ✅
**LEDC Component:**
- Source: `esphome/components/ledc/`
- Features: 10Hz-40MHz, GPIO0-GPIO33
- LEDC API: https://esphome.io/api/ledc__output_8cpp_source

**ESP32 DAC Component:**
- Source: `esphome/components/esp32_dac/`
- Features: True analog output on GPIO25/26
- DAC API: https://esphome.io/api/esp32__dac_8cpp_source

### 4. Configuration Schema Patterns ✅
```python
CONFIG_SCHEMA = output.FLOAT_OUTPUT_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(McpwmUnifiedOutput),
    cv.Required(CONF_PIN): pins.gpio_output_pin_schema,
    cv.Optional(CONF_FREQUENCY, default=50000): cv.frequency,
    cv.Optional(CONF_CHANNEL): cv.int_range(min=0, max=19),
    cv.Optional(CONF_DRIVER, default="auto"): cv.one_of("ledc", "mcpwm", "auto")
}).extend(cv.COMPONENT_SCHEMA)
```

### 5. Build System Integration ✅
ESPHome uses ESP-IDF build system with CMakeLists.txt support.

## Implementation Phase 🔄

### Phase 1: Component Structure
- [ ] **Create component directory structure**
- [ ] **Set up __init__.py with configuration schema**
- [ ] **Define C++ header file with class definitions**

### Phase 2: Core Implementation
- [ ] **Implement unified PWM output class inheriting from FloatOutput**
- [ ] **Add LEDC driver integration (8 channels)**
- [ ] **Add MCPWM driver integration (12 channels)**
- [ ] **Implement automatic channel allocation logic**
  - Priority: LEDC first (more efficient), fallback to MCPWM
  - Track allocated channels globally

### Phase 3: Advanced Features
- [ ] **GPIO pin validation and conflict detection**
- [ ] **Frequency and duty cycle validation/conversion**
- [ ] **Error handling and logging for allocation failures**
- [ ] **Support for synchronized PWM outputs**

### Phase 4: Integration and Testing
- [ ] **Create build system integration (CMakeLists.txt)**
- [ ] **Write example YAML configuration**
```yaml
external_components:
  - source: my_components
    components: [ mcpwm_unified ]

output:
  - platform: mcpwm_unified
    id: pwm_1
    pin: GPIO1
    frequency: 50000Hz
    driver: auto  # or "ledc" or "mcpwm"
  
  - platform: mcpwm_unified
    id: pwm_2
    pin: GPIO2
    frequency: 50000Hz
```

- [ ] **Test component with ESPHome tool**
- [ ] **Validate all 20 PWM channels work correctly**
- [ ] **Performance testing and optimization**

### Phase 5: Documentation and Distribution
- [ ] **Create comprehensive documentation**
- [ ] **Add usage examples and limitations**
- [ ] **Package for distribution**

## Technical Specifications

### Current ESP-IDF Implementation Analysis
**From mcpwm_sync_example.c:**
- **LEDC Channels**: 8 channels (LEDC_CHANNEL_0 to LEDC_CHANNEL_7)
  - GPIO pins: 13, 14, 15, 16, 17, 18, 19, 21
  - Frequency: 50kHz
  - Resolution: 10-bit (0-1023)
  - Various duty cycles: 128, 256, 512, 768, 200, 300

- **MCPWM Channels**: 12 channels total
  - **MCPWM_UNIT_0**: 6 channels (3 timers × 2 operators A/B)
    - GPIO pins: 1, 2, 3, 4, 5, 6
  - **MCPWM_UNIT_1**: 6 channels (3 timers × 2 operators A/B)  
    - GPIO pins: 7, 8, 9, 10, 11, 12
  - Frequency: 50kHz
  - Duty cycles: 10-75%

### Target ESPHome Component Features
1. **Unified Interface**: Single component type for both LEDC and MCPWM
2. **Automatic Allocation**: Prefer LEDC, fallback to MCPWM when LEDC exhausted
3. **GPIO Flexibility**: Support all valid GPIO pins for PWM output
4. **Frequency Range**: Support full range of both drivers
5. **ESPHome Integration**: Full compatibility with ESPHome ecosystem
6. **Error Handling**: Graceful handling of resource exhaustion
7. **Documentation**: Clear usage examples and troubleshooting

## Implementation Priority
1. Basic component structure and LEDC integration
2. MCPWM integration and channel allocation
3. Testing with ESPHome tool
4. Advanced features and optimization
5. Documentation and packaging

## Testing Strategy
- Use installed ESPHome tool for component testing
- Create test configurations with various channel combinations
- Validate frequency accuracy and duty cycle precision
- Test resource exhaustion scenarios
- Performance benchmarking against native ESP-IDF code

---
**Status**: Research complete, ready for implementation phase
**Next Step**: Create component directory structure and basic __init__.py