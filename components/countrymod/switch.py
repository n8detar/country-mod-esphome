import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv

from .climate import CountrymodClimate, countrymod_ns
from .const import (
    CONF_COUNTRYMOD_ID,
    CONF_TYPE,
    TYPE_FEATURE,
    TYPE_NIGHT,
    TYPE_TURBO,
)

CountrymodSwitch = countrymod_ns.class_(
    "CountrymodSwitch", switch.Switch, cg.Parented.template(CountrymodClimate)
)
CountrymodSwitchKind = countrymod_ns.enum("CountrymodSwitchKind")

SWITCH_TYPES = {
    TYPE_TURBO: CountrymodSwitchKind.COUNTRYMOD_SWITCH_TURBO,
    TYPE_NIGHT: CountrymodSwitchKind.COUNTRYMOD_SWITCH_NIGHT,
    TYPE_FEATURE: CountrymodSwitchKind.COUNTRYMOD_SWITCH_FEATURE,
}

CONFIG_SCHEMA = switch.switch_schema(
    CountrymodSwitch,
    block_inverted=True,
    default_restore_mode="DISABLED",
).extend(
    {
        cv.GenerateID(CONF_COUNTRYMOD_ID): cv.use_id(CountrymodClimate),
        cv.Required(CONF_TYPE): cv.enum(SWITCH_TYPES, lower=True),
    }
)


async def to_code(config):
    var = await switch.new_switch(config, SWITCH_TYPES[config[CONF_TYPE]])
    parent = await cg.get_variable(config[CONF_COUNTRYMOD_ID])
    await cg.register_parented(var, parent)

    setter = {
        TYPE_TURBO: "set_turbo_switch",
        TYPE_NIGHT: "set_night_switch",
        TYPE_FEATURE: "set_feature_switch",
    }[config[CONF_TYPE]]
    cg.add(getattr(parent, setter)(var))
