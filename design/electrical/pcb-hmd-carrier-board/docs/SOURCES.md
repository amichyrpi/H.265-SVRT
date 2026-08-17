# Primary design sources

- Raspberry Pi CM4 IO Board reference design: supplied unchanged in `cm4-io/`.
- Raspberry Pi CM4 IO USB 3 reference design and application note: preserved in
  `reference/CM4IOUSB3/` and `reference/cm4iousb3-appnote.pdf`.
- TI USB-PD-CHG-EVM-01 design guide: `reference/TI_USB_PD/`.
- TI TPS25751, BQ25798, TPS568230, TUSB320LAI, HD3SS3212 and TPS2553 datasheets.
- TI TVS2200 22 V VBUS surge-protection datasheet.
- TI TLV320AIC3204 datasheet/reference circuit.
- Infineon IM73A135 microphone datasheet (2.3–3.0 V, differential output,
  0.8 mm PCB acoustic port).
- Vishay VCNL36825T datasheet/application note.
- Hirose BM28 manufacturer drawing/model for the compact 5 A battery-flex
  interface: `https://www.hirose.com/en/product/p/CL0673-5040-0-53`.
- Hirose DF40C-100DP-0.4V(51) manufacturer product data, land pattern and official
  STEP model for the 100-position tracking-board connector:
  `https://www.hirose.com/product/p/CL0684-4032-1-51?lang=en`.
- Same Sky SJ-4351X-SMT mechanical drawing.
- JAE DX07S016JA1R1500 and DX07S024JJ2R1300 product data and mechanical drawings.
  JLC/LCSC assembly identifiers are C3197885 and C2977282 respectively.
- Official KiCad `kicad-packages3D` repository, localized by
  `tools/fetch_kicad_models.ps1` for repeatable renders.
- Same Sky official SJ-43514-SMT-TR STEP download:
  `https://www.sameskydevices.com/product/interconnect/connectors/audio-connectors/jacks/sj-43514-smt-tr`.
- JAE DX07S024JJ2R1300 public manufacturer-backed CAD/land pattern:
  `https://www.lcsc.com/product-detail/C2977282.html`.
- Exact TI package previews used by the reproducible
  `tools/fetch_verified_preview_models.py` mapping.
- Bourns SRP5030CA-1R0M official datasheet (1 uH, 9 A, 5.5 x 5.3 x 2.9 mm),
  LCSC C2049204: `https://www.bourns.com/docs/product-datasheets/srp5030ca.pdf`.
- Bourns SRP7050TA-2R2M official datasheet (2.2 uH, 10 A, 6.7 x 6.6 x 4.8 mm),
  LCSC C2045424: `https://www.bourns.com/docs/product-datasheets/srp7050ta.pdf`.
- Hirose BM28B0.6-20DS/2-0.35V(51) official product drawing and STEP model for
  the button-flex carrier mate:
  `https://www.hirose.com/en/product/p/CL0673-5040-0-51`.
- TOGIALED TJ-S3227SW1TCGLCCYRGB-A5 common-anode RGB LED, LCSC/JLCPCB
  `C601674`: `https://www.lcsc.com/product-detail/C601674.html`.

## Known source limitation

Renesas publishes the UPD720202 product data publicly but gates the detailed hardware
user manual/reference schematic.  The controller block therefore remains a review
gate even though the exact IC, package, PCIe topology, 24 MHz clock, 3.3 V rail and
1.05 V rail are placed. Do not release Gerbers until the pin straps, sequencing and
all passive values are checked against that manual.
