# PCB stack and signal-integrity constraints

Mechanical envelope: **145.70 x 49.82 mm**, formally approved on 2026-08-15.

The project now uses the JLCPCB `JLC06161H-3313` six-layer, 1.6 mm controlled-
impedance construction as the Rev A fabrication baseline:

| Layer/material | Thickness | Copper | Intended use |
|---|---:|---:|---|
| F.Silkscreen / F.Mask | 0.015 mm mask | - | Marking / solder mask |
| L1 F.Cu | - | 0.035 mm (1 oz) | Components and short critical signals |
| 3313 prepreg, Er 4.1 | 0.0994 mm | - | L1-L2 dielectric |
| L2 In1.Cu | - | 0.0152 mm (0.5 oz) | Continuous GND plane (`GND1`) |
| FR-4 core, Er 4.6 | 0.5500 mm | - | L2-L3 dielectric |
| L3 In2.Cu | - | 0.0152 mm (0.5 oz) | Power / low-speed |
| 2116 prepreg, Er 4.16 | 0.1088 mm | - | L3-L4 dielectric |
| L4 In3.Cu | - | 0.0152 mm (0.5 oz) | Power / low-speed |
| FR-4 core, Er 4.6 | 0.5500 mm | - | L4-L5 dielectric |
| L5 In4.Cu | - | 0.0152 mm (0.5 oz) | Continuous GND plane (`GND2`) |
| 3313 prepreg, Er 4.1 | 0.0994 mm | - | L5-L6 dielectric |
| L6 B.Cu | - | 0.035 mm (1 oz) | Components and signals |
| B.Mask / B.Silkscreen | 0.015 mm mask | - | Solder mask / marking |

Finish: ENIG. The exact stack is encoded in `pcb-hmd-carrier-board2.kicad_pcb`.
Continuous GND zones have been created on L2 and L5.

## Production net classes

| Class | Target | KiCad starting width / gap |
|---|---:|---:|
| `HDMI_MIPI_100R` | 100 ohm differential | 0.123 / 0.150 mm |
| `USB3_90R` | 90 ohm differential | 0.170 / 0.150 mm |
| `PCIE_85R` | 85 ohm differential | 0.200 / 0.150 mm |
| `USB2_90R` | 90 ohm differential | 0.170 / 0.150 mm |

These widths are controlled-impedance calculator starting values, not an authority
to fabricate. JLCPCB must confirm/tune the geometry against the actual production
lot and add impedance coupons before release. Any stack substitution requires a new
field-solver calculation and DRC review.

Sources:

- https://jlcpcb.com/impedance
- https://jlcpcb.com/help/article/user-guide-to-the-jlcpcb-impedance-calculator
- https://jlcpcb.com/resources/6-layer-pcbs
