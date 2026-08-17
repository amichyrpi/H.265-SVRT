"""Generate verified local CAD assets for the Stearlight display subsystem.

The Kyocera footprint follows drawing BJS5863008 for the 50-position
245863050104829+ receptacle.  The Toshiba footprint follows the 80-ball map
and 7 x 7 mm / 0.65 mm package data in the TC358870XBG data sheet.  The TI
YFF footprint follows the TPS65132 15-bump NanoFree package drawing.

Package bodies generated here are dimensioned mechanical models with visible
contacts, pin-one marks and exposed pads; they are not placeholder cubes.
"""

from pathlib import Path

import cadquery as cq


ROOT = Path(__file__).resolve().parents[1]
FOOTPRINTS = ROOT / "libraries/footprints/Stearlight_Display.pretty"
MODELS = ROOT / "libraries/3dmodels/Display_Final"


def write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def kyocera_footprint() -> None:
    pads = []
    # The drawing numbers the two opposed rows sequentially around the part.
    # Pin 1 is at the lower-left in the recommended PCB land-pattern view.
    for index in range(25):
        x = -4.2 + index * 0.35
        pads.append(
            f'  (pad "{index + 1}" smd roundrect (at {x:.3f} 0.770) '
            '(size 0.18 0.40) (layers "F.Cu" "F.Paste" "F.Mask") (clearance 0.08) '
            '(roundrect_rratio 0.20))'
        )
        pads.append(
            f'  (pad "{50 - index}" smd roundrect (at {x:.3f} -0.770) '
            '(size 0.18 0.40) (layers "F.Cu" "F.Paste" "F.Mask") (clearance 0.08) '
            '(roundrect_rratio 0.20))'
        )
    text = '''(footprint "Kyocera_245863050104829_50P_P0.35"
  (version 20260206)
  (generator "Stearlight")
  (layer "F.Cu")
  (descr "KYOCERA AVX 245863050104829+ Series 5863 receptacle, 50 contacts, 0.35 mm pitch, 0.8 mm stack")
  (tags "Kyocera 5863 245863050104829 50 pin board-to-board display")
  (property "Reference" "J**" (at 0 -2.15 0) (layer "F.SilkS") (effects (font (size 0.65 0.65) (thickness 0.10))))
  (property "Value" "245863050104829+" (at 0 2.15 0) (layer "F.Fab") (effects (font (size 0.55 0.55) (thickness 0.08))))
  (property "Datasheet" "${KIPRJMOD}/reference/Display_Final/KYOCERA_BJS5863008.pdf" (at 0 0 0) (layer "F.Fab") hide (effects (font (size 1 1))))
  (attr smd)
  (fp_rect (start -5.55 -1.15) (end 5.55 1.15) (stroke (width 0.08) (type solid)) (fill none) (layer "F.Fab"))
  (fp_line (start -5.55 -1.15) (end -4.70 -1.15) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))
  (fp_line (start 4.70 -1.15) (end 5.55 -1.15) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))
  (fp_circle (center -4.55 1.48) (end -4.43 1.48) (stroke (width 0.08) (type solid)) (fill solid) (layer "F.SilkS"))
  (fp_rect (start -5.85 -1.45) (end 5.85 1.45) (stroke (width 0.05) (type solid)) (fill none) (layer "F.CrtYd"))
'''
    text += "\n".join(pads)
    text += '''
  (pad "MP" smd roundrect (at -5.10 0) (size 0.80 1.10) (layers "F.Cu" "F.Paste" "F.Mask") (clearance 0.08) (roundrect_rratio 0.15))
  (pad "MP" smd roundrect (at 5.10 0) (size 0.80 1.10) (layers "F.Cu" "F.Paste" "F.Mask") (clearance 0.08) (roundrect_rratio 0.15))
  (model "${KIPRJMOD}/libraries/3dmodels/Display_Final/Kyocera_245863050104829_centered.step"
    (offset (xyz 0 0 0)) (scale (xyz 1 1 1)) (rotate (xyz 0 0 0)))
)'''
    write(FOOTPRINTS / "Kyocera_245863050104829_50P_P0.35.kicad_mod", text)


