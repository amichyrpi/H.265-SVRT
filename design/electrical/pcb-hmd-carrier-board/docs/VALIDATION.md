# Carrier board 2 validation checkpoint

Validation date: 2026-08-15, KiCad 10.0.5.

This is a placement/DFM checkpoint. It is not a fabrication release.

## Formal mechanical decision

- Edge-cut envelope: **145.70 x 49.82 mm**.
- The user formally approved the size change and necessary component-placement
  changes on 2026-08-15.
- The obsolete 140 mm maximum is no longer a PCB blocker; enclosure CAD must now
  respect the approved board envelope.

## Production baseline applied

- Six copper layers using the encoded JLCPCB `JLC06161H-3313` stack.
- Continuous GND planes on L2 (`GND1`) and L5 (`GND2`).
- Explicit 100 ohm HDMI/MIPI, 90 ohm USB3/USB2 and 85 ohm PCIe net classes.
- 100 high-speed/power/plane net-class assignments.
- Preliminary controlled-impedance widths recorded for board-house review.
- Dense reference designators moved from silkscreen to Fab; user-facing connector,
  orientation and warning labels remain on silkscreen.
- J9 duplicated charge/service USB-C contacts assigned to GND, VBUS, CC and USB2.
- A `VOL_DOWN_GPIO17_N` via/trace collision with `U210` was removed; no short remains.
- R261/R266 uncertain panel LEDPWM links are explicitly marked DNP.

## Primary PCB metrics

- Footprints: 312.
- BOM footprints: 265; 4 are intentionally DNP in the primary CM4 Lite assembly.
- microSD footprints: 6 on the main PCB, populated for the primary CM4 Lite
  assembly and DNP only in the optional eMMC assembly.
- Named nets represented by pads/copper: 278.
- Nets containing any routed copper: 14.
- Tracks/vias: 120.
- Copper zones: 2.
- Geometry/manufacturing DRC: **0 violations**.
- Unconnected items: **499**.
- Known accidental shorts: **0**.

The zero geometry result uses only three local manufacturer-footprint rules in
`pcb-hmd-carrier-board2.kicad_dru`:

1. J8 JAE DX07 shell-stake annulus geometry;
2. MK1/MK2 IM73A135 acoustic-port-to-ground-pad spacing (0.18 mm);
3. J9 JAE DX07 locating-hole-to-shell-stake spacing (0.25 mm).

General board clearance, annular, hole and edge constraints remain unchanged.

Current report:
`output/reports/DRC-pcb-hmd-carrier-board2-production-current.rpt`.

## Schematic authority audit

- Board2 hierarchy: 312 references across 8 recovered functional sheets.
- Board2 references absent from the hierarchy: **0**.
- Hierarchy references absent from Board2: **0**.
- PCB/schematic parity: **0 issues**.
- ERC: one documented isolated-label warning on `BAT_ID`; no power-driver
  conflicts.

Recovered passive/`VERIFY` pin types must still be replaced with controlled vendor
symbols as restricted source documents become available.

## CM4 storage variant

The complete J301/U301/C301/R301-R303 native SD circuit is on the main PCB. The
primary CM4 Lite assembly fits J301/U301/C301/R301; R302/R303 remain configurable
DNP links. The optional CM4 eMMC assembly DNPs the complete SD block while using
the same physical board.

## Review exports

- Review BOM: `output/production-audit/BOM-pcb-hmd-carrier-board2-review.csv`
- Review position list: `output/production-audit/PickAndPlace-pcb-hmd-carrier-board2-review.csv`
- Net/copper audit: `output/production-audit/Net-Audit-pcb-hmd-carrier-board2.csv`
- Schematic/PCB reference audit: `output/production-audit/Reference-Reconciliation.csv`
- Audit summary: `output/production-audit/README.md`
- ERC: `output/reports/ERC-pcb-hmd-carrier-board-current.rpt`
- Top render: `output/review/production-current/carrier2-top.png`
- Bottom render: `output/review/production-current/carrier2-bottom.png`
- Review STEP: `output/review/production-current/pcb-hmd-carrier-board2-review.step`
  (six mesh-only package bodies are omitted; see `3D_MODEL_STATUS.md`)

Gerbers and drill files are intentionally not generated while the authoritative
board remains unrouted. The schematic-authority gate is closed; routing, DRC,
high-speed, BOM, safety and prototype gates remain.
