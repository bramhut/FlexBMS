#let brand = rgb("#155e75")
#let brand-dark = rgb("#164e63")
#let brand-light = rgb("#ecfeff")
#let ink = rgb("#17202a")
#let quiet = rgb("#5f6b76")
#let border = rgb("#d5dde2")
#let panel = rgb("#f6f8fa")
#let planned = rgb("#8a5a00")
#let planned-light = rgb("#fff8dc")

#set document(
  title: "FlexBMS System Overview",
  author: "FlexBMS project",
)

#set page(
  paper: "a4",
  margin: (top: 22mm, bottom: 20mm, x: 20mm),
  header: align(
    right,
    text(size: 8pt, fill: quiet, tracking: 0.4pt)[FLEXBMS / SYSTEM OVERVIEW],
  ),
  footer: context align(
    center,
    text(size: 8pt, fill: quiet)[Page #counter(page).display("1")],
  ),
)

#set text(font: "Arial", size: 9.5pt, fill: ink, lang: "en")
#set par(justify: true, leading: 0.66em)
#set heading(numbering: "1.", outlined: true)
#set list(indent: 15pt, body-indent: 5pt, spacing: 4pt)
#set table(inset: (x: 6pt, y: 5pt), stroke: border)
#show table.cell: it => {
  set par(justify: false)
  it
}
#show heading.where(level: 1): it => block(
  above: 18pt,
  below: 8pt,
  breakable: false,
)[
  #text(size: 16pt, weight: "bold", fill: brand-dark)[#it]
  #v(3pt)
  #line(length: 100%, stroke: 0.8pt + brand)
]
#show heading.where(level: 2): it => block(
  above: 13pt,
  below: 5pt,
  breakable: false,
)[#text(size: 12pt, weight: "bold", fill: brand-dark)[#it]]

#let status(value, kind: "current") = {
  let status-color = if kind == "planned" { planned } else { brand }
  let status-fill = if kind == "planned" { planned-light } else { brand-light }
  box(
    fill: status-fill,
    stroke: 0.5pt + status-color,
    radius: 3pt,
    inset: (x: 5pt, y: 2pt),
    text(size: 7.5pt, weight: "bold", fill: status-color, value),
  )
}

#let callout(title, body, kind: "info") = {
  let callout-color = if kind == "planned" { planned } else { brand }
  let callout-fill = if kind == "planned" { planned-light } else { brand-light }
  block(
    width: 100%,
    fill: callout-fill,
    stroke: (left: 3pt + callout-color),
    radius: 3pt,
    inset: 9pt,
    above: 7pt,
    below: 7pt,
  )[
    #text(weight: "bold", fill: callout-color)[#title]
    #v(2pt)
    #body
  ]
}

#let header-cell(body) = table.cell(
  fill: brand-dark,
  stroke: brand-dark,
  text(fill: white, weight: "bold", body),
)

#align(center)[
  #v(28mm)
  #text(size: 12pt, weight: "semibold", fill: brand, tracking: 1.2pt)[FLEXBMS]
  #v(6mm)
  #text(size: 29pt, weight: "bold", fill: brand-dark)[System Overview]
  #v(3mm)
  #text(size: 14pt, fill: quiet)[Home battery energy storage system]
  #v(16mm)
  #line(length: 70%, stroke: 1pt + brand)
  #v(17mm)

  #table(
    columns: (42mm, 72mm),
    align: (left, left),
    stroke: none,
    inset: (x: 5pt, y: 4pt),
    [*Document date*], [30 July 2026],
    [*System*], [96s LiFePO4 home BESS],
    [*Nominal capacity*], [314 Ah / approximately 96.5 kWh],
    [*Primary controller*], [STM32G491],
    [*Document status*], [Living architecture overview],
  )

  #v(24mm)
  #text(size: 9pt, fill: quiet)[
    This document explains the current FlexBMS system at architecture level.
    Schematics and source code remain authoritative for implementation details.
  ]
]

