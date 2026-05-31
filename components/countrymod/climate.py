import esphome.codegen as cg
from esphome.components import climate_ir
import esphome.config_validation as cv

AUTO_LOAD = ["button", "climate_ir", "switch"]

countrymod_ns = cg.esphome_ns.namespace("countrymod")
CountrymodClimate = countrymod_ns.class_("CountrymodClimate", climate_ir.ClimateIR)

CONF_FEATURE_AS_SWING = "feature_as_swing"
CONF_INTER_FRAME_DELAY = "inter_frame_delay"
CONF_USE_POWER_BIT = "use_power_bit"

CONFIG_SCHEMA = climate_ir.climate_ir_with_receiver_schema(CountrymodClimate).extend(
    {
        cv.Optional(CONF_FEATURE_AS_SWING, default=False): cv.boolean,
        cv.Optional(CONF_INTER_FRAME_DELAY, default="110ms"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_USE_POWER_BIT, default=True): cv.boolean,
    }
)


async def to_code(config):
    var = await climate_ir.new_climate_ir(config)

    cg.add(var.set_feature_as_swing(config[CONF_FEATURE_AS_SWING]))
    cg.add(var.set_inter_frame_delay(config[CONF_INTER_FRAME_DELAY]))
    cg.add(var.set_use_power_bit(config[CONF_USE_POWER_BIT]))
