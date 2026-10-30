"""Thessla Green AirPack over Modbus RTU (RS485) for ESPHome.

Register map: docs/rejestry.md. Addresses are zero-based wire addresses.
"""

import logging

import esphome.codegen as cg
from esphome.components import modbus, time
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.const import CONF_ID, CONF_TIME_ID, CONF_UPDATE_INTERVAL
from esphome.types import ConfigType

_LOGGER = logging.getLogger(__name__)

CODEOWNERS = ["@tomaszsluszniak"]
DEPENDENCIES = ["modbus"]
AUTO_LOAD = ["modbus"]
MULTI_CONF = True

CONF_THESSLA_AIRPACK_ID = "thessla_airpack_id"
CONF_NORMAL_INTERVAL = "normal_interval"
CONF_SLOW_INTERVAL = "slow_interval"

thessla_ns = cg.esphome_ns.namespace("thessla_airpack")
ThesslaAirpack = thessla_ns.class_(
    "ThesslaAirpack", cg.PollingComponent, modbus.ModbusClientDevice
)
EntityType = modbus.modbus_ns.enum("EntityType", is_class=True)

# Register spaces as used in the definitions of each platform.
COIL = EntityType.COIL
DISCRETE = EntityType.DISCRETE_INPUT
HOLDING = EntityType.HOLDING
INPUT = EntityType.INPUT_REGISTER

# Registers read by the controller logic for the "smart" entities.
REG_SUPPLY_PERCENT = 0x0110
REG_MODE = 0x1070
REG_MANUAL_INTENSITY = 0x1072
REG_TEMP_INTENSITY = 0x1073
REG_MANUAL_TEMPERATURE = 0x1074
REG_TEMP_TEMPERATURE = 0x1075

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ThesslaAirpack),
            cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
            # update_interval = mode, intensities, airflow, functions, alarms.
            # Temperatures and the clock follow normal_interval, configuration slow_interval.
            cv.Optional(CONF_NORMAL_INTERVAL, default="10s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_SLOW_INTERVAL, default="60s"): cv.positive_time_period_milliseconds,
        }
    )
    .extend(cv.polling_component_schema("2s"))
    # The controller ships with unit address 10.
    .extend(modbus.modbus_device_schema(10))
)

THESSLA_CHILD_SCHEMA = cv.Schema(
    {cv.GenerateID(CONF_THESSLA_AIRPACK_ID): cv.use_id(ThesslaAirpack)}
)


def _final_validate(config: ConfigType) -> None:
    modbus.final_validate_modbus_device("thessla_airpack", role="client")(config)
    # ESPHome's modbus hub defaults to a 600 ms pause before every request, which makes one poll of
    # this unit take 10+ seconds. The controller needs only a few milliseconds.
    full = fv.full_config.get()
    for hub in full.get("modbus", []):
        if hub.get(CONF_ID) != config[modbus.CONF_MODBUS_ID]:
            continue
        turnaround = hub.get("turnaround_time")
        if turnaround is not None and turnaround.total_milliseconds > 100:
            _LOGGER.warning(
                "modbus '%s': turnaround_time is %s ms, so every request waits that long and values "
                "refresh slowly. Add 'turnaround_time: 20ms' to the modbus: section.",
                hub[CONF_ID], turnaround.total_milliseconds,
            )


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await modbus.register_modbus_client_device(var, config)
    fast_ms = max(1, config[CONF_UPDATE_INTERVAL].total_milliseconds)
    cg.add(var.set_normal_every(max(1, round(config[CONF_NORMAL_INTERVAL].total_milliseconds / fast_ms))))
    cg.add(var.set_slow_every(max(1, round(config[CONF_SLOW_INTERVAL].total_milliseconds / fast_ms))))
    if CONF_TIME_ID in config:
        clock = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time(clock))


async def bind_entity(var, config: ConfigType, space=None, address=None, count=1):
    """Attach an entity to the hub and make the hub poll its register."""
    parent = await cg.get_variable(config[CONF_THESSLA_AIRPACK_ID])
    cg.add(var.set_parent(parent))
    cg.add(parent.add_entity(var))
    if space is not None:
        cg.add(var.set_register(space, address))
        cg.add(parent.request(space, address, count))
    return parent


async def request(config: ConfigType, space, address: int, count: int = 1, atomic: bool = False) -> None:
    parent = await cg.get_variable(config[CONF_THESSLA_AIRPACK_ID])
    if atomic:
        cg.add(parent.request_atomic(space, address, count))
    else:
        cg.add(parent.request(space, address, count))
