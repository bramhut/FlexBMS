# FlexBMS Companion

The FlexBMS-only Companion has two compile-time targets. `npm run build:webserial`
creates the direct USB/Web Serial development build; `npm run build:gateway`
creates the committed ESP32 Gateway bundle under `dist/gateway` and its manifest.

The Gateway serves the latter on the trusted local LAN only. It is not an HV
safety authority. STM32 firmware updating is manual by USB/ST-Link in this
release. The Web Serial target uses the same CRC-framed BMS v1 protocol as the
Gateway UART link; it requires a Chromium-family browser with Web Serial.

`npm run build:desktop` produces a portable Windows executable below a new
timestamped `electron-dist/build-*/` directory and prints its exact path. It is
a thin Electron/Chromium wrapper around the direct USB Web Serial target: it
has no installer, updater, or native serial-protocol implementation. It is
deliberately unsigned and disables Electron-builder's Windows executable
signing/resource-editing pass, so packaging does not wait on `signtool`.
`../../scripts/build-release.ps1` builds that executable, the Gateway factory
image, and the STM32 image together, then writes them and SHA-256 hashes below
`release/FlexBMS-<version>/`. When run interactively, it prompts for the
release version; entering a new semantic version updates `package.json` and
`package-lock.json` before building. Use `-Version 0.1.1` for unattended use.
An existing version requires an explicit timestamped-repeat choice, so prior
artifacts are never overwritten.

Every release directory includes `flash-release.ps1`. Use `-Target Stm32` with
an ST-Link/SWD connection, `-Target Gateway -GatewayPort COM6` for the ESP32-C3
USB bootloader, or omit `-Target` to flash both. The script verifies release
hashes before erasing and flashing either controller.
