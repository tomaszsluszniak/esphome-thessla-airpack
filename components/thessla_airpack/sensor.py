import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    DEVICE_CLASS_VOLUME_FLOW_RATE,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_CUBIC_METER_PER_HOUR,
    UNIT_PERCENT,
    UNIT_VOLT,
)

from . import (
    HOLDING,
    INPUT,
    REG_MANUAL_INTENSITY,
    REG_MODE,
    REG_SUPPLY_PERCENT,
    REG_TEMP_INTENSITY,
    THESSLA_CHILD_SCHEMA,
    bind_entity,
    request,
    thessla_ns,
)

DEPENDENCIES = ["thessla_airpack"]

ThesslaSensor = thessla_ns.class_("ThesslaSensor", sensor.Sensor)
SensorKind = thessla_ns.enum("SensorKind", is_class=True)


def _temperature(address: int, icon: str = "mdi:thermometer") -> dict:
    return {
        "space": INPUT,
        "address": address,
        "scale": 0.1,
        "signed": True,
        "nan": 0x8000,
        "schema": sensor.sensor_schema(
            ThesslaSensor,
            unit_of_measurement=UNIT_CELSIUS,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
            icon=icon,
        ),
    }


def _flow(space, address: int, nan: int | None = 0xFFFF) -> dict:
    return {
        "space": space,
        "address": address,
        "nan": nan,
        "schema": sensor.sensor_schema(
            ThesslaSensor,
            unit_of_measurement=UNIT_CUBIC_METER_PER_HOUR,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_VOLUME_FLOW_RATE,
            state_class=STATE_CLASS_MEASUREMENT,
            icon="mdi:weather-windy",
        ),
    }


def _percent(space, address: int, icon: str = "mdi:fan") -> dict:
    return {
        "space": space,
        "address": address,
        "schema": sensor.sensor_schema(
            ThesslaSensor,
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            icon=icon,
        ),
    }


def _fan_voltage(address: int) -> dict:
    # 0-10 V fan control signal ("PWM"), 12-bit DAC: raw x 10/4095 V.
    return {
        "space": HOLDING,
        "address": address,
        "scale": 0.00244,
        "schema": sensor.sensor_schema(
            ThesslaSensor,
            unit_of_measurement=UNIT_VOLT,
            accuracy_decimals=2,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
            icon="mdi:sine-wave",
        ),
    }


SENSORS = {
    # Temperatures (input registers, signed tenths of a degree)
    "outside_temperature": _temperature(0x0010, "mdi:home-thermometer-outline"),  # TZ1
    "supply_temperature": _temperature(0x0011),  # TN1
    "extract_temperature": _temperature(0x0012),  # TP
    "fpx_temperature": _temperature(0x0013),  # TZ2
    "duct_temperature": _temperature(0x0014),  # TN2 (duct heater/cooler)
    "gwc_temperature": _temperature(0x0015),  # TZ3 (ground heat exchanger)
    "ambient_temperature": _temperature(0x0016),  # TO
    # Airflow
    "supply_flow": _flow(HOLDING, 0x0100),  # measured
    "extract_flow": _flow(HOLDING, 0x0101),
    "supply_target_flow": _flow(INPUT, 0x0112, None),  # Constant Flow setpoint
    "extract_target_flow": _flow(INPUT, 0x0113, None),
    "supply_percentage": _percent(INPUT, 0x0110),
    # Fan control voltage. With Constant Flow the unit raises it to hold the airflow, so a rising
    # supply voltage at unchanged flow means growing resistance on the intake side (e.g. frost).
    "supply_fan_voltage": _fan_voltage(0x0500),
    "extract_fan_voltage": _fan_voltage(0x0501),
    "extract_percentage": _percent(INPUT, 0x0111),
    # Filter wear shown on the panel's main screen (fw >= 4.75)
    "filter_supply_wear": _percent(HOLDING, 0x1182, "mdi:air-filter"),
    "filter_extract_wear": _percent(HOLDING, 0x1183, "mdi:air-filter"),
    # Number of the S alarm that stopped the unit (0 = none)
    "alarm_s_code": {
        "space": HOLDING,
        "address": 0x1120,
        "schema": sensor.sensor_schema(ThesslaSensor, accuracy_decimals=0, icon="mdi:alert-octagon",
                                       entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    },
    # Ventilation intensity actually in force, whatever the mode (what the panel shows big).
    "intensity": {
        "kind": "CURRENT_INTENSITY",
        "requests": [
            (HOLDING, REG_MODE),
            (HOLDING, REG_MANUAL_INTENSITY),
            (HOLDING, REG_TEMP_INTENSITY),
            (INPUT, REG_SUPPLY_PERCENT),
        ],
        "schema": sensor.sensor_schema(
            ThesslaSensor,
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            icon="mdi:fan",
        ),
    },
}

CONFIG_SCHEMA = THESSLA_CHILD_SCHEMA.extend(
    {cv.Optional(key): spec["schema"] for key, spec in SENSORS.items()}
)


async def to_code(config):
    for key, spec in SENSORS.items():
        if key not in config:
            continue
        conf = config[key]
        var = await sensor.new_sensor(conf)
        if "kind" in spec:
            await bind_entity(var, config)
            cg.add(var.set_kind(getattr(SensorKind, spec["kind"])))
            for space, address in spec["requests"]:
                await request(config, space, address)
            continue
        await bind_entity(var, config, spec["space"], spec["address"])
        if "scale" in spec:
            cg.add(var.set_scale(spec["scale"]))
        if spec.get("signed"):
            cg.add(var.set_signed(True))
        if spec.get("nan") is not None:
            cg.add(var.set_nan_raw(spec["nan"]))
