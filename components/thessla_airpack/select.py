import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_CONFIG

from . import (
    HOLDING,
    REG_MANUAL_INTENSITY,
    REG_MODE,
    REG_TEMP_INTENSITY,
    THESSLA_CHILD_SCHEMA,
    bind_entity,
    request,
    thessla_ns,
)
from .text_sensor import SPECIAL_FUNCTIONS

DEPENDENCIES = ["thessla_airpack"]

ThesslaSelect = thessla_ns.class_("ThesslaSelect", select.Select)
SelectKind = thessla_ns.enum("SelectKind", is_class=True)

# Special functions the user can start (the rest are triggered by the unit's inputs).
SETTABLE_SPECIAL = {0, 2, 7, 10, 11}  # per the manufacturer table; OKAP only via its input


def _select(address, options: dict, icon: str, writable=None, kind="DIRECT", category=None) -> dict:
    kwargs = {"icon": icon}
    if category:
        kwargs["entity_category"] = category
    return {
        "address": address,
        "options": options,
        "writable": set(options) if writable is None else writable,
        "kind": kind,
        "schema": select.select_schema(ThesslaSelect, **kwargs),
    }


SELECTS = {
    "operating_mode": _select(
        REG_MODE,
        {0: "Automatyczny", 1: "Manualny", 2: "Chwilowy"},
        "mdi:cog-play",
        kind="OPERATING_MODE",
    ),
    "season": _select(0x1071, {0: "Lato", 1: "Zima"}, "mdi:sun-snowflake-variant"),
    "special_function": _select(
        0x1080, SPECIAL_FUNCTIONS, "mdi:star-four-points", writable=SETTABLE_SPECIAL
    ),
    "comfort_mode": _select(0x10D0, {0: "EKO", 1: "KOMFORT"}, "mdi:leaf"),
    "bypass_mode": _select(
        0x10EB,
        {1: "Tryb 1", 2: "Tryb 2", 3: "Tryb 3"},
        "mdi:valve",
        category=ENTITY_CATEGORY_CONFIG,
    ),
    "erv_mode": _select(
        0x1267, {0: "Wyłączona", 1: "Tryb 1", 2: "Tryb 2"}, "mdi:radiator", category=ENTITY_CATEGORY_CONFIG
    ),
}

CONFIG_SCHEMA = THESSLA_CHILD_SCHEMA.extend(
    {cv.Optional(key): spec["schema"] for key, spec in SELECTS.items()}
)


async def to_code(config):
    for key, spec in SELECTS.items():
        if key not in config:
            continue
        options = spec["options"]
        var = await select.new_select(config[key], options=list(options.values()))
        await bind_entity(var, config, HOLDING, spec["address"])
        cg.add(var.set_kind(getattr(SelectKind, spec["kind"])))
        for code, label in options.items():
            cg.add(var.add_option(code, label, code in spec["writable"]))
        if spec["kind"] == "OPERATING_MODE":
            await request(config, HOLDING, REG_MANUAL_INTENSITY)
            await request(config, HOLDING, REG_TEMP_INTENSITY)
