# Stearlight carrier production execution plan

Status: **execution authorized; placement/DFM baseline complete; electrical release
gates remain open**. The user formally approved the 145.70 x 49.82 mm outline and
necessary placement changes on 2026-08-15. No Gerber package is authorized until
every electrical, safety, mechanical and validation gate below is closed.

## 0. Current design baseline

- One 145.70 x 49.82 mm edge-cut envelope remains in the board file. The obsolete
  source-board copy has been removed; its pre-port state is preserved under
  `output/review/pcb-hmd-carrier-board2-before-legacy-port.kicad_pcb`.
- The rear now contains the retained tracking, service, USB-PD, charger, 5 V and
  USB-controller 1.05 V blocks. The duplicate source CM4 and source PCIe AC
  capacitors were intentionally not retained.
- The complete J301 microSD circuit is present on the main PCB. The primary Rev A
  CM4 Lite BOM populates it; the optional eMMC BOM marks it DNP on the same PCB.
- Geometry/manufacturing DRC is clean using three documented exact-footprint rules.
  The validated routing baseline contains 1,077 segments and 213 vias, with 817
  physical open edges across 184 incomplete nets and no known shorts. KiCad's
  text DRC display is capped at 499 unconnected items; the uncapped count comes
  from `output/production-audit/ROUTING_AUDIT-production-current.md`.
- The approved edge-cut envelope is 145.70 x 49.82 mm. Future shell CAD must use
  this frozen PCB envelope.
- The JLCPCB `JLC06161H-3313` stack, L2/L5 GND planes, production net classes and
  100 explicit high-speed/power/plane assignments are encoded in the project.
- The Board2 recovery hierarchy reconciles all 312 PCB references. Passive/VERIFY
  pin types remain a controlled-document audit item, not a reason to delete hardware.

## Execution progress

| Work item | Status |
|---|---|
| Formal outline approval | Complete |
| Placement/legend DFM cleanup | Complete |
| Known short correction | Complete |
| Six-layer stack and net-class baseline | Complete; board-house tuning pending |
| Continuous internal GND planes | Complete |
| Exact-footprint DRC review/rules | Complete |
| eMMC vs CM4 Lite storage split | Complete |
| Full board2 schematic/netlist reconciliation | Complete at PCB-authority level; vendor-symbol audit pending |
| Final routing and power sequencing | In progress; depends on the gates below |
| Fabrication outputs | Withheld |

The 2026-08-20 routing review retained 28 additional DRC-clean low-speed nets.
The power and MIPI autorouter candidates were rejected: the power candidate crossed
validated signal copper, while both MIPI candidates violated production-width
clearance and failed paired-net completeness. Those candidates remain under
`output/production-audit/` for review and were not merged into the release baseline.

## 1. Gated inputs

These inputs gate only the affected routing, fabrication, or production-release
decision. They do not stop independent Rev A prototype routing:

1. Protected 2S/7000 mAh battery pack, BMS limits, NTC curve, pack-ID policy,
   connector/harness current rating and safety certification.
2. JLCPCB confirmation/tuning of the selected six-layer controlled-impedance stack,
   trace geometries, via capability, annular ring and impedance coupons. Topology
   and routing corridors may be implemented first; the tuned geometry is a
   fabrication gate.
3. Final shell datum, connector openings, screw bosses, CM4 heatsink/fan volume,
   microphone acoustic channels, display-flex bends and battery clearance.
4. Final tracking-board pin assignment for the 100-pin Hirose connector. This is a
   routing blocker for the reserved J7 signals only.
5. Exact battery-flex pin assignment. J2 currently preserves the approved 20-pin
   plus mechanical-pad footprint, but all contacts are unassigned and `BAT_ID`
   currently terminates only at TP3. Freeze redundant high-current BAT+/GND,
   NTC and ID contacts from the selected protected pack before routing J2.
6. Complete Toshiba, Renesas, TI, BOE and connector manufacturer design data needed
   to verify every strap, pin, termination, land pattern and power sequence. An
   undocumented item blocks only its own circuit; preserve it as `VERIFY` and
   continue independent work.
7. CM4 GPIO allocation freeze, including display control, fan, buttons, status LED,
   proximity, audio, tracking and all interrupt/reset lines.
8. Replacement of recovered passive/VERIFY pin types by reviewed vendor symbols.

## 2. Schematic and netlist freeze

1. Maintain the recovered complete `board2` hierarchy and replace VERIFY symbols
   incrementally without changing PCB references or nets.
2. Import the retained blocks into dedicated sheets: `CM4`, `Power_PD_Charge`,
   `USB3`, `Audio`, `Tracking`, `User_IO`, `Display`, and `CM4_Lite_SD`.
