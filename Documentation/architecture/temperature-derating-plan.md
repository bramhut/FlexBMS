# Temperature derating and inverter-limit enforcement

Decision record: 2026-10-06. Cells: EVE MB31, 314 Ah.

## Status and scope

- IC thermal-fault mask fix implemented and host-tested; STM32 build passed.
  No new software IC-temperature cutoff is planned. Hardware TSD stays unchanged.
- Cold-charge policy, hot charge/discharge derating and independent current-limit
  enforcement implemented. Software validation is described below; hardware
  response and busbar-to-cell allowance still require validation.
- No firmware release/version bump, bundle or flash is part of this plan unless
  separately requested. The existing v0.1.73 bundle does not include these changes.

## Measurements and interpretation

NTCs are on busbars. The user observed a 3-4 degC drop in the first 5-10 minutes
after stopping approximately 40 A transfer, followed by much slower cooling.
This suggests local heating but does not establish a maximum busbar-to-cell
temperature error. Use the coldest NTC for cold limits and hottest for hot limits;
never use average temperature or monitor-IC temperature as cell temperature.

Source: EVE MB31 PBRI-MB31-D06-01, revision A, table 5/7, distributor-hosted copy:
<https://www.nkon.nl/novat/amfile/file/download/file/1063/product/5510/>.
The original table is in P-rate. User explicitly selected its numerical values
as C-rates instead. This is a project policy, not an exact conversion: at 3.65 V,
equal numerical C-rate permits 14.1% more power than P-rate based on 3.2 V.
The cold allowance does not compensate on flat-temperature portions of the curve.

## Approved cold-charge policy

Companion configuration: **Cold-temperature allowance**.

- Default 3.0 degC; range 0.0-10.0 degC; resolution 0.1 degC.
- Store as an integer in tenths of a degree in the existing BMS configuration.
- Persist with the existing save-and-reboot configuration workflow.
- Existing valid configurations must migrate with 3.0 degC, preserving all other
  settings; do not report NO_CONFIG or replace existing settings with defaults.
- Label as a cold-side safety allowance, not a calibrated sensor correction.
- Effective cold temperature = minimum busbar temperature minus allowance.
- Do not subtract the allowance for hot-temperature protection.

Cold charge curve (linear interpolation, scaled by configured Ah capacity):

| Effective temperature | Charge allowance |
| --- | --- |
| <=0 degC | Charge inhibited |
| Just above 0 degC | 0.05C curve endpoint |
| 5 degC | 0.12C |
| 10 degC | 0.30C |
| >=15 degC, before hot derating | 0.50C |

After a cold stop, require effective temperature strictly above 2 degC before
allowing charging again, then apply the curve and existing recovery slew. Boot,
monitor reinitialization and invalid measurements start in the inhibited state;
rebooting cannot bypass recovery hysteresis.

The existing fixed 5 degC hardware UT flag no longer controls charging: the
measured-temperature policy is authoritative for cold inhibition. The hardware
flag remains available in raw register diagnostics. The separate 60 degC cell
overtemperature latched shutdown is preserved.

At 314 Ah and a 3 degC allowance, the temperature limit crosses 40 A at a minimum
measured busbar temperature of **8.205237 degC**:
`3 + 5 + ((40/314 - 0.12)/(0.30 - 0.12))*5`.
At 8 degC it is 37.68 A, and at 7 degC it is 33.284 A. Actual output is also
subject to voltage limits, hysteresis, recovery slew and CAN rounding.

## Approved hot charge/discharge policy

Implemented policy for both directions:

- Use maximum measured busbar temperature directly.
- Up to 50 degC: 0.50C thermal allowance.
- 50-55 degC: linearly reduce the allowance from 0.50C to zero.
- At/above 55 degC: inhibit the affected directions (both for this shared curve).
- After hot stop, recover strictly below 53 degC through the curve and recovery slew.
- Boot, reinitialization and invalid measurements also require this recovery.
- Existing 60 degC latched HV shutdown remains independent.

This is intentionally earlier than EVE's 55-60 degC reduction; the user approved
this conservative curve.

