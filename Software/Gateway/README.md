# FlexBMS Gateway

ESP32-C3-WROOM-02U-N4 Gateway firmware. It implements the STM32 framed BMS v1
transport/telemetry decoder, serves the BMS Companion on the local network,
and provides local Wi-Fi configuration/recovery, station-LAN OTA, and Home
Assistant MQTT Device Discovery.

The canonical framed-BMS payload contract, shared by Gateway UART and direct
STM32 USB Companion, is
[`Documentation/protocol/uart-v1.md`](../../Documentation/protocol/uart-v1.md).
Use that document for codec changes and protocol tests.

## Hardware mapping

| Module pin | GPIO | Net | Function |
|---|---|---|---|
| 1 | -- | `p3v3_INV` | 3.3 V supply. |
| 2 | -- | `EN` | Chip enable. |
| 3 | GPIO4 / IO4 | `CAN_TX` | CAN controller transmit. |
| 4 | GPIO5 / IO5 | `CAN_RX` | CAN controller receive. |
| 5 | GPIO6 / IO6 | `RS485_RO` | RS-485 receiver output. |
| 6 | GPIO7 / IO7 | `RS485_DI` | RS-485 driver input. |
| 7 | GPIO8 / IO8 | `IO8/STRAP` | Strapping pin. |
| 8 | GPIO9 / IO9 | `IO9/BOOT` | Boot-mode strap. |
| 9; EPAD 19 | -- | `GND_INV` | Ground. The Master schematic represents EPAD 19 as its ground-pad pins 19--27. |
| 10 | GPIO10 / IO10 | `ESP_VBUS_SENSE` | USB VBUS sense. |
| 11 | GPIO20 / RXD | -- | Not connected. UART0 receive remains unused. |
| 12 | GPIO21 / TXD | -- | Not connected. UART0 transmit remains unused. |
| 13 | GPIO18 / IO18 | `USB_ESP32_N` | USB D-. |
| 14 | GPIO19 / IO19 | `USB_ESP32_P` | USB D+. |
| 15 | GPIO3 / IO3 | `UART_ESP_RX` | Receives STM32 USART1 TX (PA9) through the isolated UART link. |
| 16 | GPIO2 / IO2 | `UART_ESP_TX` | Transmits to STM32 USART1 RX (PA10) through the isolated UART link. GPIO2 is a strapping pin; the isolator must not force an invalid boot level during reset. |
| 17 | GPIO1 / IO1 | `USR_LED` | Gateway user/status LED. This is GPIO1, not GPIO17. |
| 18 | GPIO0 / IO0 | `RS485_EN` | RS-485 driver enable. |

The link is UART1, 1,000,000 bit/s, 8-N-1, no flow control. USB on GPIO18/GPIO19
remains independent and provides the development console and native JTAG.

## Build in VS Code

Install the PlatformIO IDE extension, open this `Software/Gateway` folder, and
select **PlatformIO: Build**. The project pins `espressif32@6.12.0`, which
supplies ESP-IDF 5.5.0. The first build downloads the required toolchain and
framework automatically. Before compiling ESP32 firmware, PlatformIO runs the
in-tree Companion's `npm run build:gateway` command. It regenerates the hashed
web assets, manifest, and compiled `GatewayAssets.cpp`, so the served UI is
always part of the same firmware image. Run `npm ci` once in
`Software/Companion` after a fresh clone; a Gateway build fails rather than
embedding stale UI assets if npm or its dependencies are unavailable.

From a terminal with PlatformIO installed:

```powershell
pio run
pio run --target upload --upload-port COMx
pio device monitor --baud 115200
```

The Gateway main FreeRTOS task is configured for a 16 KiB stack. MQTT Device
Discovery and compact-state JSON serialisation run from that task, so the
default ESP-IDF stack is insufficient. `sdkconfig.defaults` supplies the value
for a fresh configuration and the pre-build script keeps PlatformIO's ignored,
persisted `sdkconfig.flexbms_gateway` aligned; no manual menuconfig change is
required.

The local HTTP server is deliberately limited to four browser connections so
the ESP32-C3's small TCP socket pool retains room for MQTT and network services.
At most one asynchronous telemetry frame is outstanding per WebSocket. A
browser that stops reading is marked for retirement after its first failed
send, receives no more queued telemetry, and is closed by the regular Gateway
task. MQTT client-start failures use a 5--60 second backoff and report heap
headroom rather than retrying once per main-loop tick.

The project monitor explicitly leaves DTR and RTS inactive because COM6 is the
ESP32-C3 native USB Serial/JTAG console. Opening or closing it is therefore a
passive observation operation, not a request to reset or enter download mode.

