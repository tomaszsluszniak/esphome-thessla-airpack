import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import DEVICE_CLASS_SWITCH, ENTITY_CATEGORY_CONFIG

from . import HOLDING, THESSLA_CHILD_SCHEMA, bind_entity, thessla_ns

DEPENDENCIES = ["thessla_airpack"]

ThesslaSwitch = thessla_ns.class_("ThesslaSwitch", switch.Switch)

SWITCHES = {
    # address, register is a "disabled" flag, icon, category
    "power": (0x1123, False, "mdi:power", None),
    "bypass_enabled": (0x10E0, True, "mdi:valve", ENTITY_CATEGORY_CONFIG),
    "gwc_enabled": (0x10A0, True, "mdi:earth", ENTITY_CATEGORY_CONFIG),
}


def _schema(icon, category):
    kwargs = {"icon": icon, "device_class": DEVICE_CLASS_SWITCH, "block_inverted": True}
    if category:
        kwargs["entity_category"] = category
    return switch.switch_schema(ThesslaSwitch, **kwargs)


CONFIG_SCHEMA = THESSLA_CHILD_SCHEMA.extend(
    {
        cv.Optional(key): _schema(icon, category)
        for key, (_, _, icon, category) in SWITCHES.items()
    }
)


async def to_code(config):
    for key, (address, disable_flag, _, _) in SWITCHES.items():
        if key not in config:
            continue
        var = await switch.new_switch(config[key])
        await bind_entity(var, config, HOLDING, address)
        if disable_flag:
            cg.add(var.set_disable_flag(True))