At 314 Ah with the recommended curve, 40 A becomes thermally limited above
53.726115 degC; the slope starts at 50 degC but the 0.50C allowance is initially
higher than 40 A. Do not scale the curve to the fuse/system current ceiling.

No cold-discharge curve is added. The user states the installation never goes
below -5 degC. Cold charging is still restricted independently.

## Combining limits

Per direction, use the minimum of cell-temperature allowance, cell-voltage
allowance and configured system ceiling. Lower limits take effect immediately;
increases pass through the existing recovery slew. Keep charge/discharge voltage
targets, SoC integration, calibration qualification and balancing policy unchanged.
Invalid/stale temperature or current data must not grant increased permissions;
retain existing measurement/communication safe-off behavior.

## Approved independent inverter-limit enforcement

STM32 owns enforcement, using fresh measured pack current and the final
CAN-encoded directional current allowance, including its rounding. Do not use
display-deadband current. This does not require inverter acknowledgement.

| Violation | Trip condition |
| --- | --- |
| Zero charge allowance | Charging current >1 A continuously for 5 s |
| Nonzero charge allowance L | Charging current >L + max(2 A, 0.10*L) continuously for 5 s |
| Discharge | Same rules using discharge magnitude and discharge allowance |
| Existing absolute overcurrent | Keep existing independent 63 A protection without adding the 5 s dwell |

Each direction has its own timer. A changing limit does not reset an ongoing
violation timer while the violation persists. Reset on a valid compliant sample;
invalid samples cannot count as compliance or establish a new valid interval.
Only intervals between adjacent valid violating samples, at most 1 s apart,
accrue time; longer gaps preserve the accumulated dwell but add no time.
Measurement loss remains covered by the independent communication/freshness path.
Arming begins with the first generated RUN snapshot. Enforcement uses its final
quantized limits, without waiting for CAN acknowledgement. The 5 s dwell allows
normal inverter response; there is no additional startup grace period.

On violation: latch a distinct current-limit-violation fault, invoke existing HV
safe-off, clear run request, and require acknowledgement after resolution. Capture
direction, actual current and encoded allowance for Companion diagnostics. These
5 s/tolerance choices need GoodWe hardware validation; they are not cell ratings.

The new fault is BMS bit 11, `INVERTER_CURRENT_LIMIT`. The event stores signed
current in mA, allowed magnitude in 0.1 A, direction and STM32 uptime. It survives
fault acknowledgement in RAM, not an STM32 restart. It is carried in schema 2
of `GOODWE_CAN_DIAGNOSTICS` to Gateway and direct USB Companion. The existing
absolute-current protection still takes precedence if its threshold is exceeded.

## Implementation map

- `Software/Master/Inc/Peripherals/TemperatureLimits.h`: static C-rate curves,
  validation and recovery hysteresis, independent of the system current ceiling.
- `CurrentLimitGuard.h`: common CAN quantization and directional dwell logic.
- `Src/Peripherals/bcc/SlaveController.cpp`: temperature extrema, limit composition,
  recovery slew, fault dispatch and retained event.
- `RuntimeConfiguration.cpp`: configuration version 5, unchanged 32-byte dual-slot
  record; cold allowance in bits 16-23 of the existing reserved word. Versions
  2-4 default to 30 deci-degrees and keep their existing migration semantics.
- `BmsUart.cpp`, Gateway `Protocol.cpp` / `GatewayApi.cpp`: service and diagnostics
  wire contracts; legacy requests preserve existing allowance or default 3 degC.
- Companion `ConfigurationView.vue` and `DiagnosticsView.vue`: setting and last
  violation detail. No self-discharge, SoC or balancing correction is added here.

## Implementation and validation checklist

- [x] Confirm hot breakpoints and implement pure thermal/hysteresis logic.
- [x] Versioned storage, STM32/Gateway services and Companion configuration.
- [x] Integrate thermal allowances before recovery slew and inverter encoding.
- [x] Directional enforcement, fault dispatch and retained diagnostic event.
- [x] Align fixed cold inhibit; retain hard cell/IC/absolute-current protections.
- [x] Host tests: temperature knots, allowance endpoints, mixed hot/cold sensors,
  invalid readings, reboot hysteresis, recovery, capacity scaling and 40 A crossings.
