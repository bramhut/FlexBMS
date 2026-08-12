# FlexBMS documentation

The source document is [`main.typ`](main.typ). It is a concise, current-state
overview of the FlexBMS system, hardware, software, and planned integrations.

The agreed Home-BESS Gateway, Companion, and firmware-update architecture is in
[`architecture/home-bess-firmware-and-maintenance.md`](architecture/home-bess-firmware-and-maintenance.md).

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
