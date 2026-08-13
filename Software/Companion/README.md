# FlexBMS Companion

The FlexBMS-only Companion has two compile-time targets. `npm run build:webserial`
creates the direct USB/Web Serial development build; `npm run build:gateway`
creates the committed ESP32 Gateway bundle under `dist/gateway` and its manifest.

The Gateway serves the latter on the trusted local LAN only. It is not an HV
safety authority. STM32 firmware updating is manual by USB/ST-Link in this
release.
