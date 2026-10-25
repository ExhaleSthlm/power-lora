import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from . import PowerLoRa

DEPENDENCIES = ["power_lora"]
FIELDS = ["radio_ready", "link_alive", "mock_data"]
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID("power_lora_id"): cv.use_id(PowerLoRa),
    cv.Optional("radio_ready"): binary_sensor.binary_sensor_schema(entity_category="diagnostic"),
    cv.Optional("link_alive"): binary_sensor.binary_sensor_schema(device_class="connectivity", entity_category="diagnostic"),
    cv.Optional("mock_data"): binary_sensor.binary_sensor_schema(entity_category="diagnostic"),
})

async def to_code(config):
    parent = await cg.get_variable(config["power_lora_id"])
    for i, name in enumerate(FIELDS):
        if name in config:
            cg.add(parent.set_binary(i, await binary_sensor.new_binary_sensor(config[name])))
