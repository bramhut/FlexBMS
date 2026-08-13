# FlexBMS Gateway

ESP32-C3-WROOM-02U-N4 Gateway firmware. It implements the STM32 UART v1
transport/telemetry decoder and first-time Wi-Fi provisioning. MQTT, Home
Assistant, LAN maintenance APIs, WebSocket, OTA, and Gateway-originated BMS
service requests remain out of scope.

## Hardware mapping

| ESP32-C3 pin | Net | Function |
|---|---|---|
| GPIO3 | `UART_ESP_RX` | Receives STM32 USART1 TX (PA9) through the isolated UART link. |
| GPIO2 | `UART_ESP_TX` | Transmits to STM32 USART1 RX (PA10) through the isolated UART link. GPIO2 is a strapping pin; the isolator must not force an invalid boot level during reset. |

The link is UART1, 1,000,000 bit/s, 8-N-1, no flow control. USB on GPIO18/GPIO19
remains independent and is used for the development console.

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

## First-time Wi-Fi setup

On a device without saved credentials, the Gateway starts an open temporary
access point named `FlexBMS-Setup-XXYYZZ`, where the suffix comes from its MAC
address. Connect to it, open `http://192.168.4.1`, enter the local Wi-Fi SSID
and password, and submit the form. Credentials are stored in standard NVS; the
Gateway reboots into station mode and the setup AP stops.

The first-time setup AP is intentionally open, so use it only while physically
present. Credentials are never written to the log. Wi-Fi failure or loss does
not affect the STM32 UART link.

To change saved credentials in this increment, erase the device flash/NVS and
upload the firmware again:

```powershell
pio run --target erase --upload-port COMx
pio run --target upload --upload-port COMx
```