At boot the firmware verifies the UART framing/CRC test vector, starts UART1,
and sends an empty v1 HEARTBEAT every 500 ms. Valid STM32 frames are decoded
and logged on USB. The receive parser accepts fragmented data and resynchronises
after malformed input or a bad CRC.

## Flash layout

The fitted ESP32-C3-WROOM-02U-N4 has 4 MiB flash. `partitions.csv` reserves
two 1.5 MiB application slots for ESP32 OTA, a 512 KiB raw `bms_update`
partition for a complete STM32 image, and 384 KiB LittleFS for future
diagnostics and metadata. The Companion bundle is currently compiled into the
application image, so it is versioned atomically with Gateway firmware. The
STM32 image must be complete and CRC-verified in `bms_update` before a future
UART update begins.

| Partition | Offset | Size |
|---|---:|---:|
| `nvs` | `0x9000` | 24 KiB |
| `otadata` | `0xF000` | 8 KiB |
| `phy_init` | `0x11000` | 4 KiB |
| `ota_0` | `0x20000` | 1.5 MiB |
| `ota_1` | `0x1A0000` | 1.5 MiB |
| `bms_update` | `0x320000` | 512 KiB |
| `littlefs` | `0x3A0000` | 384 KiB |

ESP32 OTA, LittleFS asset serving, and STM32 update transfer remain separate
implementation increments.

The application image belongs at the `ota_0` offset from `partitions.csv`,
currently `0x20000`. The Gateway build derives its upload offset from that CSV;
do not use the ESP-IDF default `0x10000` offset with this partition layout.

## Wi-Fi setup, fallback, and recovery

The saved NVS credentials are always the primary network. Before connecting,
the Gateway scans; it selects the visible saved SSID first, then the first
visible entry in its ordered local fallback list. This avoids connection
attempts to SSIDs that are not currently in range. A candidate with an
authentication failure is skipped for the rest of that selection cycle. Once
connected, the Gateway stays on that network. If that connection later drops,
it starts a fresh cycle and evaluates the saved NVS network before fallbacks
again. Connecting to a fallback never replaces the saved NVS credentials.

Fallback credentials are optional and intentionally local-only. Copy
[`FallbackNetworks.local.example.h`](components/wifi_manager/include/flexbms/FallbackNetworks.local.example.h)
to `FallbackNetworks.local.h` in the same directory, replace the placeholders,
and list at most four networks in priority order. The real file is ignored by
Git. These credentials are compiled into the firmware image, not protected as
a secret store: someone who can read or extract the device firmware may recover
them. Use only local fallback networks with credentials acceptable for that
exposure, and do not add the local header to source control.

On a device without saved credentials, configured local fallbacks are tried
first. If none is visible or usable, the Gateway starts an open temporary
access point named `FlexBMS-Setup-XXYYZZ`, where the suffix comes from its MAC
address. Connect to it and open `http://192.168.4.1`. Common phone captive
portal prompts should reach the same Companion page automatically, but
HTTPS-only clients must use that address manually. The AP remains active until
credentials are submitted.

The Companion **Wi-Fi** page can scan nearby networks or accept a hidden SSID
manually. Its password field is write-only. Saving credentials stores them in
standard NVS, acknowledges the request, and restarts Gateway networking into
station mode. If the saved network is unavailable, fallback selection and then
recovery provisioning are used.

After 30 seconds without a station IP, the Gateway starts the same AP in
AP+STA recovery mode for ten minutes. It then stops the AP for a one-minute
station-only selection interval and repeats until connected. A station IP
stops the AP immediately.

After DHCP assigns a station address, the Gateway announces the hostname
`flexbms.local` with mDNS. It does not advertise mDNS during the setup or
recovery AP, and does not add a separate mDNS service record. Each station
link loss withdraws the responder; a later DHCP lease starts it again, with a
short retry if the mDNS stack is temporarily unavailable.

The first-time setup AP is intentionally open, so use it only while physically
present. It is also an open recovery AP: nearby users can view monitoring and
change Wi-Fi settings while it is active. Gateway-to-STM32 service controls
are disabled in this state. Credentials are never written to the log. Wi-Fi
failure or loss does not affect the STM32 UART link or safety decisions.

## MQTT and Home Assistant

