# FlexBMS Gateway

ESP32-C3-WROOM-02U-N4 Gateway firmware. It implements the STM32 UART v1
transport/telemetry decoder, serves the BMS Companion on the local network,
and provides local Wi-Fi configuration/recovery. MQTT, Home Assistant, and OTA
remain out of scope.

The canonical STM32--ESP32 framing and payload contract is
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
framework automatically.

From a terminal with PlatformIO installed:

```powershell
pio run
pio run --target upload --upload-port COMx
pio device monitor --baud 115200
```

At boot the firmware verifies the UART framing/CRC test vector, starts UART1,
and sends an empty v1 HEARTBEAT every 500 ms. Valid STM32 frames are decoded
and logged on USB. The receive parser accepts fragmented data and resynchronises
after malformed input or a bad CRC.

## Flash layout

The fitted ESP32-C3-WROOM-02U-N4 has 4 MiB flash. `partitions.csv` reserves
two 1.25 MiB application slots for ESP32 OTA, a 512 KiB raw `bms_update`
partition for a complete STM32 image, and 896 KiB LittleFS for the Companion
bundle, diagnostics, and small metadata. The STM32 image must be complete and
CRC-verified in `bms_update` before a future UART update begins.

| Partition | Offset | Size |
|---|---:|---:|
| `nvs` | `0x9000` | 24 KiB |
| `otadata` | `0xF000` | 8 KiB |
| `phy_init` | `0x11000` | 4 KiB |
| `ota_0` | `0x20000` | 1.25 MiB |
| `ota_1` | `0x160000` | 1.25 MiB |
| `bms_update` | `0x2A0000` | 512 KiB |
| `littlefs` | `0x320000` | 896 KiB |

This change reserves the space only. ESP32 OTA, LittleFS asset serving, and
STM32 update transfer remain separate implementation increments.

## Wi-Fi setup and recovery

On a device without saved credentials, the Gateway starts an open temporary
access point named `FlexBMS-Setup-XXYYZZ`, where the suffix comes from its MAC
address. Connect to it and open `http://192.168.4.1`. Common phone captive
portal prompts should reach the same Companion page automatically, but
HTTPS-only clients must use that address manually. The AP remains active until
credentials are submitted.

The Companion **Wi-Fi** page can scan nearby networks or accept a hidden SSID
manually. Its password field is write-only. Saving credentials stores them in
standard NVS, acknowledges the request, and restarts Gateway networking into
station mode. There is no candidate-network test or multi-network list: if the
replacement cannot connect, recovery provisioning is used.

After 30 seconds without a station IP, the Gateway starts the same AP in
AP+STA recovery mode for ten minutes while it continues station retries. It
then stops the AP for a one-minute station-only retry interval and repeats
until connected. A station IP stops the AP immediately.

After DHCP assigns a station address, the Gateway announces the hostname
`flexbms.local` with mDNS. It does not advertise mDNS during the setup or
recovery AP, and does not add a separate mDNS service record.

The first-time setup AP is intentionally open, so use it only while physically
present. It is also an open recovery AP: nearby users can view monitoring and
change Wi-Fi settings while it is active. Gateway-to-STM32 service controls
are disabled in this state. Credentials are never written to the log. Wi-Fi
failure or loss does not affect the STM32 UART link or safety decisions.
