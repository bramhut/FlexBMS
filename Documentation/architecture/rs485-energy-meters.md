# Optional RS485 energy meters

The Gateway can read zero, one, or two Eastron **SDM72D-M-2** meters over the
existing isolated RS485 interface. Both slots are disabled by default. This is
a Gateway-local, read-only extension for Home Assistant reporting. Home
Assistant owns inverter power regulation through its existing inverter
integration. The STM32 safety controller, CAN link, and UART v1 protocol do not
participate in meter polling or configuration.

## Configuration and ownership

Companion **Configuration > RS485 energy meters** provides:

| Setting | Scope | Default / validation |
|---|---|---|
| Enable | Each of two fixed slots | Off; either slot can be used independently. |
| Name in Home Assistant | Per slot | Meter 1 / Meter 2; enabled names must be nonempty, at most 48 UTF-8 bytes, without control characters. |
| Modbus address | Per slot | 1 / 2; 1–247; enabled addresses must differ. |
| Reverse power direction | Per slot | Off; negate phase and total active power, exchange import/export counters, retain positive current magnitudes. |
| Baud rate | Shared bus | 9600; 1200, 2400, 4800, 9600, or 19200. |
| Parity / stop bits | Shared, Advanced | None / 1; none, even, or odd; 1 or 2 stop bits; always 8 data bits. |

Each slot initially shows just its enable checkbox. Enabling it reveals the
name, address and reverse-direction settings; switching it off hides those
fields while retaining their values in the draft. Slots appear side by side
on wider screens and stack on smaller screens. Shared serial settings are
shown when at least one slot is enabled, with parity/stop bits in the Advanced
disclosure. Save and Discard are enabled only while the draft has changes.
Live readings group power, phase currents and energy by meter; a Diagnostics
disclosure contains read age, failure count and identity. Read errors and
availability remain visible without expanding diagnostics.

Configure the addresses and serial settings on the physical meters manually; the Gateway
never writes meter registers. Settings are persisted in a versioned Gateway
NVS blob in the `energy_meters` namespace and applied without a BMS or Gateway
reboot. Invalid requests leave the saved configuration unchanged. Unsupported
or damaged stored settings fall back to both slots disabled and report
`stored_configuration_valid: false`.

Saving is permitted only on the trusted station LAN, with the setup/recovery
AP inactive and no firmware upload/install running. Direct USB Companion
cannot configure or read the Gateway's meters. Read-only status is visible
through the Gateway, including when the STM32 UART is unavailable. Names and
slave addresses can change without changing HA entity IDs: IDs belong to the
Gateway MAC identity and fixed meter slot. Choose the direction setting before
collecting HA energy statistics; changing it exchanges the sources of the two
cumulative energy entities.

## Hardware and polling

UART0 uses GPIO7 TX (`RS485_DI`), GPIO6 RX (`RS485_RO`), and GPIO0 RTS
(`RS485_EN`). The SP3485's linked DE and active-low RE are driven by ESP-IDF
RS485 half-duplex mode; GPIO0 is low when idle/disabled. UART1 on GPIO2/3 remains
the independent 1 Mbit/s STM32 link; USB Serial/JTAG remains the console. The
Master schematic has fitted R32 = 120 ohm termination and R31/R33 = 390 ohm
bias resistors. Check actual assembly and arrange termination at the two cable
ends, taking the Gateway termination into account. Connect both meters to one
bus with the Gateway as the only master and observe the manufacturers' signal
polarity and reference wiring.

The `energy_meters` component pins Espressif `esp-modbus` 2.1.4. Only RTU is
enabled; Modbus TCP/ASCII support is disabled in the Gateway build. With no
meters enabled at boot, no polling task, Modbus controller, or UART0 driver is
allocated. After first use, disabling both releases the controller/driver and
leaves the worker sleeping. Optional extension setup failure does not prevent
the BMS UART, networking, or firmware recovery from starting.

One worker owns the bus and reads the enabled slaves sequentially. Electrical
samples target one second per meter and energy counters target 30 seconds.
These are nominal periods, not simultaneous samples or a hard real-time
guarantee. Each request has a 500 ms response timeout. Failed electrical cycles
back off independently from one to five seconds; energy/identity failures
retry more slowly. The healthy meter continues to be serviced. A configuration
generation prevents an in-flight read from appearing under a changed name or
address. Both electrical requests must succeed and all decoded values must
validate before a complete electrical sample becomes visible.

| Function | Zero-based address | Registers | Values / cadence |
|---|---|---:|---|
| FC04 Read Input Registers | `0x0006` | 12 | L1/L2/L3 current (A), followed by L1/L2/L3 active power (W); 1 s. |
| FC04 | `0x0034` | 2 | Total signed active power (W); 1 s. |
| FC04 | `0x0048` | 4 | Total import and export active energy (kWh); 30 s. |
| FC03 Read Holding Registers | `0xFC00` | 3 | Serial number (two registers) and meter/model code; once after configuration, retry on failure. |
| FC03 | `0xFC84` | 1 | Firmware version; once after configuration, retry on failure. |

