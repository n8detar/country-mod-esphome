import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv

from .climate import CountrymodClimate, countrymod_ns
from .const import CONF_COUNTRYMOD_ID

CountrymodModeSelect = countrymod_ns.class_(
    "CountrymodModeSelect", select.Select, cg.Parented.template(CountrymodClimate)
)

COUNTRYMOD_MODE_OPTIONS = ["Auto", "Eco", "Turbo"]

CONFIG_SCHEMA = select.select_schema(CountrymodModeSelect).extend(
    {
        cv.GenerateID(CONF_COUNTRYMOD_ID): cv.use_id(CountrymodClimate),
    }
)


async def to_code(config):
    var = await select.new_select(config, options=COUNTRYMOD_MODE_OPTIONS)
    parent = await cg.get_variable(config[CONF_COUNTRYMOD_ID])
    await cg.register_parented(var, parent)
    cg.add(parent.set_mode_select(var))
