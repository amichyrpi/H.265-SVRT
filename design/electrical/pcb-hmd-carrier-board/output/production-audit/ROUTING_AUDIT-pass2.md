# Board2 routing audit

- Source: `pcb-hmd-carrier-board2-pass2-candidate.kicad_pcb`
- Track segments: **1716**
- Vias: **317**
- High-speed differential pairs measured: **38**
- High-speed pairs with copper on both sides: **6/38**
- Worst P/N copper-length mismatch: **16.5081 mm**
- High-speed pairs missing one side: **HDMI_L_CLK, HDMI_L_D0, HDMI_L_D1, HDMI_L_D2, HDMI_R_CLK, HDMI_R_D0, HDMI_R_D1, HDMI_R_D2, L_DSIA_CLK, L_DSIA_D0, L_DSIA_D1, L_DSIA_D2, L_DSIA_D3, L_DSIB_CLK, L_DSIB_D0, L_DSIB_D1, L_DSIB_D2, L_DSIB_D3, PCIE_CLK, PCIE_RX, PCIE_TX, R_DSIA_CLK, R_DSIA_D0, R_DSIA_D1, R_DSIA_D2, R_DSIA_D3, R_DSIB_CLK, R_DSIB_D0, R_DSIB_D1, R_DSIB_D2, R_DSIB_D3, USB3_TX**
- Power nets below their preliminary class width: **7**

The mismatch result is a geometric audit, not a field-solver result. Final
impedance and delay require JLCPCB stackup confirmation and coupon review.

## Width findings

- +3V3: 0.200 mm minimum, 0.500 mm required
- LED_BOOST: 0.561 mm minimum, 1.000 mm required
- +1V05_USB: 0.250 mm minimum, 0.500 mm required
- VBUS_CHARGE: 0.200 mm minimum, 1.000 mm required
- +5V_SYS: 0.200 mm minimum, 1.000 mm required
- VBAT_SYS: 0.200 mm minimum, 1.000 mm required
- +1V8: 0.150 mm minimum, 0.500 mm required