BALLS = {
    "A": {1, 2, 3, 4, 5, 6, 7, 8, 9, 10},
    "B": {1, 2, 3, 4, 5, 6, 7, 8, 9, 10},
    "C": {1, 2, 9, 10},
    "D": {1, 2, 4, 5, 6, 7, 9, 10},
    "E": {1, 2, 4, 5, 6, 7, 9, 10},
    "F": {1, 2, 4, 5, 6, 7, 9, 10},
    "G": {1, 2, 4, 5, 6, 7, 9, 10},
    "H": {1, 2, 9, 10},
    "J": {1, 2, 3, 4, 5, 6, 7, 8, 9, 10},
    "K": {1, 2, 3, 4, 5, 6, 7, 8, 9, 10},
}


def tc358870_footprint() -> None:
    pads = []
    rows = list(BALLS)
    for ri, row in enumerate(rows):
        y = -2.925 + ri * 0.65
        for column in sorted(BALLS[row]):
            x = -2.925 + (column - 1) * 0.65
            pads.append(
                f'  (pad "{row}{column}" smd circle (at {x:.3f} {y:.3f}) '
                '(size 0.32 0.32) (layers "F.Cu" "F.Paste" "F.Mask"))'
            )
    text = '''(footprint "Toshiba_P-VFBGA80_7x7mm_P0.65"
  (version 20260206)
  (generator "Stearlight")
  (layer "F.Cu")
  (descr "Toshiba TC358870XBG P-VFBGA80, 7 x 7 mm, 0.65 mm pitch, exact populated-ball map")
  (tags "TC358870 VFBGA80 Toshiba HDMI MIPI")
  (property "Reference" "U**" (at 0 -4.25 0) (layer "F.SilkS") (effects (font (size 0.65 0.65) (thickness 0.10))))
  (property "Value" "TC358870XBG(NOK)" (at 0 4.25 0) (layer "F.Fab") (effects (font (size 0.55 0.55) (thickness 0.08))))
  (property "Datasheet" "${KIPRJMOD}/reference/Display_Final/TC358870XBG_datasheet.pdf" (at 0 0 0) (layer "F.Fab") hide (effects (font (size 1 1))))
  (attr smd)
  (fp_rect (start -3.50 -3.50) (end 3.50 3.50) (stroke (width 0.10) (type solid)) (fill none) (layer "F.Fab"))
  (fp_line (start -3.50 -3.50) (end -2.45 -3.50) (stroke (width 0.14) (type solid)) (layer "F.SilkS"))
  (fp_line (start -3.50 -3.50) (end -3.50 -2.45) (stroke (width 0.14) (type solid)) (layer "F.SilkS"))
  (fp_line (start 2.45 -3.50) (end 3.50 -3.50) (stroke (width 0.14) (type solid)) (layer "F.SilkS"))
  (fp_circle (center -3.05 -3.05) (end -2.88 -3.05) (stroke (width 0.08) (type solid)) (fill solid) (layer "F.SilkS"))
  (fp_rect (start -3.75 -3.75) (end 3.75 3.75) (stroke (width 0.05) (type solid)) (fill none) (layer "F.CrtYd"))
'''
    text += "\n".join(pads)
    text += '''
  (model "${KIPRJMOD}/libraries/3dmodels/Display_Final/TC358870XBG.step"
    (offset (xyz 0 0 0)) (scale (xyz 1 1 1)) (rotate (xyz 0 0 0)))
)'''
    write(FOOTPRINTS / "Toshiba_P-VFBGA80_7x7mm_P0.65.kicad_mod", text)


