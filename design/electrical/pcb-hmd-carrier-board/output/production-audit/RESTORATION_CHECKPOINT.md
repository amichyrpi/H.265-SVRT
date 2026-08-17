# Board2 restoration checkpoint

Generated before new production routing.

- Component/footprint count before restoration: **306**
- Component/footprint count after restoration: **312**
- Restored components: **C301, J301, R301, R302, R303, U301**
- Components moved during restoration: **U207**
- Components deleted during restoration: **none**
- Changed reference designators: **REF** -> H1, REF** -> H2, REF** -> H3, REF** -> H4, REF** -> H5, REF** -> H6**
- microSD restoration: **complete on the main PCB; J301 moved 1 mm left for verified J8 shell clearance**
- Default assembly state: **populated for CM4 Lite; DNP in the same-PCB eMMC variant**
- Existing schematic references: **312**
- Board BOM references: **265**
- PCB-only references still requiring schematic recovery: **0**
- Schematic-only references requiring reconciliation: **0**
- DRC checkpoint: **0 placement/footprint violations, 499 unconnected items**

## Remaining unrouted nets by functional block

- Display control / power: 6 unique nets
- HDMI: 2 unique nets
- MIPI D-PHY: 20 unique nets
- PD / charger / power distribution: 2 unique nets
- Remaining low-speed / verify: 1 unique nets
- USB3 / Type-C mux: 12 unique nets
- User controls / proximity / LEDs: 1 unique nets

Full net lists: `Restoration-Unrouted-Groups.csv`.

## Placement consequence

The authoritative microSD location was restored.  KiCad proved a physical overlap
with J8/U207.  J301 was moved 1 mm left and U207 moved 1.6 mm right / 3.7 mm down;
the pre-routing physical DRC is now clean.  Six duplicate `REF**` mounting-hole
references were annotated H1-H6 because unique references are required for
manufacturing exports.  All unrelated component positions remain frozen.
