export const safetyFields = [
  'capacity_mah',
  'cold_allowance_deci_c',
  'charge_derate_mv',
  'charge_stop_mv',
  'discharge_derate_mv',
  'discharge_stop_mv',
  'cell_ov_mv',
  'cell_uv_mv',
  'cell_ot_deci_c',
  'charge_max_ma',
  'discharge_max_ma',
  'recovery_permille_per_s',
  'cold_stop_deci_c',
  'cold_recover_deci_c',
  'cold_t0_deci_c',
  'cold_c0_permille',
  'cold_t1_deci_c',
  'cold_c1_permille',
  'cold_t2_deci_c',
  'cold_c2_permille',
  'cold_t3_deci_c',
  'cold_c3_permille',
  'hot_start_deci_c',
  'hot_stop_deci_c',
  'hot_recover_deci_c',
  'hot_c_permille',
  'zero_tolerance_ma',
  'limit_margin_ma',
  'limit_margin_permille',
  'violation_dwell_ms',
  'bal_min_mv',
  'bal_start_delta_mv',
  'bal_stop_delta_mv',
  'bal_min_ma',
  'bal_max_c_permille',
  'bal_qualify_ms',
  'bal_pulse_ms',
  'bal_max_cells',
  'communication_timeout_ms',
  'firmware_version',
  'configuration_valid',
  'cell_ov_programmed_uv',
  'cell_uv_programmed_uv',
] as const
export type SafetyPolicy = Record<typeof safetyFields[number], number>

export function decodeSafetyPolicy(bytes: unknown): SafetyPolicy | null {
  if (!Array.isArray(bytes) || bytes.length !== 2 + safetyFields.length * 4 || bytes[0] !== 1 || bytes[1] !== safetyFields.length ||
      bytes.some(value => !Number.isInteger(value) || value < 0 || value > 255)) return null
  const values = Object.fromEntries(safetyFields.map((name, index) => {
    const offset = 2 + index * 4
    return [name, bytes[offset] | (bytes[offset+1] << 8) | (bytes[offset+2] << 16) | (bytes[offset+3] << 24)]
  })) as SafetyPolicy
  if (values.capacity_mah <= 0 || values.cold_allowance_deci_c < 0 || values.cold_allowance_deci_c > 100 ||
      ![0, 1].includes(values.configuration_valid)) return null
  return values
}
