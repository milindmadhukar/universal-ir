import esphome.codegen as cg
from esphome.components import climate_ir

AUTO_LOAD = ["climate_ir"]
CODEOWNERS = ["@milind"]

godrej_ac_ns = cg.esphome_ns.namespace("godrej_ac")
GodrejAC = godrej_ac_ns.class_("GodrejAC", climate_ir.ClimateIR)

CONFIG_SCHEMA = climate_ir.climate_ir_with_receiver_schema(GodrejAC)


async def to_code(config):
    await climate_ir.new_climate_ir(config)
