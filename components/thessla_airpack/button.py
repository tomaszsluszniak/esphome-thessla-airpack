import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_CONFIG

from . import HOLDING, THESSLA_CHILD_SCHEMA, bind_entity, thessla_ns

DEPENDENCIES = ["thessla_airpack"]

ThesslaButton = thessla_ns.class_("ThesslaButton", button.Button)
ButtonKind = thessla_ns.enum("ButtonKind", is_class=True)

# key: (kind, icon, register, command value)
BUTTONS = {
    "sync_clock": ("SYNC_CLOCK", "mdi:clock-check-outline", None, None),
    # AirPack4 without AFC: record a filter replacement, reset the alarm and set the next date.
    "filter_supply_replaced": ("WRITE", "mdi:air-filter", 0x1FFF, 0x11),
    "filter_extract_replaced": ("WRITE", "mdi:air-filter", 0x1FFF, 0x21),
}

CONFIG_SCHEMA = THESSLA_CHILD_SCHEMA.extend(
    {
        cv.Optional(key): button.button_schema(
            ThesslaButton, icon=icon, entity_category=ENTITY_CATEGORY_CONFIG
        )
        for key, (_, icon, _, _) in BUTTONS.items()
    }
)


async def to_code(config):
    for key, (kind, _, address, value) in BUTTONS.items():
        if key not in config:
            continue
        var = await button.new_button(config[key])
        cg.add(var.set_kind(getattr(ButtonKind, kind)))
        if address is None:
            await bind_entity(var, config)
        else:
            # Bound for writing only; the register is not polled for this entity.
            await bind_entity(var, config)
            cg.add(var.set_register(HOLDING, address))
            cg.add(var.set_value(value))
