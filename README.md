# ESPHome MCPWM Unified Component

A high-performance ESPHome component that provides unified PWM output functionality using both LEDC and MCPWM drivers on ESP32 platforms. This component automatically selects the optimal driver and manages resource allocation across multiple PWM channels.

## Features

- **Unified Interface**: Single component supporting both LEDC and MCPWM drivers
- **Automatic Driver Selection**: Prefers LEDC for efficiency, falls back to MCPWM when needed
- **Resource Management**: Automatic channel allocation with conflict detection
- **High Channel Count**: Supports up to 20 PWM channels (8 LEDC + 12 MCPWM)
- **Wide Frequency Range**: Optimized resolution based on frequency requirements
- **ESPHome Integration**: Works seamlessly with ESPHome lights and output controls
- **Home Assistant Ready**: Full compatibility with Home Assistant automation

## Supported Platforms

| Platform | LEDC Channels | MCPWM Channels | Total Channels |
|----------|---------------|----------------|----------------|
| ESP32    | 8             | 12             | 20             |
| ESP32-S3 | 8             | 12             | 20             |

## Installation

### Method 1: External Component (Recommended)

Add to your ESPHome YAML configuration:

```yaml
external_components:
  - source: /path/to/mcpwm_sync_example/my_components
    components: [ mcpwm_unified ]
```

### Method 2: Local Components

1. Copy the `my_components/mcpwm_unified/` directory to your ESPHome configuration folder
2. Reference it in your YAML:

```yaml
external_components:
  - source: my_components
    components: [ mcpwm_unified ]
```

## Basic Usage

### Simple PWM Output

```yaml
output:
  - platform: mcpwm_unified
    id: pwm_output_1
    pin: GPIO1
    frequency: 50000Hz

light:
  - platform: monochromatic
    id: my_light
    output: pwm_output_1
    name: "PWM Light"
```

### Multiple Outputs with Auto Allocation

```yaml
output:
  # These will automatically use LEDC channels 0-7
  - platform: mcpwm_unified
    id: pwm_1
    pin: GPIO13
    frequency: 25000Hz
  
  - platform: mcpwm_unified
    id: pwm_2
    pin: GPIO14
    frequency: 25000Hz
    
  # This will automatically use MCPWM when LEDC is full
  - platform: mcpwm_unified
    id: pwm_9
    pin: GPIO1
    frequency: 50000Hz
```

### Explicit Driver Selection

```yaml
output:
  # Force LEDC driver
  - platform: mcpwm_unified
    id: ledc_output
    pin: GPIO13
    frequency: 25000Hz
    driver: ledc
    
  # Force MCPWM driver
  - platform: mcpwm_unified
    id: mcpwm_output
    pin: GPIO1
    frequency: 50000Hz
    driver: mcpwm
    mcpwm_unit: 0
    mcpwm_timer: 1
    mcpwm_operator: A
```

## Configuration Options

### Required Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `pin` | GPIO | Output GPIO pin number |

### Optional Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `frequency` | Hz | 50000 | PWM frequency in Hz |
| `driver` | string | "auto" | Driver selection: "auto", "ledc", "mcpwm" |
| `channel` | int | auto | Preferred channel number (0-19) |
| `mcpwm_unit` | int | 0 | MCPWM unit (0-1) for MCPWM driver |
| `mcpwm_timer` | int | 0 | MCPWM timer (0-2) for MCPWM driver |
| `mcpwm_operator` | string | "A" | MCPWM operator ("A" or "B") for MCPWM driver |

### Driver Selection

- **`auto`** (default): Tries LEDC first, falls back to MCPWM
- **`ledc`**: Forces LEDC driver usage
- **`mcpwm`**: Forces MCPWM driver usage

## Channel Allocation Strategy

1. **LEDC Priority**: Channels 0-7 are allocated to LEDC for maximum efficiency
2. **MCPWM Fallback**: Channels 8-19 use MCPWM when LEDC is exhausted
3. **Conflict Prevention**: Automatic detection of GPIO pin conflicts
4. **Resource Tracking**: Prevents double allocation of channels

## GPIO Pin Recommendations