#pagebreak()

#outline(title: [Contents], depth: 2, indent: auto)

#pagebreak()

= System level

FlexBMS is the battery management and integration platform for a stationary home battery energy storage
system. The battery consists of 96 LiFePO4 cells in series, arranged as four 24-cell modules. At a nominal
cell voltage of 3.2 V and a capacity of 314 Ah, the pack is approximately 307 V and 96.5 kWh.

The system connects the battery to a three-phase hybrid inverter. FlexBMS measures the battery, protects
it, controls the high-voltage connection, and provides the information needed by higher-level energy
management. The battery management system remains the final authority for battery safety.

== System roles

#table(
  columns: (39mm, 1fr, 29mm),
  header-cell([Layer]),
  header-cell([Responsibility]),
  header-cell([Status]),
  [Battery and HV hardware],
  [Stores energy, measures physical quantities, and safely connects the pack to the inverter.],
  [#status("CURRENT")],
  [STM32 BMS],
  [Performs real-time measurement, protection, state handling, and contactor control.],
  [#status("CURRENT")],
  [ESP32 gateway],
  [Publishes telemetry to the local network while remaining listen-only on CAN.],
  [#status("PLANNED", kind: "planned")],
  [Energy management system],
  [Chooses when and how much to charge or discharge within BMS limits.],
  [#status("PLANNED", kind: "planned")],
  [Home Assistant],
  [Provides dashboards, automation, and a user-facing view of the installation.],
  [#status("PLANNED", kind: "planned")],
)

== Energy and information flow

#align(center)[
  #block(
    width: 100%,
    fill: panel,
    stroke: border,
    radius: 4pt,
    inset: 12pt,
  )[
    #grid(
      columns: (1fr, 12mm, 1fr, 12mm, 1fr),
      align: center,
      [*Grid, PV and home*],
      [#text("<->")],
      [*Hybrid inverter*],
      [#text("<->")],
      [*FlexBMS battery*],
    )
    #v(8pt)
    #line(length: 100%, stroke: 0.5pt + border)
    #v(8pt)
    #grid(
      columns: (1fr, 10mm, 1fr, 10mm, 1fr),
      align: center,
      [Home Assistant],
      [#text("<->")],
      [EMS],
      [->],
      [Operating request],
    )
    #v(4pt)
    #align(center)[The BMS may reduce or reject any request to protect the battery.]
  ]
]

#callout(
  [Control principle],
  [
    Energy management is advisory; battery protection is mandatory. The EMS may request an operating
    point, but the BMS defines the permitted charge and discharge envelope and can open the contactor.
  ],
)

= Hardware

== Battery assembly

The pack contains 96 series-connected LiFePO4 cells divided into four physical 24s modules. Each module
contains two twelve-cell monitoring sections. A 200 A / 75 mV shunt is located at the negative end of the
pack for current measurement.

#block(breakable: false)[
  #table(
    columns: (45mm, 1fr),
    header-cell([Item]),
    header-cell([Current baseline]),
    [Cell chemistry], [LiFePO4],
    [Series configuration], [96s],
    [Physical modules], [4 x 24s],
    [Cell capacity], [314 Ah],
    [Nominal pack voltage], [307.2 V],
    [Nominal stored energy], [Approximately 96.5 kWh],
    [Current shunt], [200 A / 75 mV],
  )
]

The modules are installed in an unheated garage. Temperature measurement is distributed across the pack.
The current STM32 configuration expects four NTC inputs per battery-monitor IC. Final sensor population
and harness documentation should be kept consistent with the assembled modules.

== Module boards

Each 24s module has one FlexBMS module board with two MC33771C battery cell controllers. Each controller
measures twelve cells, supports passive balancing, reads local temperature sensors, and participates in
the transformer-isolated TPL daisy chain.

Across the complete pack this gives:

