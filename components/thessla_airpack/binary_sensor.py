import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_HEAT,
    DEVICE_CLASS_OPENING,
    DEVICE_CLASS_PROBLEM,
    DEVICE_CLASS_RUNNING,
    DEVICE_CLASS_SMOKE,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from . import COIL, DISCRETE, HOLDING, INPUT, THESSLA_CHILD_SCHEMA, bind_entity, thessla_ns

DEPENDENCIES = ["thessla_airpack"]

ThesslaBinarySensor = thessla_ns.class_(
    "ThesslaBinarySensor", binary_sensor.BinarySensor
)
BinaryKind = thessla_ns.enum("BinaryKind", is_class=True)


def _bs(space, address, icon=None, device_class=None, category=None) -> dict:
    kwargs = {}
    if icon:
        kwargs["icon"] = icon
    if device_class:
        kwargs["device_class"] = device_class
    if category:
        kwargs["entity_category"] = category
    return {
        "space": space,
        "address": address,
        "schema": binary_sensor.binary_sensor_schema(ThesslaBinarySensor, **kwargs),
    }


BINARY_SENSORS = {
    # Link to the controller (no register: derived from the poll results).
    "online": {
        "kind": "ONLINE",
        "schema": binary_sensor.binary_sensor_schema(
            ThesslaBinarySensor,
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    },
    # Alarms (panel icons "S" and "E")
    "alarm_s": _bs(HOLDING, 0x2001, "mdi:alert-octagon", DEVICE_CLASS_PROBLEM),
    "alarm_e": _bs(HOLDING, 0x2000, "mdi:alert", DEVICE_CLASS_PROBLEM),
    # Functions / relays
    "bypass_open": _bs(COIL, 9, "mdi:valve", DEVICE_CLASS_OPENING),
    "fans_powered": _bs(COIL, 11, "mdi:fan", DEVICE_CLASS_RUNNING),
    "heating_cable": _bs(COIL, 12, "mdi:heating-coil", DEVICE_CLASS_HEAT),
    "gwc_active": _bs(COIL, 14, "mdi:earth", DEVICE_CLASS_RUNNING),
    "hood_damper": _bs(COIL, 15, "mdi:stove", DEVICE_CLASS_OPENING),
    "fpx_active": _bs(HOLDING, 0x1060, "mdi:snowflake-melt", DEVICE_CLASS_RUNNING),
    "erv_active": _bs(HOLDING, 0x1260, "mdi:radiator", DEVICE_CLASS_HEAT),
    "constant_flow_active": _bs(INPUT, 0x010F, "mdi:waves-arrow-right", DEVICE_CLASS_RUNNING),
    # Physical inputs
    "hood_input": _bs(DISCRETE, 4, "mdi:stove"),
    "air_quality_input": _bs(DISCRETE, 5, "mdi:air-filter"),
    "humidistat_input": _bs(DISCRETE, 6, "mdi:water-percent"),
    "fireplace_input": _bs(DISCRETE, 14, "mdi:fireplace"),
    "fire_alarm": _bs(DISCRETE, 15, "mdi:fire-alert", DEVICE_CLASS_SMOKE),
}

CONFIG_SCHEMA = THESSLA_CHILD_SCHEMA.extend(
    {cv.Optional(key): spec["schema"] for key, spec in BINARY_SENSORS.items()}
)


async def to_code(config):
    for key, spec in BINARY_SENSORS.items():
        if key not in config:
            continue
        var = await binary_sensor.new_binary_sensor(config[key])
        if "kind" in spec:
            await bind_entity(var, config)
            cg.add(var.set_kind(getattr(BinaryKind, spec["kind"])))
        else:
            await bind_entity(var, config, spec["space"], spec["address"])
