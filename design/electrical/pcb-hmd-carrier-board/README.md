# Stearlight HMD carrier - Rev A

This directory contains the first engineering milestone for the CM4-based Stearlight
VR HMD carrier. The project is a real hierarchical KiCad design with a six-layer,
145.70 mm x 49.82 mm placement board. It is deliberately marked **not for fabrication**
until the unresolved product interfaces and high-speed layout gates below are closed.

## Current milestone

- Eight Board2-specific hierarchical sheets recover CM4, power, PCIe/USB 3,
  audio, display, interfaces, microSD and supporting circuitry from the approved PCB.
- The official Raspberry Pi CM4 200-pin connector definition and combined carrier
  footprint are reused from `cm4-io/`.
- The schematic passes KiCad ERC with zero errors and one documented warning for
  the externally reserved single-ended `BAT_ID` net.
- The current board2 PCB contains 312 footprints on a six-layer construction.
- The 145.70 x 49.82 mm outline and required placement changes were formally
  approved on 2026-08-15; the future shell must use this frozen PCB envelope.
- Placement follows the supplied drawing: charge USB-C and jack on the left, two
  forward microphones, USB-C host and battery on the right, CM4 centered on the
  rear, and the 100-pin tracking connector on the rear-left region.
- The additive `pcb-hmd-carrier-board2.kicad_pcb` variant now carries the complete
  dual-eye BOE display placement: exact 50-pin panel connectors, two HDMI-to-dual-
  DSI bridges, clocks, power, bias, backlight, controls, passives and test pads.
  The existing manually placed carrier components were not moved.
- The useful legacy rear-side power, charge, service and tracking blocks have now
  been ported to the new outline. The obsolete source-board copy was removed.
- The complete native microSD circuit is restored on the main PCB. CM4 Lite is the
  primary fitted Rev A assembly; the optional eMMC assembly marks that block DNP.
- The JLCPCB `JLC06161H-3313` six-layer stack, two continuous internal GND planes,
  production net classes and narrowly scoped exact-footprint DRC rules are encoded.

Open [pcb-hmd-carrier-board2.kicad_pro](pcb-hmd-carrier-board2.kicad_pro) in KiCad 10
for the restored carrier and its matching Board2 schematic hierarchy. The original
project remains a reference baseline.

## Architecture

```text
protected 2S pack <-> BQ25798 charger/power path <- TPS25751D <- charge USB-C
        |
        +-> TPS568230 5 V / 8 A -> CM4 + protected USB host VBUS
        +-> CM4 3.3 V output -> control/audio rails
                               -> TPS62825 1.05 V USB controller rail

CM4 PCIe Gen2 x1 -> UPD720202 -> HD3SS3212 -> USB-C SuperSpeed host
CM4 PCM/I2S      -> TLV320AIC3204 -> TRRS + two IM73A135 microphones
```

## Review outputs

- `output/docs/Stearlight_HMD_Carrier_RevA_Schematic.pdf`
- `output/review/carrier-top-corrected.png`
- `output/review/carrier-bottom-corrected.png`
- `docs/BOM.csv`
- `output/assembly/PickAndPlace.csv`
- `output/reports/ERC.rpt`
- `output/reports/DRC.rpt`
- `output/review/legacy-port/carrier2-bottom-final.png`
- `output/review/legacy-port/carrier2-top-final.png`
- `output/reports/DRC-pcb-hmd-carrier-board2-legacy-port.rpt`
- `output/reports/DRC-pcb-hmd-carrier-board2-production-current.rpt`
- `output/reports/ERC-pcb-hmd-carrier-board-current.rpt`
- `output/production-audit/`
- `output/review/production-current/carrier2-top.png`
- `output/review/production-current/carrier2-bottom.png`
- `output/review/production-current/pcb-hmd-carrier-board2-review.step`

The authoritative PCB is electrically netted and routing is being developed in
isolated, measured candidates before copper is promoted. Its current geometry DRC
has zero violations and zero known shorts; the baseline still has 499 unconnected
groups and 14 of 278 named nets contain copper. All 312 PCB references are present
in the recovered hierarchy with zero KiCad schematic-parity issues. Gerbers are not
generated until the routed release candidate passes every release check; this keeps
a placement/routing experiment from being mistaken for fabrication data.

## Mandatory fabrication/production gates

1. Obtain the complete controlled BOE initialization/timing specification and
   validate the NT57860 sequence, DSI split and 1440 x 1600 timing on hardware.
2. Define the Lighthouse tracking-board pinout for `J7`.
3. Review every UPD720202 strap, reset, sequencing and passive against the gated
   Renesas hardware manual/reference schematic.
4. Obtain JLCPCB review/tuning of the preliminary HDMI/MIPI, PCIe, USB3 and USB2
   differential geometries against the selected `JLC06161H-3313` stack and coupons.
5. Recheck the exact JAE USB-C and Same Sky jack land patterns against the enclosure
   datum before freezing the shell; their real connector bodies are present in the
   current 3D review.
6. Confirm the protected 2S battery pack, connector/harness ratings, NTC curve,
   pack ID policy and independent BMS certification.
7. Configure and validate the TPS25751 PD image/PDOs and charger limits.
8. Freeze shell CAD, CM4 heatsink/fan clearance, acoustic ducts and mounting datum.
9. Replace recovered passive/`VERIFY` pin types with controlled vendor symbols as
   restricted datasheets become available; reference and pad/net reconciliation is complete.

See `docs/DESIGN_PLAN.md`, `docs/POWER_BUDGET.md`, `docs/INTERFACES.md`,
`docs/STACKUP.md` and `docs/SOURCES.md` for the engineering assumptions.
The ordered routing, power-sequencing, DFM and bring-up workflow is in
`docs/PRODUCTION_EXECUTION_PLAN.md`; microSD variant rules are in
`docs/CM4_LITE_MICROSD.md`.
The remaining release inputs are tracked in `docs/PRODUCTION_BLOCKERS.md`; current
review-only BOM, placement and net reports are under `output/production-audit/`.
The exact-model coverage and unresolved CAD imports are listed in
`docs/3D_MODEL_STATUS.md`; measured validation status is in `docs/VALIDATION.md`.

## Regeneration and checks

Run from this directory in PowerShell:

```powershell
./tools/validate_board2_production.ps1
```

The older reference-hierarchy regeneration path remains:

```powershell
./tools/fetch_kicad_models.ps1
python tools/generate_carrier_schematic.py
kicad-cli sch upgrade --force pcb-hmd-carrier-board.kicad_sch
Get-ChildItem sheets -Filter *.kicad_sch | ForEach-Object {
    kicad-cli sch upgrade --force $_.FullName
}
kicad-cli sch erc pcb-hmd-carrier-board.kicad_sch -o output/reports/ERC.rpt
kicad-cli sch export netlist pcb-hmd-carrier-board.kicad_sch `
    --format kicadxml -o output/pcb-hmd-carrier-board.net.xml
& "C:\Program Files\KiCad\10.0\bin\python.exe" tools/generate_carrier_pcb.py
kicad-cli pcb drc pcb-hmd-carrier-board.kicad_pcb -o output/reports/DRC.rpt `
    --all-track-errors
```

Do not generate manufacturing Gerbers until all mandatory gates are closed and DRC
is clean.