- eight MC33771C devices;
- 96 monitored cell voltages;
- up to 32 configured NTC measurements; and
- one current-sensing location on the first, lowest-voltage controller.

== Master controller

The master board is the central interface between the battery, high-voltage switching hardware, service
tools, inverter side, and local network.

Its main functional blocks are:

- STM32G491 real-time controller;
- MC33664 TPL transceiver for the module-board chain;
- ESP32-C3-WROOM-02U connectivity controller;
- isolated CAN and RS485 interfaces;
- isolated measurements of battery-side and load-side high voltage;
- contactor and precharge relay drivers;
- USB for flashing, diagnostics, and the Companion application; and
- power selection between USB and the battery-derived low-voltage supply.

The board contains separate HV, LV, and inverter-side isolated domains. USB-only power supports
development and diagnostics, but does not provide the 12 V rail required to energize the contactor.

== High-voltage path

The main contactor is located in the positive battery path. A separate precharge path limits inrush
current before the contactor closes. The STM32 is the sole software owner of both drivers. It measures
the battery and load sides through separate isolated AMC3330 channels, compares the battery-side result
with the voltage reported by the cell-monitor chain, and observes both active-low diagnostic outputs.

The contactor is pulled in at full drive and then held with 20 kHz PWM. Precharge remains closed during
pull-in and opens only after the contactor drive is established. The hold duty is a firmware
configuration value.

Voltage feedback shows whether the two HV nodes are plausible and whether precharge has progressed. It
does not prove the mechanical position of the contactor: the load-side voltage may remain present after
opening because the inverter DC-link discharge behavior is not yet characterized.

Pack-level protection also includes the main DC fuse and a manual DC isolation device. Exact component
ratings, wiring, creepage, clearance, and protection coordination belong in the hardware design files and
commissioning documentation.

== Physical communications

#block(breakable: false)[
  #table(
    columns: (33mm, 47mm, 1fr),
    header-cell([Interface]),
    header-cell([Connection]),
    header-cell([Purpose]),
    [TPL], [Master to 8 monitor ICs], [Cell, temperature, diagnostic, and current data.],
    [USB], [Service computer to STM32], [Firmware, logs, and Companion access.],
    [CAN], [STM32 and inverter-side domain], [Battery status and inverter communication.],
    [Wi-Fi], [ESP32 to local network], [Telemetry and future network services.],
    [RS485], [ESP32 to inverter-side equipment], [Fallback or supplementary Modbus connection.],
  )
]

= Software

The software is divided by responsibility. Time-critical protection and actuation remain on the STM32.
Network-facing functions belong on the ESP32. Site optimization belongs in the EMS, with Home Assistant
providing the local user and automation layer.

== STM32

#status("CURRENT")

The STM32 firmware is the real-time BMS. It starts and monitors the MC33771C chain, gathers a complete
battery measurement set, evaluates battery and communication faults, and controls relay permission.

The current battery profile is:

- eight MC33771C devices with twelve cells each;
- four NTC channels per controller on GPIO0 through GPIO3;
- current sensing on CID 1 with a 375 micro-ohm shunt value;
- positive current for charging and negative current for discharging;
- 314 Ah nominal capacity; and
- amp-hour retention in RTC backup registers while backup power is present.

The firmware separates active fault conditions, acknowledgement-required latches, and in-memory fault
history. The HV supervisor receives permission only while the BMS is running without blocking faults and
holds one fresh, complete measurement set from the monitor chain.

Runtime faults keep measurement and monitoring active while preventing relay closure. A clear request is
accepted only after the underlying runtime condition has disappeared. The BMS then repeats its device,
register, diagnostic, and measurement validation before allowing the latch to clear.

The STM32 also hosts:

- the dedicated HV supervisor;
- board-level analog and digital I/O;
- CAN communication;
- USB serial communication;
- and the browser-based Companion service interface.

