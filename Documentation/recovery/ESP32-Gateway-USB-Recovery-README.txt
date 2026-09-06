FlexBMS ESP32 Gateway USB recovery
==================================

This package restores the ESP32-C3 Gateway firmware. It is intended for a
Windows 11 laptop with a data-capable USB cable connected directly to the
ESP32-C3.

Important
---------

- Flash ONLY the ESP32 Gateway with this package. It does not update the
  STM32 BMS controller.
- Follow the site's normal battery/electrical safety procedure before
  connecting or disconnecting cables.
- Use the ESP32-C3 native USB connector. Do not connect this cable to an
  STM32/ST-Link connector.
- Close PlatformIO, serial monitors, and other programs that may be using the
  ESP32 COM port.
- Do not separately erase the flash or use another image. The supplied
  factory image contains the bootloader, partition table, and application.

Contents
--------

- FlexBMS-Gateway-factory.bin  Complete 4 MiB ESP32-C3 factory image.
- flash-gateway.cmd             Double-click launcher for Windows.
- flash-gateway.ps1             Flashing script and automatic checks.
- SHA256SUMS.txt                Image integrity checksum.
- RECOVERY_NOTES.txt            What this recovery build changes and what to
                                check after flashing.

Flashing procedure
------------------

1. Extract the ZIP to a normal folder, for example the Desktop. Do not run
   the script from inside the ZIP preview.
2. Connect the laptop directly to the ESP32-C3 with a known data-capable USB
   cable. Allow Windows a few seconds to create the COM port.
3. Double-click `flash-gateway.cmd`.
4. If more than one COM port is listed, choose the port belonging to the
   ESP32-C3 USB Serial/JTAG device. The script verifies the image checksum
   before it writes anything.
5. The script first tries the normal automatic reset into download mode. If
   that fails, use the manual procedure below and press Enter when prompted.
6. Wait for the script to report that flashing and verification completed.
   Do not disconnect USB while it is writing or verifying.

If the COM port is already known, the same operation can be started from
PowerShell with, for example:

    .\flash-gateway.ps1 -Port COM6

The script can install the required `esptool` automatically using the laptop's
internet connection. If Python is not installed, it will offer to install a
per-user Python package through Windows `winget`. No PlatformIO installation
is required.

Manual ESP32 download mode
--------------------------

Use this only if the automatic reset fails:

1. Keep the USB cable connected.
2. Hold the ESP32 BOOT button.
3. Press and release the ESP32 RESET/EN button (or power-cycle the ESP32 if
   there is no reset button).
4. Release BOOT.
5. Press Enter in the flashing window to retry.

If the board has no accessible BOOT and RESET/EN controls and automatic reset
does not work, the local hardware must expose those controls before USB
recovery is possible.

After a successful flash
------------------------

1. Leave the USB cable connected for at least 60 seconds while the Gateway
   starts.
2. The factory image starts with clean network storage. The Gateway should
   create an open setup Wi-Fi network named `FlexBMS-Setup-XXYYZZ` if it has
   no usable saved network. Connect to it and open `http://192.168.4.1`.
3. Save the normal Wi-Fi credentials in the Companion Wi-Fi page. Once the
   Gateway joins the station network, open `http://flexbms.local` or use its
   DHCP address.
4. Confirm that the Companion page loads and that the Gateway is reachable by
   ping or HTTP before disconnecting the USB cable.

The restored firmware deliberately starts Wi-Fi/HTTP recovery independently
of STM32 UART diagnostics. A missing STM32 response, a UART setup problem, or
a status LED problem must not prevent the ESP32 Gateway from remaining
reachable and accepting a Gateway firmware update.

If something goes wrong
-----------------------

- No COM port: try another USB cable/port, wait for Windows to enumerate the
  device, and check Device Manager under Ports or USB devices. A charge-only
  cable will not work.
- `Failed to connect` or sync errors: close all serial programs and use the
  manual download-mode procedure above, then retry.
- The script says Python/esptool cannot be installed: make sure the laptop is
  online and that `winget` is available. The exact command shown by the script
  can also be run from an Administrator-approved PowerShell window.
- No setup network after flashing: power-cycle the Gateway, wait up to one
  minute, then scan again. If the normal Wi-Fi credentials were retained by
  the device, use the normal network and `http://flexbms.local` instead.

This package is for Gateway recovery. Do not use it to repair or update an
STM32 controller that has already entered its bootloader or has lost its
application; that requires the separate STM32/SWD procedure.
