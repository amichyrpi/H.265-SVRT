"""Generate the exact button-flex mate and RGB status LED CAD assets.

The Hirose model already stored in this project is the manufacturer STEP. The
connector footprint below uses the official odd/even contact numbering and the
two dedicated power-contact numbers used by its BM28 DP mate.

The RGB LED model is dimensioned from the TOGIALED TJ-S3227SW1TCGLCCYRGB-A5
datasheet (3.2 x 2.7 x 1.1 mm); it is not a generic placeholder cube.
"""

from pathlib import Path

import cadquery as cq


ROOT = Path(__file__).resolve().parents[1]
FOOTPRINTS = ROOT / "libraries/footprints/Stearlight.pretty"
MODELS = ROOT / "libraries/3dmodels/Button_Status"


def write_connector_footprint() -> None:
    signal_pads = []
    for index in range(10):
        x = -1.575 + index * 0.35
        signal_pads.append(
            f'  (pad "{1 + index * 2}" smd rect (at {x:.3f} -0.775) '
            '(size 0.18 0.65) (layers "F.Cu" "F.Paste" "F.Mask") (clearance 0.08))'
        )
        signal_pads.append(
            f'  (pad "{2 + index * 2}" smd rect (at {x:.3f} 0.775) '
            '(size 0.18 0.65) (layers "F.Cu" "F.Paste" "F.Mask") (clearance 0.08))'
        )

    footprint = '''(footprint "Hirose_BM28B0.6-20DS_2-0.35V_BUTTONS"
  (version 20260206)
  (generator "Stearlight")
  (layer "F.Cu")
  (descr "Exact carrier mate for BM28B0.6-20DP/2-0.35V(51); official odd/even numbering and two power contacts")
  (tags "Hirose BM28 DS 20 signal 2 power buttons")
  (property "Reference" "REF**" (at 0 -2.0 0) (layer "F.SilkS") (effects (font (size 0.65 0.65) (thickness 0.10))))
  (property "Value" "BM28B0.6-20DS/2-0.35V(51)" (at 0 2.0 0) (layer "F.Fab") (effects (font (size 0.55 0.55) (thickness 0.08))))
  (property "Datasheet" "${KIPRJMOD}/reference/Hirose_BM28B0.6-20DS_drawing.pdf" (at 0 0 0) (layer "F.Fab") hide (effects (font (size 1 1))))
  (attr smd)
  (fp_rect (start -3.05 -0.85) (end 3.05 0.85) (stroke (width 0.10) (type solid)) (fill none) (layer "F.Fab"))
  (fp_rect (start -3.30 -1.10) (end 3.30 1.10) (stroke (width 0.05) (type solid)) (fill none) (layer "F.CrtYd"))
  (fp_line (start -3.05 -0.85) (end -2.00 -0.85) (stroke (width 0.10) (type solid)) (layer "F.SilkS"))
  (fp_line (start 2.00 -0.85) (end 3.05 -0.85) (stroke (width 0.10) (type solid)) (layer "F.SilkS"))
  (fp_circle (center -1.575 -1.55) (end -1.475 -1.55) (stroke (width 0.08) (type solid)) (fill solid) (layer "F.SilkS"))
'''
    footprint += "\n".join(signal_pads) + "\n"
    footprint += '''  (pad "21" smd rect (at -2.55 0) (size 0.75 0.90) (layers "F.Cu" "F.Paste" "F.Mask"))
  (pad "22" smd rect (at 2.55 0) (size 0.75 0.90) (layers "F.Cu" "F.Paste" "F.Mask"))
  (model "${KIPRJMOD}/libraries/3dmodels/BM28/BM28-20DS.stp" (offset (xyz 0 0 0)) (scale (xyz 1 1 1)) (rotate (xyz 0 0 0)))
)'''
    (FOOTPRINTS / "Hirose_BM28B0.6-20DS_2-0.35V_BUTTONS.kicad_mod").write_text(
        footprint, encoding="utf-8"
    )


