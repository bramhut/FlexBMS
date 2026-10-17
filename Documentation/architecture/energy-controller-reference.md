# Energy controller documentation reference

The separate Zaalsteden project owns the real-time controller for EMHASS battery targets, the hybrid inverter and Peblar charging. Zaalsteden is the initial HA OS deployment; Veldkamp will run the same application alongside HA in Docker on Ubuntu x86.

The design baseline is 9 October 2026. Controller software and deployment packages are not yet implemented. EV is not yet modelled in EMHASS; the first controller receives an independent requested EV current through HA, with EV priority inside electrical and device limits.

## Controller document index

The following paths are relative to the Zaalsteden project root, not this FlexBMS repository. Open that checkout on the current PC; do not assume a user-profile path or adjacent folder layout. A controller repository URL can replace these named-project references when a remote is configured.

| Path in Zaalsteden | Purpose |
|---|---|
| `README.md` | Documentation entry point and actual implementation status |
| `docs/architecture.md` | Software modules, control cycle, phase calculations, validity and failure boundaries |
| `docs/sites-and-entities.md` | Both site topologies, known entities, placeholders and electrical assumptions |
| `docs/deployment.md` | HA OS App and sibling Docker deployment, secrets and operating modes |
| `docs/implementation-and-commissioning.md` | Implementation milestones, tests, live checks and unresolved parameters |

Keep controller-specific decisions and validation evidence in those files. The older EMS sections in [the FlexBMS overview](../main.typ) describe the earlier baseline, not a second maintained controller specification. In particular, the detailed controller docs distinguish a confirmed schuur feeder scope, P1 current estimated with PF = 1, planned new phase meters, `last_reported` publishing requirements, BMS idle sequencing and accepted last-command persistence during complete communications loss.

## FlexBMS ownership

FlexBMS remains the source of truth for STM32 battery safety, inverter CAN limits, BMS run-request semantics, UART framing and Gateway firmware/MQTT contracts:

- [Home BESS firmware and maintenance](home-bess-firmware-and-maintenance.md).
- [Gateway and Companion API](companion-restructure-and-gateway-api.md).
- [UART protocol](../protocol/uart-v1.md).

The planned Gateway adjustment to avoid refreshing cached measurement entities must be documented and tested in FlexBMS when implemented. It has not been made as part of this documentation update. Do not treat HA, MQTT or the controller as a replacement for STM32/inverter protection, or a request acknowledgement as confirmation of HV operation.

The system overview PDF is generated from `Documentation/main.typ`. This reference has been added to the source; the existing PDF is intentionally not regenerated in this update, preserving its prior local changes.
