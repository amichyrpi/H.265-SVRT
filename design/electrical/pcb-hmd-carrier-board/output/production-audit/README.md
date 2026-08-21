# Carrier board 2 production audit

Generated from `pcb-hmd-carrier-board2.kicad_pcb`. This is a review checkpoint, not a fabrication release.

- Approved edge-cut envelope: 145.70 x 49.82 mm
- Copper layers: 6
- Footprints: 312 (4 marked DNP)
- Footprints containing at least one 3D model reference: 265/312
- Named nets represented by pads or copper: 278
- Nets with any track/via copper: 94/278
- Board tracks/vias: 1290
- Copper zones: 2
- Existing schematic components: 312
- PCB-only BOM references: 0
- Schematic-only references: 0
- Fitted BOM references missing one or more sourcing fields: 261/261
- Production DRC geometry violations: 0
- Unconnected pads at this checkpoint: 499
- Uncapped connectivity: 817 physical open edges across 184 incomplete nets
- Electrically complete high-speed differential pairs: 0/38

## Release status

All Board2 references are reconciled to the recovered schematic hierarchy. Gerber
release remains gated on zero unrouted items, clean DRC, high-speed route audit,
and the open battery/tracking/vendor-controlled production inputs documented in
`docs/PRODUCTION_BLOCKERS.md`.
