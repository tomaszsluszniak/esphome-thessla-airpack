from esphome.components import text
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_CONFIG

from . import THESSLA_CHILD_SCHEMA, bind_entity, thessla_ns

DEPENDENCIES = ["thessla_airpack"]

ThesslaScheduleCommand = thessla_ns.class_("ThesslaScheduleCommand", text.Text)

CONF_SCHEDULE_COMMAND = "schedule_command"

CONFIG_SCHEMA = THESSLA_CHILD_SCHEMA.extend(
    {
        # Writes the weekly schedule (used by the card); format in ThesslaAirpack::set_schedule.
        cv.Optional(CONF_SCHEDULE_COMMAND): text.text_schema(
            ThesslaScheduleCommand,
            icon="mdi:calendar-edit",
            entity_category=ENTITY_CATEGORY_CONFIG,
            mode="TEXT",
        ),
    }
)


async def to_code(config):
    if CONF_SCHEDULE_COMMAND in config:
        var = await text.new_text(config[CONF_SCHEDULE_COMMAND], max_length=120)
        await bind_entity(var, config)