3. Revalidate TPS25751D + BQ25798 against TI's USB-PD-CHG-EVM-01 material. Confirm
   every PD configuration pin, EEPROM connection, current sense, charger inductor,
   compensation component, NTC network, battery sense and protection path.
4. Revalidate TPS568230 and TPS62825 feedback dividers, current limits, compensation,
   input/output capacitors, saturation current and derating from official data.
5. Complete the currently reserved J7 tracking pinout. Until then J7 cannot be routed
   or considered production-complete.
6. Cross-check every CM4 connector pin against the official CM4 IO Board. Keep the
   native SD circuit populated in the primary CM4 Lite BOM and DNP in the optional
   eMMC BOM.
7. Run ERC with zero unexplained errors/warnings. Document narrowly scoped waivers;
   never globally suppress a rule.
8. Export a fresh netlist and perform a schematic-to-PCB comparison before placement
   freeze. Resolve reference/value/MPN drift and regenerate BOM fields.

## 3. Power tree and enforced sequence

Target power flow:

```text
charge USB-C -> TPS25751D PD -> BQ25798 power path/2S charger -> VBAT_SYS
VBAT_SYS -> TPS568230 -> +5V_SYS -> CM4, display power and protected USB VBUS
+3V3 -> TPS62825 -> +1V05_USB
+5V_SYS -> display low-voltage bucks, LCD bias and WLED boost
```

The implementation must have hardware-safe defaults and firmware-observable power
good/fault states. Exact delays and voltage thresholds must come from the device and
panel documents; the sequence below defines order only:

1. Battery/USB insertion: external VBUS off, WLED off, LCD reset asserted, bridge
   reset asserted and CM4 enable held in its documented safe state.
2. Detect source, negotiate only validated PD PDOs, apply charger input/current/
   thermal limits, then establish the BQ25798 system path.
3. Start `+5V_SYS`; verify ramp, overshoot, inductor current and `PGOOD_5V` under
   minimum/maximum battery and hot-plug conditions.
4. Start always-required low-voltage rails and `+1V05_USB`; verify each PGOOD/reset
   relationship before releasing dependent ICs.
5. Release CM4 and USB controller only after their rails and reference clocks meet
   requirements. Keep USB host VBUS disabled until role/orientation logic is valid.
6. Enable display bridge rails in the Toshiba-required order, start REFCLK, then
   release each bridge reset independently.
7. Apply panel IOVDD and +5.5/-5.5 V bias in the BOE-required order, hold panel reset,
   send the validated initialization, start DSI video, and enable the backlight last.
8. Shutdown in reverse: backlight off first, panel reset/bias sequence, bridges off,
   peripherals off, CM4 shutdown handshake, then system rail removal.

Bench validation must cover USB-only, battery-only, simultaneous charge/load,
thermal limiting, brownout, cable removal, shorted accessory VBUS, missing battery
NTC, stalled fan, and forced software crash. Capture oscilloscope plots for every
rail sequence and attach them to the release record.

## 4. Placement and mechanical freeze

1. Import the shell STEP and establish named PCB/enclosure datums. Lock the outline,
   mounting holes, connectors, microphones and display-flex locations.
2. Check front/back body collisions using the actual manufacturer STEP models,
   including FPC mating height, CM4 standoffs, microSD card insertion envelope and
   connector plug/cable keepouts.
3. Preserve CM4 heat-spreader and fan airflow volume. Keep battery cells and face-side
   surfaces outside measured worst-case hot zones.
4. Repack each regulator so input capacitor, IC, catch/synchronous path and inductor
   form the smallest possible loop. Place feedback components away from switch nodes.
5. Keep microphones and codec analog inputs away from bucks, inductors, HDMI, MIPI,
   USB3 and clocks; implement continuous local ground and the acoustic keepouts.
6. Place test pads so they remain accessible in a fixture with the CM4 installed.
7. Run full 3D interference review in the shell before placement sign-off.

## 5. Stackup and design rules

Use the selected JLCPCB stack data rather than generic widths. At minimum define:

- 100 ohm differential: HDMI TMDS and MIPI D-PHY;
- 90 ohm differential: USB 3 SuperSpeed;
- 85 ohm differential: PCIe Gen2;
- USB2 differential geometry per the CM4/reference design;
- power-net width/via tables derived from worst-case current and allowed temperature
  rise, not from the default KiCad net class.

Preferred plane concept is L1 signals/components, L2 solid GND, L3 power/low speed,
L4 power/low speed, L5 solid GND, and L6 signals/components. Changes require an
updated field-solver result. Avoid plane splits under every high-speed pair and clock.
Add impedance coupons and board-house notes to the fabrication drawing.