These are **protocol offsets**, not 30001/40001-style register numbers.
Electrical/energy values are IEEE-754 32-bit floats with the most significant
word first. ESP-Modbus supplies host-endian 16-bit words; the extension joins
words explicitly before decoding. NaN, infinity, negative currents or energy,
and implausible electrical magnitudes are rejected. Identity reads are optional
and never gate electrical availability. The map is specific to SDM72D-M-2;
other SDM72 variants are not interchangeable. Part 3 of the
[Eastron SDM72D-M-2 User Manual V1.2](https://vairema.lt/wp-content/uploads/3-phase-eastron-sdm72d-m-2-user-manual-v1.2.pdf)
is the register reference; the
[Espressif serial master example](https://github.com/espressif/esp-modbus/tree/v2.1.4/examples/serial/mb_serial_master)
documents the controller integration.

## MQTT and freshness

For Gateway identity `flexbms_XXYYZZ`, each enabled slot gets a separate HA
device linked to the Gateway via `via_device`. Discovery is retained under
`homeassistant/device/flexbms_XXYYZZ_meter1/config` (or `meter2`). Disabling a
slot publishes an empty retained discovery record, including after reconnect
if it was disabled while the broker was unavailable. Discovery and current
fresh samples are restored on broker reconnect and HA birth.

The following topics are below `flexbms/flexbms_XXYYZZ/meter1/` or `meter2/`:

| Topic | Retained | Contents |
|---|---|---|
| `state` | No | `current_l1_a` … `current_l3_a`, `power_l1_w` … `power_l3_w`, `total_power_w`, `sample_age_ms`, `failed_reads`. |
| `energy` | No | `import_kwh`, `export_kwh`. |
| `availability` | Yes | `online` only for a complete electrical sample less than 3 s old; otherwise `offline`. |
| `energy_availability` | Yes | `online` only when the meter is available and energy is valid and less than 90 s old. |

All sensors also require Gateway last-will availability. Electrical entities
expire after 3 seconds without a published sample, energy entities after 90
seconds. State and energy publish only on a new successful sample (or fresh
reconnect replay), not periodically from an unchanged cache. Failed cycles
invalidate the corresponding readings immediately, preserve diagnostic age
and failure count, and never publish zero as a substitute. MQTT queueing is
nonblocking and bounded by an outbox backpressure check; when publication
cannot proceed, HA expiry continues to protect freshness.

The HA entities are three phase currents, three phase active powers, total
active power, import/export energy (`total_increasing`), and diagnostic
measurement age/failed reads. Companion additionally shows serial number,
model code, firmware version, last error, and last successful electrical age.

## Validation and commissioning

The native `Software/Gateway/tests/energy_meters_test.cpp` exercises float word
order, signed power, reverse direction, atomic invalid-value rejection,
configuration parsing, freshness boundaries, stable discovery IDs, per-meter
and energy availability, reconnect/HA birth, disabled discovery cleanup, and
failed MQTT queue retries. Compile it with `MeterJson.cpp`, `MeterPublisher.cpp`,
the component include directory, and the ESP-IDF cJSON sources/headers.
`npm run check` in `Software/Companion` includes the meter configuration
transport correlation/capability/disconnect test. A Gateway PlatformIO build
regenerates its embedded Companion; set `PYTHONIOENCODING=utf-8` on Windows.

Before using feeder readings for the HA regulation automation:

1. Set distinct physical slave addresses, match baud/parity/stop bits, and
   enable/name the desired slots in Companion. Verify settings survive reboot.
2. Compare each phase current, phase power, total power, and energy against the
   meter display and a known load. Check the feeder's import/export sign and
   direction option before collecting energy statistics.
3. Confirm both HA devices/entities are discovered and fresh electrical samples
   arrive at roughly one-second intervals during normal operation.
4. Disconnect each meter in turn. Confirm its entities become unavailable,
   no replacement zero is reported, and the other meter continues updating.
   Reconnect it and verify recovery. Check energy-only failure handling with a
   Modbus simulator if the physical meter cannot reproduce that fault.
5. Restart the broker/HA and the Gateway. Verify discovery, availability and
   current readings recover. Disable a slot with the broker offline, reconnect
   the broker, and verify its old discovery is removed.
6. Enable zero/one/two meters and verify the STM32 UART, browser, and MQTT BMS
   telemetry remain responsive. Hardware timing and electrical wiring require
   this bench check; software builds and native tests cannot establish them.

Software validation on 2026-10-06: Gateway PlatformIO build passed with the
regenerated Companion (application flash 1,415,386 / 1,572,864 bytes), all 47
Companion checks passed, and the native meter model/parser/publisher tests
passed. A local browser fixture verified zero/two-slot settings, preserved
unsaved names/direction during status refresh, duplicate-address rejection,
advanced serial options, successful save, and station-permission gating with
no browser errors. The system overview was rebuilt using Typst 0.15.1.
Physical-meter timing, wiring, persistence and HA commissioning remain to be
verified on the installed equipment; no firmware was flashed during this work.

The subsequent compact-panel update passed all 47 Companion checks and a
Gateway build (application flash 1,420,150 / 1,572,864 bytes; Companion build ID
`30b5af80e0f9e72c`). Browser checks covered zero/one/two visible settings,
preserved name/direction while collapsing and refreshing, saving shared serial
options, grouped sample readings, diagnostics, disconnect and permission
gating, and a 375 px viewport without horizontal overflow. No browser errors
were reported. The updated UI is local and was not flashed to the Gateway;
the previously packaged release below predates this layout update.

Release **0.2.0** was packaged on 2026-10-06 using
`scripts/build-release.ps1 -Version 0.2.0`. Both firmware targets and the portable
Companion built successfully, with all 47 Companion checks passing. The local
release is `release/FlexBMS-0.2.0/`; its `FlexBMS_bundle.fbu` is 1,554,670 bytes
and contains the v0.2.0 STM32 and Gateway images. Bundle payloads match the
release/build binaries, their CRC32 values and all six release SHA-256 entries
were verified, and the Gateway factory image contains the application at
`0x20000`. Companion's bundle parser accepts the artifact. The Companion
package/lockfile and STM32 firmware header are synchronized to 0.2.0. Release
binaries remain local under the repository's existing ignore rule; source,
docs and the generated Gateway browser bundle are versioned. No hardware was
flashed; the commissioning checks above remain outstanding.