On the trusted station LAN, the Companion **Wi-Fi** page also accepts a local
MQTT broker host, port, username, and write-only password. The Gateway stores
these separately in NVS and never connects to MQTT while its open setup or
recovery AP is active. It publishes one retained Home Assistant MQTT Device
Discovery configuration under `homeassistant/device/flexbms_XXYYZZ/config`, a
retained compact state at `flexbms/flexbms_XXYYZZ/state`, and retained
availability with an `offline` MQTT last will. `XXYYZZ` is derived from the
Gateway Wi-Fi MAC.

| Topic | Retention | Direction | Purpose |
|---|---|---|---|
| `homeassistant/device/flexbms_XXYYZZ/config` | retained | publish | Home Assistant Device Discovery configuration. |
| `flexbms/flexbms_XXYYZZ/state` | retained | publish | Compact aggregate BMS/Gateway state. |
| `flexbms/flexbms_XXYYZZ/availability` | retained | publish | `online`, with `offline` configured as the MQTT last will. |
| `flexbms/flexbms_XXYYZZ/event` | not retained | publish | UART v1 event ID/value with Gateway uptime. |
| `flexbms/flexbms_XXYYZZ/command/run_request` | not retained | subscribe | Only `ON` and `OFF` are accepted. |

The compact state exposes pack voltage/current/power, valid SoC, BMS/HV state,
cell-voltage extrema in V (three display decimals), cell-voltage delta in whole
mV, NTC-temperature extrema, active-fault text, and the `telemetry_fresh`,
`hv_running`, `fault_active`, and `uart_healthy` binary states. It also exposes
distinct ESP32 **Gateway uptime** and STM32 **BMS controller uptime** sensors.
The latter comes from the STM32 `STATUS.uptime_ms` field; both counters reset
when their respective controller resets and are diagnostic only. Home Assistant
normally prefixes an entity's friendly name with this discovered device's name;
that is Home Assistant's device-entity presentation, not a duplicate telemetry
field.

On MQTT connect and on Home Assistant's `homeassistant/status` birth `online`
message, the Gateway republishes discovery and current state. This
updates discovery metadata after a firmware upgrade without manually deleting
entities.

The published device provides aggregate BMS health and pack telemetry plus a
`run_request` switch. Its non-retained command topic accepts only `ON` and
`OFF`; those commands enter the same validated STM32 service slot as the local
Companion. The switch reports STM32 status, never an optimistic result or a
promise that HV will close. Fault acknowledgement, balancing, firmware update,
raw diagnostics, and inverter/EMS control are deliberately not MQTT commands.
## Release build

`../../scripts/build-release.ps1` runs the Companion checks, produces the
portable direct-USB Companion executable, and builds this Gateway firmware.
The resulting `FlexBMS-Companion.exe`, `FlexBMS_bundle.fbu`,
`FlexBMS-Gateway.bin`, `FlexBMS-Gateway-factory.bin`, `FlexBMS-STM32.bin`,
`flash-release.ps1`, and `SHA256SUMS.txt` are placed in
`release/FlexBMS-<version>/`. `FlexBMS_bundle.fbu` is the normal OTA artifact;
the raw images and factory image remain for wired recovery. The interactive
script prompts for the release version and synchronizes it to both the
Companion package files and STM32 firmware-version header; `-Version 0.1.1` is available for unattended
use. Repeating an existing version requires an explicit timestamped-repeat
choice. The normal PlatformIO Gateway build continues to regenerate the
compiled Gateway web bundle before compiling firmware.

From a release directory, `flash-release.ps1 -Target Gateway -GatewayPort COM6`
verifies checksums and writes the complete 4 MiB Gateway factory image through
the ESP32-C3 USB serial bootloader. Close any serial monitor and manually enter
the bootloader if the board does not reset into it automatically. Factory-image
creation reads the `ota_0` application offset from `partitions.csv` and checks
the merged image at that location before it is released.

## Station-LAN OTA

The local Companion page exposes update controls only while the Gateway is
connected to its station network. Select `FlexBMS_bundle.fbu`, then choose the
included STM32 BMS and/or ESP32 Gateway image. Companion streams each selected
image, and the Gateway validates its declared length and CRC-32 before
installing it. If both are selected, Companion completes STM32 first and
updates the rebooting Gateway last. `FlexBMS-Gateway-factory.bin` is only for
the wired recovery script. Setup and recovery APs do not expose update
controls. No account, signature, cloud, or automatic retry path is used; retain
`flash-release.ps1` for recovery.

After an accepted Gateway upload, Companion waits for the Gateway to reboot and
reconnect. A fresh Gateway bundle is identified by its content-derived build ID,
so the browser automatically reloads the non-cacheable HTML shell when needed;
a manual hard refresh is not required.
