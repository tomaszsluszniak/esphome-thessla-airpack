import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_CONFIG,
    UNIT_CELSIUS,
    UNIT_PERCENT,
)

from . import (
    HOLDING,
    INPUT,
    REG_MANUAL_INTENSITY,
    REG_MANUAL_TEMPERATURE,
    REG_MODE,
    REG_SUPPLY_PERCENT,
    REG_TEMP_INTENSITY,
    REG_TEMP_TEMPERATURE,
    THESSLA_CHILD_SCHEMA,
    bind_entity,
    request,
    thessla_ns,
)

DEPENDENCIES = ["thessla_airpack"]

ThesslaNumber = thessla_ns.class_("ThesslaNumber", number.Number)
NumberKind = thessla_ns.enum("NumberKind", is_class=True)


def _number(kind, address, scale, lo, hi, step, unit, icon, device_class=None, category=None, requests=()):
    kwargs = {"unit_of_measurement": unit, "icon": icon}
    if device_class:
        kwargs["device_class"] = device_class
    if category:
        kwargs["entity_category"] = category
    return {
        "kind": kind,
        "address": address,
        "scale": scale,
        "range": (lo, hi, step),
        "requests": requests,
        "schema": number.number_schema(ThesslaNumber, **kwargs),
    }


def _temp(address, lo, hi, icon="mdi:thermometer", category=ENTITY_CATEGORY_CONFIG):
    return _number(
        "DIRECT", address, 0.5, lo, hi, 0.5, UNIT_CELSIUS, icon, DEVICE_CLASS_TEMPERATURE, category
    )


NUMBERS = {
    # Like the panel: MANUAL -> manual setpoint, AUTO -> starts temporary mode.
    "intensity": _number(
        "INTENSITY",
        None,
        1.0,
        10,
        100,
        1,
        UNIT_PERCENT,
        "mdi:fan",
        requests=[
            (HOLDING, REG_MODE),
            (HOLDING, REG_MANUAL_INTENSITY),
            (HOLDING, REG_TEMP_INTENSITY),
            (INPUT, REG_SUPPLY_PERCENT),
        ],
    ),
    "manual_intensity": _number(
        "DIRECT", REG_MANUAL_INTENSITY, 1.0, 10, 100, 1, UNIT_PERCENT, "mdi:fan-speed-1"
    ),
    "supply_temperature_setpoint": _number(
        "SUPPLY_TEMPERATURE",
        None,
        0.5,
        10,
        45,
        0.5,
        UNIT_CELSIUS,
        "mdi:thermometer-chevron-up",
        DEVICE_CLASS_TEMPERATURE,
        requests=[
            (HOLDING, REG_MODE),
            (HOLDING, REG_MANUAL_TEMPERATURE),
            (HOLDING, REG_TEMP_TEMPERATURE),
        ],
    ),
    "bypass_min_temperature": _temp(0x10E1, 5, 20),
    "bypass_heating_temperature": _temp(0x10E2, 15, 30),
    "bypass_cooling_temperature": _temp(0x10E3, 15, 30),
}

CONFIG_SCHEMA = THESSLA_CHILD_SCHEMA.extend(
    {cv.Optional(key): spec["schema"] for key, spec in NUMBERS.items()}
)


async def to_code(config):
    for key, spec in NUMBERS.items():
        if key not in config:
            continue
        lo, hi, step = spec["range"]
        var = await number.new_number(config[key], min_value=lo, max_value=hi, step=step)
        if spec["address"] is None:
            await bind_entity(var, config)
        else:
            await bind_entity(var, config, HOLDING, spec["address"])
        cg.add(var.set_kind(getattr(NumberKind, spec["kind"])))
        cg.add(var.set_scale(spec["scale"]))
        for space, address in spec["requests"]:
            await request(config, space, address)
