import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from . import PowerLoRa

DEPENDENCIES = ["power_lora"]

# Order is the v2 wire contract. Append/change only with a new version.
FIELDS = [
    "energy_import", "reactive_energy_import",
    "power_import", "reactive_power_import",
    "power_import_l1", "power_import_l2", "power_import_l3",
    "reactive_power_import_l1", "reactive_power_import_l2", "reactive_power_import_l3",
    "voltage_l1", "voltage_l2", "voltage_l3", "current_l1", "current_l2", "current_l3",
]
DIAGNOSTICS = ["packets_received", "packets_missing", "frames_rejected", "duplicates",
               "transmitter_restarts", "last_packet_age", "rssi", "packets_sent"]

def field_schema(index):
    if index < 2:
        unit, decimals, device = ("kWh", 3, "energy") if index == 0 else ("kvarh", 3, None)
    elif index < 10:
        reactive = index == 3 or index >= 7
        unit, decimals, device = ("var", 0, "reactive_power") if reactive else ("W", 0, "power")
    elif index < 13:
        unit, decimals, device = "V", 1, "voltage"
    else:
        unit, decimals, device = "A", 2, "current"
    options = dict(unit_of_measurement=unit, accuracy_decimals=decimals,
                   state_class="total_increasing" if index < 2 else "measurement")
    if device:
        options["device_class"] = device
    return sensor.sensor_schema(**options)

schema = {cv.GenerateID("power_lora_id"): cv.use_id(PowerLoRa)}
schema.update({cv.Optional(name): field_schema(i) for i, name in enumerate(FIELDS)})
for name in DIAGNOSTICS:
    options = dict(accuracy_decimals=0, entity_category="diagnostic", state_class="measurement")
    if name == "last_packet_age":
        options.update(unit_of_measurement="s", device_class="duration")
    elif name == "rssi":
        options.update(unit_of_measurement="dBm", device_class="signal_strength")
    schema[cv.Optional(name)] = sensor.sensor_schema(**options)
CONFIG_SCHEMA = cv.Schema(schema)

async def to_code(config):
    parent = await cg.get_variable(config["power_lora_id"])
    for i, name in enumerate(FIELDS):
        if name in config:
            cg.add(parent.set_measurement(i, await sensor.new_sensor(config[name])))
    for i, name in enumerate(DIAGNOSTICS):
        if name in config:
            cg.add(parent.set_diagnostic(i, await sensor.new_sensor(config[name])))