## 6. Routing order

1. Charger, system buck, USB VBUS switch and all high-current paths; verify loop area,
   copper temperature, via current and return paths.
2. PCIe from CM4 through the existing AC capacitors to UPD720202. Place coupling
   capacitors at the reference-design side and constrain intra-pair skew/vias.
3. USB3 controller to mux to Type-C, then USB2. Route orientation branches with
   matched topology and no stubs.
4. HDMI0/1 from CM4 to the two display bridges, followed by all MIPI DSI pairs from
   each bridge to its panel connector.
5. Crystals/oscillators and other clocks.
6. I2S and analog audio, maintaining separation from switch nodes and high-speed
   aggressors.
7. I2C, interrupts, resets, buttons, RGB LED, proximity, tracking low-speed nets and
   service signals.
8. Remaining power/ground connections, pours and stitching vias. Refill zones only
   after all return paths have been reviewed.

Length/skew numbers must be copied from the applicable official interface/device
requirements. Do not invent generic matching tolerances. Use symmetric via pairs,
short layer transitions and continuous reference planes.

## 7. Thermal, battery and protection verification

1. Produce a worst-case power budget for charge-and-play, both displays/backlight at
   maximum, USB accessory at current limit, Wi-Fi active and CM4 CPU/GPU stress.
2. Derate inductors, MOSFETs, connectors, capacitors and copper at the measured
   enclosed-headset ambient. Add thermal vias exactly where package guidance allows.
3. Simulate/measure charger, 5 V buck, USB3 controller, display bridges, WLED boost
   and CM4 hotspot temperatures with the intended fan and blocked-airflow fault.
4. Verify USB ESD at external connectors, audio protection, reverse/fault behavior,
   shield/chassis strategy and battery double-fault protections.
5. Complete IEC 62368-1-oriented product safety review and the applicable battery,
   EMC, radio and transport compliance plan before a wearable pilot build.

## 8. DRC, DFM and production data

1. Fix the existing footprint-origin, drill, annular-ring, edge-clearance, mask and
   silkscreen diagnostics against actual JLCPCB capabilities and manufacturer
   drawings. Do not waive them merely to obtain a green report.
2. Route until unrouted count is zero. Run DRC with all track errors and require zero
   unexplained shorts, clearances, courtyard collisions, dangling tracks and isolated
   copper.
3. Run independent netlist/BOM pin audits for CM4, PD/charger, USB3, both displays,
   audio and all external connectors.
4. Generate and review schematic PDF, Gerbers, drill/map, IPC-356 netlist, BOM,
   CPL/PickAndPlace, assembly drawings, paste layers and STEP assembly.
5. Use a CAM viewer independent of KiCad to compare copper, mask, paste, drills,
   plated slots, outline and layer order. Verify pin-1/polarity on every component.
6. Upload a non-ordered JLCPCB quote first and resolve component rotations, basic/
   extended parts, substitute policy, panel rails/fiducials and impedance notes.

## 9. Prototype bring-up

Build in controlled stages with current-limited supplies:

1. Bare-board continuity/impedance coupons and power-input isolation.
2. PD/charger only, then battery/system power under electronic load.
3. All low-voltage rails and reset supervision.
4. CM4 Lite boot, UART/service access, microSD power switching and controlled shutdown.
5. Optional eMMC BOM variant; never fit the native microSD circuit on that assembly.
6. USB2, PCIe enumeration, USB3 signal/eye test and accessory VBUS fault testing.
7. Audio codec, jack detect/mic paths, onboard microphone noise and acoustic test.
8. Buttons, RGB status LED, proximity, fan and tracking connector electrical tests.
9. One display bridge/panel at a time: rails, reset, I2C, test pattern, HDMI, DSI and
   backlight; only then enable dual-eye operation.
10. Full thermal, charge-and-play, suspend/resume, brownout, ESD pre-scan and extended
    reliability soak.

## 10. Fabrication release criteria

Release Gerbers only when all of these are true:

- schematic ERC and PCB DRC are clean except individually approved/documented items;
- zero unrouted nets and zero accidental shorts;
- power tree, sequencing and worst-case thermal tests are signed off;
- controlled-impedance geometry and coupons are approved by JLCPCB;
- exact production BOM has lifecycle/stock/alternate review and no silent substitutes;
- shell/connector/display/thermal 3D interference review is signed off;
- battery and wearable safety review is complete;
- pilot bring-up passed and all rework is incorporated into a new PCB revision;
- manufacturing outputs are independently CAM-reviewed and archived with hashes.
