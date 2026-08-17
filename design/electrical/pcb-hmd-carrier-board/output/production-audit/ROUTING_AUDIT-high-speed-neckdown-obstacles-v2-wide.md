# Board2 routing audit

- Source: `pcb-hmd-carrier-board2-high-speed-neckdown-obstacles-v2-wide.kicad_pcb`
- Track segments: **766**
- Vias: **182**
- Physical open edges: **883**
- Incomplete nets: **212**
- High-speed differential pairs measured: **38**
- High-speed pairs with copper on both sides: **38/38**
- Electrically complete high-speed pairs: **18/38**
- Worst P/N copper-length mismatch: **6.7711 mm**
- High-speed pairs missing one side: **none**
- Power nets below their preliminary class width: **1**

The mismatch result is a geometric audit, not a field-solver result. Final
impedance and delay require JLCPCB stackup confirmation and coupon review.

## Width findings

- +3V3: 0.200 mm minimum, 0.500 mm required