- [x] Host tests use the actual configuration parser/writer with emulated flash:
  versions 2-5, migration, CRC corruption, slot fallback and allowance endpoints.
- [x] Browser tests: legacy/current configuration and diagnostic wire payloads.
- [x] Gateway codec host tests: signed event current, malformed length/direction,
  legacy schema. Gateway API startup self-check covers allowance endpoints and
  rejects excessive/fractional values; running that self-check awaits hardware.
- [x] Host guard tests: equality, changing limits, zero/nonzero limits, direction,
  elapsed-time rollover, invalid samples and recovery slew.
- [x] Actual FaultManager host test: new bit 11 invokes safe-off once, denies
  acknowledgement while active, retains the latch after clearing and requires
  explicit acknowledgement. PCC GPIO/contactors are stubbed, not hardware-tested.
- [x] Final STM32 and Gateway builds passed; Companion type check and all 43
  tests passed. Relevant host tests passed with strict compiler warnings;
  thermal/guard and FaultManager tests also passed ASan/UBSan. Scoped source/docs
  `git diff --check` passed (unrelated generated PDF excluded).
- [ ] Hardware validation: sensor offset, inverter response/overshoot, limit
  reporting and safe-off. Do not claim hardware validation from host tests.

Verification recorded 2026-10-06. STM32 uses 50,816 bytes RAM and 137,940 bytes
flash; Gateway uses 43,164 bytes RAM and 1,347,230 bytes flash. Existing STM32
unused-function, libc syscall-stub and RWX linker warnings remain. No release
version increment, update bundle, upload or flash was performed.

### Reproducing the focused host checks

Run in Linux/WSL from the repository root, with g++ supporting C++20. The
configuration test maps emulated flash at the MCU address and is Linux-specific.
The stub include directories are test-only; never add them to firmware builds.

```sh
g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -ISoftware/Master/Inc Software/Master/tests/temperature_limits_test.cpp -o /tmp/flexbms-temperature-test
/tmp/flexbms-temperature-test
g++ -std=c++20 -Wall -Wextra -Werror -ISoftware/Master/tests/hal_stub -ISoftware/Master/Inc -ISoftware/Master/Inc/Peripherals Software/Master/tests/runtime_configuration_test.cpp -o /tmp/flexbms-config-test
/tmp/flexbms-config-test
g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -ISoftware/Master/tests/fault_stub -ISoftware/Master/Inc -ISoftware/Master/Inc/Peripherals Software/Master/tests/current_limit_fault_test.cpp Software/Master/Src/Peripherals/FaultManager.cpp -o /tmp/flexbms-fault-test
/tmp/flexbms-fault-test
g++ -std=c++20 -Wall -Wextra -Werror -ISoftware/Gateway/components/uart_v1/include Software/Gateway/tests/pack_schema_test.cpp Software/Gateway/components/uart_v1/src/Protocol.cpp -o /tmp/flexbms-codec-test
/tmp/flexbms-codec-test
```

Run `npm.cmd run check` from `Software/Companion`. Run `platformio.exe run`
separately from `Software/Master` and `Software/Gateway`, setting
`PYTHONIOENCODING=utf-8` for the Gateway build. Neither command flashes hardware.

## Read-only visibility follow-up (2026-10-06)

The policy is exposed in Companion's Safety limits diagnostic subtab through
GET_SAFETY_LIMITS. Thermal knots/recovery values, current violation margins/dwell
and balancing qualification/pulse timings are shared with enforcement rather
than separately reproduced in the reporter. Hard voltage limits display nominal
settings and quantized programmed thresholds. This follow-up does not change
the agreed protection envelope. The requested next release is 0.1.74 and also
includes the previously unreleased thermal/enforcement work described above.

Release 0.1.74 is built: both firmware targets, the portable Companion, all 46
Companion tests and focused firmware host checks passed. Bundle payloads match
the release/build binaries; CRC32, release SHA-256 hashes and factory-image app
placement were verified. The bundle is 1,501,665 bytes. No hardware was flashed;
on-target protection validation remains outstanding.
