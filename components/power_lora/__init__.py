import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import uart
from esphome.const import CONF_ID

DEPENDENCIES = ["uart", "esp32"]
AUTO_LOAD = ["sensor", "binary_sensor"]
ns = cg.esphome_ns.namespace("power_lora")
PowerLoRa = ns.class_("PowerLoRa", cg.Component, uart.UARTDevice)

def validate_timing(config):
    if config["transmit_interval"].total_milliseconds < config["airtime_budget"].total_milliseconds * 100:
        raise cv.Invalid("transmit_interval must be at least 100 * airtime_budget (1% profile)")
    if config["stale_after"].total_milliseconds <= config["transmit_interval"].total_milliseconds:
        raise cv.Invalid("stale_after must exceed transmit_interval")
    return config

CONFIG_SCHEMA = cv.All(cv.Schema({
    cv.GenerateID(): cv.declare_id(PowerLoRa),
    cv.Required("role"): cv.one_of("transmitter", "receiver", lower=True),
    cv.Required("m0_pin"): pins.gpio_output_pin_schema,
    cv.Required("m1_pin"): pins.gpio_output_pin_schema,
    cv.Required("aux_pin"): pins.gpio_input_pin_schema,
    cv.Optional("node_id", default=1): cv.int_range(min=1, max=65534),
    cv.Optional("transmit_interval", default="120s"): cv.All(
        cv.positive_time_period_milliseconds, cv.Range(min=cv.TimePeriod(milliseconds=10000), max=cv.TimePeriod(milliseconds=3600000))),
    cv.Optional("stale_after", default="360s"): cv.All(
        cv.positive_time_period_milliseconds, cv.Range(max=cv.TimePeriod(milliseconds=86400000))),
    # An explicit engineering budget, NOT an E220 airtime measurement.
    cv.Optional("airtime_budget", default="1000ms"): cv.All(
        cv.positive_time_period_milliseconds, cv.Range(max=cv.TimePeriod(milliseconds=30000))),
    # Deliberately restrict initial RF profile; widening needs a band review.
    cv.Optional("tx_power", default=10): cv.one_of(10, 13, int=True),
}).extend(uart.UART_DEVICE_SCHEMA).extend(cv.COMPONENT_SCHEMA), validate_timing)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "power_lora", baud_rate=9600, require_tx=True, require_rx=True,
    data_bits=8, parity="NONE", stop_bits=1)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_transmitter(config["role"] == "transmitter"))
    for name in ("m0_pin", "m1_pin", "aux_pin"):
        cg.add(getattr(var, "set_" + name)(await cg.gpio_pin_expression(config[name])))
    cg.add(var.set_node_id(config["node_id"]))
    cg.add(var.set_transmit_interval(config["transmit_interval"].total_milliseconds))
    cg.add(var.set_stale_after(config["stale_after"].total_milliseconds))
    cg.add(var.set_tx_power(config["tx_power"]))
