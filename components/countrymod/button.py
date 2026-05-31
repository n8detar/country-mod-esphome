import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv

from .climate import CountrymodClimate, countrymod_ns
from .const import CONF_COUNTRYMOD_ID, CONF_TYPE, TYPE_DISPLAY, TYPE_LIGHT, TYPE_VIEW_VOLTAGE, TYPE_ZIGZAG

CountrymodButton = countrymod_ns.class_(
    "CountrymodButton", button.Button, cg.Parented.template(CountrymodClimate)
)
CountrymodButtonKind = countrymod_ns.enum("CountrymodButtonKind")

BUTTON_TYPES = {
    TYPE_DISPLAY: CountrymodButtonKind.COUNTRYMOD_BUTTON_DISPLAY,
    TYPE_LIGHT: CountrymodButtonKind.COUNTRYMOD_BUTTON_LIGHT,
    TYPE_VIEW_VOLTAGE: CountrymodButtonKind.COUNTRYMOD_BUTTON_VIEW_VOLTAGE,
    TYPE_ZIGZAG: CountrymodButtonKind.COUNTRYMOD_BUTTON_VIEW_VOLTAGE,
}

CONFIG_SCHEMA = button.button_schema(CountrymodButton).extend(
    {
        cv.GenerateID(CONF_COUNTRYMOD_ID): cv.use_id(CountrymodClimate),
        cv.Required(CONF_TYPE): cv.enum(BUTTON_TYPES, lower=True),
    }
)


async def to_code(config):
    var = await button.new_button(config, BUTTON_TYPES[config[CONF_TYPE]])
    parent = await cg.get_variable(config[CONF_COUNTRYMOD_ID])
    await cg.register_parented(var, parent)