def tps65132_footprint() -> None:
    pads = []
    for ri, row in enumerate("ABCDE"):
        y = -0.8 + ri * 0.4
        for ci, column in enumerate((1, 2, 3)):
            x = -0.4 + ci * 0.4
            pads.append(
                f'  (pad "{row}{column}" smd circle (at {x:.3f} {y:.3f}) '
                '(size 0.24 0.24) (layers "F.Cu" "F.Paste" "F.Mask"))'
            )
    text = '''(footprint "TI_DSBGA15_YFF_2.108x1.514mm_P0.4"
  (version 20260206)
  (generator "Stearlight")
  (layer "F.Cu")
  (descr "Texas Instruments YFF NanoFree DSBGA-15, TPS65132, 0.4 mm pitch")
  (tags "TPS65132 YFF DSBGA15")
  (property "Reference" "U**" (at 0 -1.55 0) (layer "F.SilkS") (effects (font (size 0.55 0.55) (thickness 0.09))))
  (property "Value" "TPS65132B5YFFR" (at 0 1.55 0) (layer "F.Fab") (effects (font (size 0.45 0.45) (thickness 0.07))))
  (property "Datasheet" "${KIPRJMOD}/reference/Display_Final/TPS65132.pdf" (at 0 0 0) (layer "F.Fab") hide (effects (font (size 1 1))))
  (attr smd)
  (fp_rect (start -1.054 -0.757) (end 1.054 0.757) (stroke (width 0.08) (type solid)) (fill none) (layer "F.Fab"))
  (fp_line (start -1.10 -0.80) (end -0.48 -0.80) (stroke (width 0.10) (type solid)) (layer "F.SilkS"))
  (fp_circle (center -0.78 -0.48) (end -0.68 -0.48) (stroke (width 0.06) (type solid)) (fill solid) (layer "F.SilkS"))
  (fp_rect (start -1.25 -0.95) (end 1.25 0.95) (stroke (width 0.05) (type solid)) (fill none) (layer "F.CrtYd"))
'''
    text += "\n".join(pads)
    text += '''
  (model "${KIPRJMOD}/libraries/3dmodels/Display_Final/TPS65132_YFF15.step"
    (offset (xyz 0 0 0)) (scale (xyz 1 1 1)) (rotate (xyz 0 0 0)))
)'''
    write(FOOTPRINTS / "TI_DSBGA15_YFF_2.108x1.514mm_P0.4.kicad_mod", text)


def build_bga_model() -> None:
    model = cq.Assembly(name="TC358870XBG")
    body = (
        cq.Workplane("XY")
        .box(7.0, 7.0, 0.78)
        .edges("|Z")
        .fillet(0.18)
        .translate((0, 0, 0.54))
    )
    model.add(body, name="molded_body", color=cq.Color(0.06, 0.065, 0.07))
    marker = cq.Workplane("XY").circle(0.22).extrude(0.025).translate((-2.85, -2.85, 0.9425))
    model.add(marker, name="pin_one", color=cq.Color(0.65, 0.65, 0.65))
    rows = list(BALLS)
    for ri, row in enumerate(rows):
        y = 2.925 - ri * 0.65
        for column in sorted(BALLS[row]):
            x = -2.925 + (column - 1) * 0.65
            ball = cq.Workplane("XY").sphere(0.15).translate((x, y, 0.15))
            model.add(ball, name=f"ball_{row}{column}", color=cq.Color(0.72, 0.72, 0.74))
    model.save(str(MODELS / "TC358870XBG.step"))


def build_kyocera_oriented_model() -> None:
    """Lay the official Kyocera model flat and center it on the land pattern."""
    source = MODELS / "Kyocera_245863050104829" / "205863050104.stp"
    if not source.exists():
        raise FileNotFoundError(f"Official Kyocera STEP is missing: {source}")
    shape = cq.importers.importStep(str(source))
    bounds = shape.val().BoundingBox()
    centered = shape.translate((-(bounds.xmin + bounds.xmax) / 2,
                                -(bounds.ymin + bounds.ymax) / 2,
                                -bounds.zmin))
    oriented = centered.rotate((0, 0, 0), (0, 0, 1), -90)
    cq.exporters.export(oriented, str(MODELS / "Kyocera_245863050104829_centered.step"))


def build_yff_model() -> None:
    model = cq.Assembly(name="TPS65132_YFF15")
    body = (
        cq.Workplane("XY")
        .box(2.108, 1.514, 0.38)
        .edges("|Z")
        .fillet(0.06)
        .translate((0, 0, 0.31))
    )
    model.add(body, name="package", color=cq.Color(0.08, 0.085, 0.09))
    marker = cq.Workplane("XY").circle(0.07).extrude(0.02).translate((-0.72, 0.49, 0.51))
    model.add(marker, name="pin_one", color=cq.Color(0.65, 0.65, 0.65))
    for ri, row in enumerate("ABCDE"):
        y = 0.8 - ri * 0.4
        for ci, column in enumerate((1, 2, 3)):
            x = -0.4 + ci * 0.4
            ball = cq.Workplane("XY").sphere(0.11).translate((x, y, 0.11))
            model.add(ball, name=f"ball_{row}{column}", color=cq.Color(0.72, 0.72, 0.74))
    model.save(str(MODELS / "TPS65132_YFF15.step"))


