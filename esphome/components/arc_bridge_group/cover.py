import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import cover
from esphome.const import CONF_ID

from . import arc_bridge_group_ns
from ..arc_bridge.cover import ARCCover

AUTO_LOAD = ["cover"]
DEPENDENCIES = ["arc_bridge"]

CONF_MEMBERS = "members"

ARCBridgeGroupCover = arc_bridge_group_ns.class_("ARCBridgeGroupCover", cover.Cover, cg.Component)


def validate_members(value):
    value = cv.ensure_list(cv.use_id(ARCCover))(value)
    if not value:
        raise cv.Invalid("members must contain at least one arc_bridge cover id")
    if len(value) != len(set(value)):
        raise cv.Invalid("members must not contain duplicate arc_bridge cover ids")
    return value


CONFIG_SCHEMA = (
    cover.cover_schema(ARCBridgeGroupCover)
    .extend(
        {
            cv.Required(CONF_MEMBERS): validate_members,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cover.register_cover(var, config)
    await cg.register_component(var, config)

    for member_id in config[CONF_MEMBERS]:
        member = await cg.get_variable(member_id)
        cg.add(var.add_member(member))