=== Todo:
High priority:
- Go through CubeMX configuration. Pinout is OK.
- Watchdog implementation. Contactor should open in case of timeout.
- Analog measurement of HV side. Including diagnostic pin checking. ADC configuraion, high accuracy low Fs.
- Precharge handling. Including: 
- Contactor handling. Including: economizing, failure checking using voltages (to be discussed)
- USB VSENSE setup
- Derating on temperature (mainly low temperature charging, high temperature is not that applicable)
- Communication to Goodwe inverter. Exact protocol needs discussion and likely imperical data once we have the inverter ready.
- User LED

Medium priority:
- Communication to ESP32 over UART. Reporting and control. Companion should be made accessible from ESP32. A detailed plan still needs to be made for this.
- Aux relay control (potentially useful for a future heating system)


== HV supervisor

#status("CURRENT")

The HV supervisor follows `OFF -> SELF_TEST -> PRECHARGE -> CONTACTOR_CLOSE -> RUN`. It starts only on a
new run-request edge. No current software component supplies that request, so the supervisor remains
`OFF` by default.

`SELF_TEST` checks BMS permission, measurement freshness, isolated-voltage diagnostics, agreement between
the battery-side and cell-monitor voltages, an unenergized load side, and the available power source.
`PRECHARGE` raises the load-side voltage and requires a stable voltage relationship before contactor
pull-in. `CONTACTOR_CLOSE` transfers from the precharge path to contactor hold drive. `RUN` keeps
monitoring the BMS permission and both HV measurements.

A removed request, lost BMS permission, or implausible HV feedback disables both drivers. Shutdown and
fault handling require the request to return to off before a new edge can start another sequence.

USB-only operation supports service access but cannot request or energize the HV path. The fitted
hardware pull-down defines the inactive USB-sense level; the STM32 does not enable an internal pull.

#callout(
  [Source of truth],
  [
    Thresholds, timings, fault definitions, CAN identifiers, and register-level behavior are intentionally
    not duplicated here. The STM32 source code and hardware schematics are authoritative.
  ],
)

== ESP32

#status("PLANNED", kind: "planned")

The ESP32 is the network and telemetry controller in the isolated inverter-side domain. Its intended role
is to observe BMS and inverter information, publish selected data to the local network, and support
integration with the EMS and Home Assistant.

The ESP32 is not part of the battery safety chain. It is not allowed to transmit commands on the BMS CAN
bus and must remain listen-only there. Loss of the ESP32 or Wi-Fi must not prevent the STM32 from
protecting or isolating the battery.

No ESP32 firmware is currently present in this repository. The following items remain to be defined:

- telemetry transport and data model;
- network provisioning and update strategy;
- authentication and local security;
- use of Modbus TCP or RS485 for inverter telemetry; and
- the exact boundary between ESP32, EMS, and Home Assistant.

=== Pinout
Logical pin numbering on ESP32-C3-WROOM-02U-N4:
1. 3V3
2. EN
3. IO4 - CAN_TX
4. IO5 - CAN_RX
To be continued...


#block(breakable: false)[
  == Energy management system

  #status("PLANNED", kind: "planned")

  The EMS is the supervisory optimization layer. It should combine battery availability with site demand,
  PV production, inverter limits, dynamic electricity prices, EV charging, and other flexible loads.
]

The EMS may request charging or discharging, but it must operate inside limits supplied by the BMS and
inverter. It must tolerate missing telemetry and fall back to a safe, predictable operating policy.

The EMS implementation and deployment location are not yet defined in this repository. Its first
interface should be a small, explicit contract containing battery state, available charge/discharge
power, requested power, data freshness, and reason/status information.

== Home Assistant integration

#status("PLANNED", kind: "planned")

Home Assistant is the intended user-facing integration point for the installation. It can provide:

- battery, inverter, PV, grid, and load dashboards;
- fault and availability notifications;
- manual operating-mode selection;
- automation around dynamic prices and flexible loads; and
- long-term energy and temperature history.