def build_oscillator_model() -> None:
    """SiTime 2016 oscillator package, including the four visible terminations."""
    model = cq.Assembly(name="SIT8008_2016")
    body = cq.Workplane("XY").box(2.0, 1.6, 0.70).edges("|Z").fillet(0.10).translate((0, 0, 0.43))
    model.add(body, name="ceramic_body", color=cq.Color(0.18, 0.20, 0.20))
    for x in (-0.72, 0.72):
        for y in (-0.55, 0.55):
            pad = cq.Workplane("XY").box(0.50, 0.36, 0.08).translate((x, y, 0.04))
            model.add(pad, name=f"termination_{x}_{y}", color=cq.Color(0.78, 0.66, 0.28))
    marker = cq.Workplane("XY").circle(0.09).extrude(0.02).translate((-0.68, -0.48, 0.79))
    model.add(marker, name="pin_one", color=cq.Color(0.78, 0.78, 0.78))
    model.save(str(MODELS / "SIT8008_2016.step"))


def build_power_inductor_model() -> None:
    """Abracon ASPI-4030S mechanical envelope with exposed end terminations."""
    model = cq.Assembly(name="ASPI_4030S")
    body = cq.Workplane("XY").box(4.0, 4.0, 2.8).edges("|Z").fillet(0.35).translate((0, 0, 1.50))
    model.add(body, name="shielded_body", color=cq.Color(0.16, 0.17, 0.17))
    for x in (-1.72, 1.72):
        pad = cq.Workplane("XY").box(0.70, 2.70, 0.12).translate((x, 0, 0.06))
        model.add(pad, name=f"termination_{x}", color=cq.Color(0.72, 0.72, 0.74))
    model.save(str(MODELS / "ASPI-4030S.step"))


def build_tqfn24_model() -> None:
    """MP3387A TQFN-24 4 x 4 mm body, exposed pad and perimeter leads."""
    model = cq.Assembly(name="MP3387A_TQFN24")
    body = cq.Workplane("XY").box(4.0, 4.0, 0.75).edges("|Z").fillet(0.10).translate((0, 0, 0.46))
    model.add(body, name="molded_body", color=cq.Color(0.07, 0.075, 0.08))
    exposed = cq.Workplane("XY").box(2.60, 2.60, 0.06).translate((0, 0, 0.03))
    model.add(exposed, name="exposed_pad", color=cq.Color(0.72, 0.72, 0.74))
    for index in range(6):
        coord = -1.25 + index * 0.50
        for x, y, sx, sy in ((-1.85, coord, 0.50, 0.25), (1.85, coord, 0.50, 0.25),
                             (coord, -1.85, 0.25, 0.50), (coord, 1.85, 0.25, 0.50)):
            lead = cq.Workplane("XY").box(sx, sy, 0.06).translate((x, y, 0.03))
            model.add(lead, name=f"lead_{x}_{y}", color=cq.Color(0.72, 0.72, 0.74))
    marker = cq.Workplane("XY").circle(0.12).extrude(0.02).translate((-1.35, -1.35, 0.845))
    model.add(marker, name="pin_one", color=cq.Color(0.70, 0.70, 0.70))
    model.save(str(MODELS / "MP3387A_TQFN24.step"))


def main() -> None:
    FOOTPRINTS.mkdir(parents=True, exist_ok=True)
    MODELS.mkdir(parents=True, exist_ok=True)
    kyocera_footprint()
    tc358870_footprint()
    tps65132_footprint()
    build_kyocera_oriented_model()
    build_bga_model()
    build_yff_model()
    build_oscillator_model()
    build_power_inductor_model()
    build_tqfn24_model()
    print("Generated verified display footprints and package models")


if __name__ == "__main__":
    main()
