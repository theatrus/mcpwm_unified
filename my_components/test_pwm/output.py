import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import output
from esphome import pins
from esphome.const import CONF_ID, CONF_PIN, CONF_FREQUENCY, CONF_CHANNEL

from . import test_pwm_ns

TestPwmOutput = test_pwm_ns.class_("TestPwmOutput", output.FloatOutput, cg.Component)

DRIVER_OPTIONS = ["auto", "ledc", "mcpwm"]
CONF_DRIVER = "driver"
CONF_MCPWM_UNIT = "mcpwm_unit"
CONF_MCPWM_TIMER = "mcpwm_timer"
CONF_MCPWM_OPERATOR = "mcpwm_operator"

CONFIG_SCHEMA = output.FLOAT_OUTPUT_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(TestPwmOutput),
        cv.Required(CONF_PIN): pins.gpio_output_pin_schema,
        cv.Optional(CONF_FREQUENCY, default=50000): cv.frequency,
        cv.Optional(CONF_DRIVER, default="auto"): cv.one_of(*DRIVER_OPTIONS, lower=True),
        cv.Optional(CONF_CHANNEL): cv.int_range(min=0, max=19),
        cv.Optional(CONF_MCPWM_UNIT): cv.int_range(min=0, max=1),
        cv.Optional(CONF_MCPWM_TIMER): cv.int_range(min=0, max=2),
        cv.Optional(CONF_MCPWM_OPERATOR, default="A"): cv.one_of("A", "B", upper=True),
    }
).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await output.register_output(var, config)
    await cg.register_component(var, config)

    pin = await cg.gpio_pin_expression(config[CONF_PIN])
    cg.add(var.set_pin(pin))
    cg.add(var.set_frequency(config[CONF_FREQUENCY]))
    cg.add(var.set_driver(config[CONF_DRIVER]))