Home Assistant must not become a safety dependency. If Home Assistant is unavailable, the STM32 must
continue to protect the battery and the EMS must remain within the last valid limits or stop requesting
power.

The entity model, discovery mechanism, command permissions, and dashboard design are still open.

== Companion application

#status("CURRENT")

The repository contains a Vue-based browser application for service and diagnostics. It connects to the
STM32 over Web Serial and presents live cell, temperature, fault, and register information. It is a
commissioning and engineering tool rather than the permanent home-energy dashboard.

= Operating concept

== Start-up

At start-up, the STM32 validates its configuration and initializes the complete TPL chain. It assigns
each battery monitor a chain address, configures its registers, runs diagnostics, and obtains a fresh
measurement set. Both HV outputs remain off. After BMS validation, the separate HV supervisor can run
its self-test and precharge sequence only when a new run request is supplied.

== Normal operation

During normal operation the STM32 repeatedly measures the pack and evaluates its safety state. Battery
limits and status are made available to the surrounding system. The inverter converts energy between the
DC battery and the AC installation, while the future EMS decides the desired operating point.

== Faulted operation

A battery, measurement, or communication fault immediately removes relay permission. Monitoring
continues where possible so that the live condition can be observed. Faults remain latched until they are
acknowledged through the supported service path and the BMS has successfully revalidated the battery
chain.

The BMS exposes the HV supervisor as one aggregate blocking fault while the supervisor retains separate
active, latched, and historical HV reasons. The Companion clear command can clear an HV latch only with
the run request off and the live HV conditions healthy. The BMS then repeats chain initialization,
diagnostics, and a complete fault-checked measurement cycle before the latch is released. Historical
reasons remain available until reboot.

Configuration and initial TPL setup faults require a reboot. The ESP32, EMS, and Home Assistant cannot
override a blocking STM32 fault.

= Interfaces and ownership

#block(breakable: false)[
  #table(
    columns: (38mm, 45mm, 1fr),
    header-cell([Information or action]),
    header-cell([Owner]),
    header-cell([Consumer]),
    [Cell voltages and temperatures], [STM32 BMS], [Diagnostics, inverter limits, telemetry],
    [Battery current and SoC], [STM32 BMS], [Inverter, EMS, Home Assistant],
    [Battery safety limits], [STM32 BMS], [Inverter and EMS],
    [Contactor and precharge], [STM32 BMS], [Physical HV path],
    [Inverter operating state], [Inverter], [ESP32 / EMS / Home Assistant],
    [Requested charge/discharge power], [EMS], [Inverter-facing control interface],
    [Dashboards and automation], [Home Assistant], [User and household services],
  )
]

The protocol between these layers should carry explicit validity and freshness information. A stale value
must never be interpreted as a current permission to charge, discharge, or close the contactor.

= Current project structure

#table(
  columns: (48mm, 1fr),
  header-cell([Repository area]),
  header-cell([Contents]),
  [`Hardware/Master`], [Master schematic, PCB, harnesses, and production data.],
  [`Hardware/ModuleBoard`], [Battery module-board schematic, PCB, and production data.],
  [`Software/Master`], [STM32G491 firmware and platform configuration.],
  [`Software/bms_companion`], [Browser-based USB service and diagnostic application.],
  [`Simulations`], [Electrical simulation files used during hardware development.],
  [`Documentation`], [This system overview and its Typst build setup.],
)

The repository does not yet contain ESP32, EMS, or Home Assistant implementation directories. They should
be added as separate components when their architecture and deployment boundaries are agreed.

= Open topics

This overview is intentionally incomplete. The next useful documentation additions are:

- confirmed as-built module, NTC, and high-voltage topology;
- final inverter selection, protocol compatibility, and commissioning sequence;
- system-level operating, fault, recovery, and shutdown policy;
- ESP32 telemetry, security, and update architecture; and
- EMS and Home Assistant interfaces, fallback behavior, and user permissions.
