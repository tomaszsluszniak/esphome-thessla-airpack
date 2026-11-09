import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

from . import HOLDING, INPUT, THESSLA_CHILD_SCHEMA, bind_entity, request, thessla_ns

DEPENDENCIES = ["thessla_airpack"]

ThesslaTextSensor = thessla_ns.class_("ThesslaTextSensor", text_sensor.TextSensor)
TextKind = thessla_ns.enum("TextKind", is_class=True)

SPECIAL_FUNCTIONS = {
    0: "Brak",
    1: "Okap",
    2: "Kominek",
    3: "Wietrzenie (przycisk)",
    4: "Wietrzenie (przełącznik)",
    5: "Wietrzenie (higrostat)",
    6: "Wietrzenie (jakość powietrza)",
    7: "Wietrzenie",
    8: "Wietrzenie (harmonogram AUTO)",
    9: "Wietrzenie (harmonogram MANUAL)",
    10: "Otwarte okna",
    11: "Pusty dom",
}
THERMAL_STATE = {0: "Nieaktywny", 1: "Grzanie", 2: "Chłodzenie"}


def _enum(space, address, options: dict, icon: str, category=None) -> dict:
    kwargs = {"icon": icon}
    if category:
        kwargs["entity_category"] = category
    return {
        "kind": "ENUM",
        "space": space,
        "address": address,
        "options": options,
        "schema": text_sensor.text_sensor_schema(ThesslaTextSensor, **kwargs),
    }


def _special(kind: str, requests: list, icon: str) -> dict:
    return {
        "kind": kind,
        "requests": requests,
        "schema": text_sensor.text_sensor_schema(
            ThesslaTextSensor, icon=icon, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
    }


SCHEDULE_START, SCHEDULE_SETTING, SCHEDULE_AIRING, SEASON_STRIDE = 0x0010, 0x0048, 0x0080, 28


def _schedule(season: int) -> dict:
    offset = season * SEASON_STRIDE
    spec = _special(
        "SCHEDULE",
        [(HOLDING, SCHEDULE_START + offset, 28), (HOLDING, SCHEDULE_SETTING + offset, 28)]
        + [(HOLDING, SCHEDULE_AIRING + offset + day * 4, 1) for day in range(7)],
        "mdi:calendar-clock",
    )
    spec["season"] = season
    return spec


TEXT_SENSORS = {
    "special_function": _enum(HOLDING, 0x1080, SPECIAL_FUNCTIONS, "mdi:star-four-points"),
    "bypass_status": _enum(HOLDING, 0x10EA, THERMAL_STATE, "mdi:valve"),
    "comfort_status": _enum(HOLDING, 0x10D1, THERMAL_STATE, "mdi:thermostat"),
    "gwc_status": _enum(HOLDING, 0x10A7, {0: "Nieaktywny", 1: "Zima", 2: "Lato"}, "mdi:earth"),
    "fpx_stage": _enum(HOLDING, 0x1066, {0: "Wyłączony", 1: "FPX1", 2: "FPX2"}, "mdi:snowflake-melt"),
    "filter_type": _enum(HOLDING, 0x1FFF, {1: "Presostat", 2: "Filtry płaskie", 3: "CleanPad", 4: "CleanPad Pure"},
                         "mdi:air-filter", ENTITY_CATEGORY_DIAGNOSTIC),
    "filter_supply_date": {"kind": "PACKED_DATE", "space": HOLDING, "address": 0x1234,
                           "schema": text_sensor.text_sensor_schema(ThesslaTextSensor, icon="mdi:calendar-alert")},
    "filter_extract_date": {"kind": "PACKED_DATE", "space": HOLDING, "address": 0x1236,
                            "schema": text_sensor.text_sensor_schema(ThesslaTextSensor, icon="mdi:calendar-alert")},
    "firmware_version": _special(
        "FIRMWARE", [(INPUT, 0, 2), (INPUT, 4, 1)], "mdi:chip"
    ),
    "serial_number": _special("SERIAL_NUMBER", [(INPUT, 24, 6)], "mdi:identifier"),
    # The manufacturer requires all four clock registers in one read.
    "controller_clock": _special("CLOCK", [(HOLDING, 0, 4, "atomic")], "mdi:clock-outline"),
    # Weekly AUTO schedules as raw registers (read by the card): slot starts, slot settings, airing.
    "schedule_summer": _schedule(0),
    "schedule_winter": _schedule(1),
}

CONFIG_SCHEMA = THESSLA_CHILD_SCHEMA.extend(
    {cv.Optional(key): spec["schema"] for key, spec in TEXT_SENSORS.items()}
)


async def to_code(config):
    for key, spec in TEXT_SENSORS.items():
        if key not in config:
            continue
        var = await text_sensor.new_text_sensor(config[key])
        cg.add(var.set_kind(getattr(TextKind, spec["kind"])))
        if "season" in spec:
            cg.add(var.set_season(spec["season"]))
        if spec["kind"] == "PACKED_DATE":
            await bind_entity(var, config, spec["space"], spec["address"])
        elif spec["kind"] == "ENUM":
            await bind_entity(var, config, spec["space"], spec["address"])
            for code, label in spec["options"].items():
                cg.add(var.add_option(code, label))
        else:
            await bind_entity(var, config)
            for space, address, count, *flags in spec["requests"]:
                await request(config, space, address, count, atomic="atomic" in flags)
