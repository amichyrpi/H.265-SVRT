# Board2 routing audit

- Source: `pcb-hmd-carrier-board2-routed-candidate.kicad_pcb`
- Track segments: **1324**
- Vias: **237**
- High-speed differential pairs measured: **8**
- Worst P/N copper-length mismatch: **34.7707 mm**
- High-speed pairs missing one side: **HDMI_L_D1, USB3_POR, USB3_RX**
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
