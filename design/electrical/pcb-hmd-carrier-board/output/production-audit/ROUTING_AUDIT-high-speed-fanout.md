# Board2 routing audit

- Source: `pcb-hmd-carrier-board2-high-speed-fanout-candidate.kicad_pcb`
- Track segments: **370**
- Vias: **101**
- High-speed differential pairs measured: **38**
- High-speed pairs with copper on both sides: **26/38**
- Worst P/N copper-length mismatch: **35.2392 mm**
- High-speed pairs missing one side: **L_DSIA_D0, L_DSIA_D1, L_DSIA_D3, L_DSIB_CLK, L_DSIB_D0, L_DSIB_D1, L_DSIB_D2, L_DSIB_D3, R_DSIA_CLK, R_DSIA_D3, R_DSIB_CLK, R_DSIB_D0**
- Power nets below their preliminary class width: **0**

The mismatch result is a geometric audit, not a field-solver result. Final
impedance and delay require JLCPCB stackup confirmation and coupon review.

