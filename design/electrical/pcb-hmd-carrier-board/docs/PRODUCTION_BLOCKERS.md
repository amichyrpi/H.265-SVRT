# Production release gates

The user formally approved the **145.70 x 49.82 mm** outline and necessary
placement changes on 2026-08-15. Those items are not blockers. The categories
below deliberately distinguish routing, fabrication and production release: a
later-stage gate does not stop independent prototype routing.

## Routing blockers

| Affected block | Current evidence | Required closure |
|---|---|---|
| Tracking | J7 is mechanically fixed but its 100-pin electrical assignment is reserved | Freeze the tracking-board pin map and power/current allocation before routing J7 signal pins |
| Battery flex | J2 is mechanically fixed, but all 20 signal contacts and both shell/mechanical contacts are presently unassigned; `BAT_ID` exists only at TP3 | Select the exact protected 2S pack/harness and assign redundant BAT+/GND, NTC and ID contacts before routing J2 |
| Display vendor-controlled details | The PCB preserves the bridge and panel circuits, but some Toshiba/BOE strap, termination and initialization data remains VERIFY | Route only documented nets; do not guess undocumented straps or termination values |
| Recovered symbols | All 312 PCB references and exact PCB pad/net assignments are reconciled with zero KiCad schematic-parity issues; recovered pin electrical types remain passive/VERIFY | Replace with reviewed vendor symbols before changing any recovered net or asserting functional sign-off |

These blockers apply only to their affected connections. Power, CM4 Lite SD,
USB, audio, user I/O and other documented nets may continue to be routed.

## Fabrication blockers

| Gate | Current evidence | Required closure before ordering Rev A |
|---|---|---|
| Battery pack | Protected 2S / 7000 mAh concept only | Exact pack/BMS MPN, limits, NTC curve, connector/harness rating and fault policy |
| Controlled impedance | JLC06161H-3313 is selected; project widths remain preliminary | Confirm the stack is available on the actual quote and have JLCPCB tune 85/90/100-ohm geometries and coupons |
| Enclosure/mechanical fit | Outline is approved; final shell datum/STEP is absent | Verify connector openings, bosses, CM4 cooling volume, flex bends, acoustic ducts, card insertion and service access |
| Vendor footprint audit | Placement is frozen from the approved Board2 baseline | Check every production land pattern and 3D body against controlled manufacturer drawings |
| Exact assembly BOM | The current audit finds 261/261 fitted references missing at least one Manufacturer, MPN or supplier-part field | Select qualified voltage/tolerance/dielectric/current-rated parts and approved alternates; close every row in `output/production-audit/BOM-Release-Gaps.csv` |

## Production-release blockers

| Gate | Required closure before declaring production-ready |
|---|---|
| GPIO ownership | Freeze reset, interrupt, fan, tracking, panel, LED and button GPIO ownership |
| Safety/compliance | Complete battery, wearable, EMC, ESD, radio and transport safety reviews |
| Prototype evidence | Complete staged power, SI, thermal, display, USB, audio, tracking and fault bring-up on assembled Rev A hardware |
| Manufacturing evidence | Pilot build, AOI/X-ray review where applicable, functional-test coverage and incorporated rework |

## Current validation checkpoint

- Board2 hierarchy: 312/312 references reconciled, zero PCB/schematic parity
  issues, and one documented ERC isolated-label warning (`BAT_ID`).
- PCB geometry DRC: 0 violations using three documented exact-footprint rules.
- PCB connectivity: routing work is measured in the reports under
  `output/production-audit/`; KiCad's text report caps displayed unconnected
  items at 499, so `ROUTING_AUDIT-production-current.md` also reports the true
  physical open-edge count. An autorouted candidate is never promoted merely
  because it reduces that count.
- Known accidental shorts: 0.
- Gerbers/drills: intentionally not generated.

The geometry DRC result does not mean the board is electrically complete. Release
requires zero unrouted items and a schematic-to-PCB match in addition to DRC.
