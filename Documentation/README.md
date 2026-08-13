# FlexBMS documentation

The source document is [`main.typ`](main.typ). It is a concise, current-state
overview of the FlexBMS system, hardware, software, and planned integrations.

The agreed Home-BESS Gateway, Companion, and firmware-update architecture is in
[`architecture/home-bess-firmware-and-maintenance.md`](architecture/home-bess-firmware-and-maintenance.md).
The canonical byte-level STM32--ESP32 protocol is
[`protocol/uart-v1.md`](protocol/uart-v1.md); use it for all UART implementation
and test work.

The implementation specification for the BMS-only Companion restructure and
its Gateway browser API is
[`architecture/companion-restructure-and-gateway-api.md`](architecture/companion-restructure-and-gateway-api.md).

## Prerequisite

The repository currently targets Typst 0.15.1. Install it on Windows with:

```powershell
.\Documentation\install-typst.ps1
```

The script uses the official `Typst.Typst` Windows Package Manager package.

## Build

From the repository root:

```powershell
.\Documentation\build.ps1
```

This generates `Documentation\FlexBMS-System-Overview.pdf`.

For continuous rebuilding while editing:

```powershell
.\Documentation\build.ps1 -Watch
```