def write_led_footprint() -> None:
    footprint = '''(footprint "TOGIALED_TJ-S3227SW1TCGLCCYRGB-A5"
  (version 20260206)
  (generator "Stearlight")
  (layer "F.Cu")
  (descr "TOGIALED TJ-S3227SW1TCGLCCYRGB-A5 common-anode RGB LED, 3.2x2.7x1.1 mm; C601674")
  (tags "RGB LED common anode SMD3227 JLCPCB C601674")
  (property "Reference" "D**" (at 0 -2.25 0) (layer "F.SilkS") (effects (font (size 0.65 0.65) (thickness 0.10))))
  (property "Value" "TJ-S3227SW1TCGLCCYRGB-A5" (at 0 2.25 0) (layer "F.Fab") (effects (font (size 0.55 0.55) (thickness 0.08))))
  (property "Datasheet" "${KIPRJMOD}/reference/TOGIALED_TJ-S3227SW1TCGLCCYRGB-A5.pdf" (at 0 0 0) (layer "F.Fab") hide (effects (font (size 1 1))))
  (attr smd)
  (fp_rect (start -1.60 -1.35) (end 1.60 1.35) (stroke (width 0.10) (type solid)) (fill none) (layer "F.Fab"))
  (fp_line (start -1.60 -1.35) (end -0.50 -1.35) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))
  (fp_line (start 0.50 -1.35) (end 1.60 -1.35) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))
  (fp_line (start -1.60 1.35) (end 1.60 1.35) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))
  (fp_circle (center 1.75 -1.40) (end 1.85 -1.40) (stroke (width 0.08) (type solid)) (fill solid) (layer "F.SilkS"))
  (fp_rect (start -2.15 -1.70) (end 2.15 1.70) (stroke (width 0.05) (type solid)) (fill none) (layer "F.CrtYd"))
  (pad "1" smd roundrect (at 1.40 -0.725) (size 1.20 0.90) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.12))
  (pad "2" smd roundrect (at -1.40 -0.725) (size 1.20 0.90) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.12))
  (pad "3" smd roundrect (at -1.40 0.725) (size 1.20 0.90) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.12))
  (pad "4" smd roundrect (at 1.40 0.725) (size 1.20 0.90) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.12))
  (model "${KIPRJMOD}/libraries/3dmodels/Button_Status/TOGIALED_TJ-S3227_RGB.step" (offset (xyz 0 0 0)) (scale (xyz 1 1 1)) (rotate (xyz 0 0 0)))
)'''
    (FOOTPRINTS / "TOGIALED_TJ-S3227SW1TCGLCCYRGB-A5.kicad_mod").write_text(
        footprint, encoding="utf-8"
    )


def write_led_model() -> None:
    MODELS.mkdir(parents=True, exist_ok=True)
    output = MODELS / "TOGIALED_TJ-S3227_RGB.step"

    assembly = cq.Assembly(name="TOGIALED_TJ-S3227_RGB")
    substrate = cq.Workplane("XY").box(3.2, 2.7, 0.50).translate((0, 0, 0.25))
    lens = (
        cq.Workplane("XY")
        .box(2.0, 2.1, 0.50)
        .edges("|Z")
        .fillet(0.12)
        .translate((0, 0, 0.75))
    )
    # Four exposed lead-frame areas align with the manufacturer land pattern.
    leads = [
        cq.Workplane("XY").box(0.75, 0.65, 0.08).translate((x, y, 0.04))
        for x in (-1.40, 1.40)
        for y in (-0.725, 0.725)
    ]
    marker = cq.Workplane("XY").circle(0.10).extrude(0.025).translate((1.25, -0.90, 1.0125))

    assembly.add(substrate, name="white_substrate", color=cq.Color(0.90, 0.90, 0.86))
    assembly.add(lens, name="clear_lens", color=cq.Color(0.76, 0.89, 0.94, 0.55))
    for index, lead in enumerate(leads):
        assembly.add(lead, name=f"lead_{index + 1}", color=cq.Color(0.75, 0.75, 0.77))
    assembly.add(marker, name="pin_1_marker", color=cq.Color(0.12, 0.12, 0.12))
    assembly.save(str(output))


def main() -> None:
    FOOTPRINTS.mkdir(parents=True, exist_ok=True)
    write_connector_footprint()
    write_led_footprint()
    write_led_model()
    print("Generated button-connector and RGB-indicator CAD assets")


if __name__ == "__main__":
    main()