### LEDC Pins (Channels 0-7)
Recommended pins: 13, 14, 15, 16, 17, 18, 19, 21

### MCPWM Pins (Channels 8-19)  
Recommended pins: 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12

*Note: These recommendations are based on the original ESP-IDF MCPWM example. Most GPIO pins will work, but avoid strapping pins when possible.*

## Advanced Configuration Examples

### High-Resolution Servo Control

```yaml
output:
  - platform: mcpwm_unified
    id: servo_pwm
    pin: GPIO1
    frequency: 50Hz    # Standard servo frequency
    driver: mcpwm      # MCPWM for precise timing
    
servo:
  - id: my_servo
    output: servo_pwm
    auto_detach_time: 0s
```

### LED Strip Dimming

```yaml
output:
  - platform: mcpwm_unified
    id: led_strip_pwm
    pin: GPIO13
    frequency: 100000Hz  # High frequency to avoid flicker
    driver: ledc         # LEDC for efficient LED control

light:
  - platform: monochromatic
    id: led_strip
    output: led_strip_pwm
    name: "LED Strip"
    gamma_correct: 2.8
```

### Motor Speed Control

```yaml
output:
  - platform: mcpwm_unified
    id: motor_pwm
    pin: GPIO1
    frequency: 25000Hz
    driver: mcpwm
    mcpwm_unit: 0
    mcpwm_timer: 0
    mcpwm_operator: A

fan:
  - platform: speed
    id: motor_fan
    output: motor_pwm
    name: "Motor Fan"
```

## Performance Characteristics

### LEDC Driver
- **Best for**: LED dimming, basic PWM applications
- **Advantages**: Low CPU overhead, power efficient
- **Resolution**: 10-14 bits depending on frequency
- **Frequency Range**: 1Hz - 40MHz

### MCPWM Driver  
- **Best for**: Motor control, servo control, precise timing
- **Advantages**: Hardware synchronization, deadtime control, fault detection
- **Resolution**: Fixed by frequency setting
- **Frequency Range**: 1Hz - 1MHz

## Frequency vs Resolution

| Frequency Range | LEDC Resolution | Notes |
|-----------------|-----------------|-------|
| ≥40kHz | 10-bit (1024 levels) | High frequency applications |
| 20-40kHz | 11-bit (2048 levels) | Balanced performance |
| 10-20kHz | 12-bit (4096 levels) | Good resolution |
| 5-10kHz | 13-bit (8192 levels) | High resolution |
| <5kHz | 14-bit (16384 levels) | Maximum resolution |

## Troubleshooting

### Compilation Issues

If you encounter compilation errors:

1. Ensure you're using a compatible ESP-IDF version (5.0+)
2. Check that all required headers are available
3. Verify GPIO pin assignments don't conflict

### Runtime Issues

**"GPIO already in use"**
- Check for duplicate pin assignments
- Verify no conflicts with other ESPHome components

**"Failed to allocate PWM channel"**
- You've exceeded the 20-channel limit
- Try explicit channel assignment with `channel:` parameter

**"LEDC/MCPWM init failed"**
- GPIO pin may not support PWM output
- Check ESP32 pin capability matrix

### Debug Logging

Enable debug logging to troubleshoot allocation issues:

```yaml
logger:
  level: DEBUG
  logs:
    mcpwm_unified: DEBUG
```

## Component Architecture

```
mcpwm_unified/
├── __init__.py          # Component registration
├── output.py            # ESPHome schema and code generation  
├── mcpwm_unified.h      # C++ header with class definitions
└── mcpwm_unified.cpp    # C++ implementation with drivers
```

## Contributing

This component is based on the ESP-IDF MCPWM synchronization example and adapted for ESPHome. Contributions are welcome for:

- Additional MCPWM features (deadtime, fault detection, sync)
- Performance optimizations
- Extended platform support
- Bug fixes and improvements

## License

This project follows the same license as the original ESP-IDF example code.

## Changelog

### v1.0.0
- Initial release with LEDC/MCPWM unified driver
- Automatic resource allocation
- Full ESPHome integration
- Support for 20 PWM channels
- Comprehensive error handling and logging