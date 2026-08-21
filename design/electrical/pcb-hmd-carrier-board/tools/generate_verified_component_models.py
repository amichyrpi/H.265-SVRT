"""Generate only manufacturer-drawing-derived component CAD.

These models are not generic package envelopes.  They either normalize the
coordinate system of official manufacturer CAD or reproduce the documented
mechanical drawing where no redistributable vendor STEP is published.
Run with CadQuery on PYTHONPATH (see README).
"""

from pathlib import Path

import cadquery as cq


ROOT = Path(__file__).resolve().parents[1]


def rounded_box(x, y, z, radius):
    solid = cq.Workplane("XY").box(x, y, z, centered=(True, True, False))
    return solid.edges("|Z").fillet(radius)


def normalize_same_sky_jack() -> None:
    source = ROOT / "libraries/3dmodels/exact/SameSky/Same_Sky_SJ-43514-SMT-TR.step"
    destination = source.with_name("Same_Sky_SJ-43514-SMT-TR_KiCad.step")
    # Vendor axes: X=width, Y=height, Z=-insertion depth.  KiCad axes:
    # X=insertion depth into the PCB, Y=width, Z=height above F.Cu.
    shape = cq.importers.importStep(str(source))
    shape = shape.rotate((0, 0, 0), (1, 0, 0), -90)
    shape = shape.rotate((0, 0, 0), (0, 0, 1), 90)
    shape = shape.translate((0, 0, 2.5001))
    cq.exporters.export(shape, str(destination))
    print(f"Wrote normalized official CAD: {destination}")


def make_im73a135() -> None:
    """Infineon PG-LLGA-5-2, 4.0 x 3.0 x 1.28 mm, bottom-port."""
    destination = ROOT / "libraries/3dmodels/exact/Infineon/IM73A135V01.step"
    destination.parent.mkdir(parents=True, exist_ok=True)
    substrate = rounded_box(4.0, 3.0, 0.28, 0.16)
    lid = rounded_box(3.82, 2.82, 1.00, 0.18).translate((0, 0, 0.28))
    # The acoustic port is on the land side.  Its opening is modeled through
    # the substrate to make the package visibly distinct from a plain block.
    port = cq.Workplane("XY").center(-1.15, 0).circle(0.35).extrude(0.34)
    substrate = substrate.cut(port)
    assembly = cq.Assembly(name="IM73A135V01")
    assembly.add(substrate, name="substrate", color=cq.Color(0.08, 0.08, 0.08))
    assembly.add(lid, name="metal_lid", color=cq.Color(0.72, 0.74, 0.76))
    assembly.save(str(destination))
    print(f"Wrote datasheet-derived microphone CAD: {destination}")


def make_b6ps_vh() -> None:
    """JST B6PS-VH right-angle 6-way header from JST VH drawing dimensions."""
    destination = ROOT / "libraries/3dmodels/exact/JST/B6PS-VH.step"
    destination.parent.mkdir(parents=True, exist_ok=True)

    # Pin 1 is at X=0/Y=0; the six contacts are on a 3.96 mm pitch.
    housing = (
        cq.Workplane("XY")
        .box(23.70, 10.90, 8.70, centered=(True, True, False))
        .translate((9.90, 9.45, 0))
        .edges("|Z")
        .fillet(0.35)
    )
    for pin_x in (0.0, 3.96, 7.92, 11.88, 15.84, 19.80):
        opening = (
            cq.Workplane("XY")
            .box(2.55, 5.5, 5.1, centered=(True, True, False))
            .translate((pin_x, 4.1, 1.7))
        )
        housing = housing.cut(opening)

    assembly = cq.Assembly(name="JST_B6PS_VH")
    assembly.add(housing, name="housing", color=cq.Color(0.93, 0.92, 0.84))
    for index, pin_x in enumerate((0.0, 3.96, 7.92, 11.88, 15.84, 19.80), 1):
        vertical = cq.Workplane("XY").box(0.64, 0.64, 4.0).translate((pin_x, 0, 2.0))
        horizontal = (
            cq.Workplane("XY")
            .box(0.64, 7.0, 0.64)
            .translate((pin_x, 3.5, 3.68))
        )
        assembly.add(
            vertical.union(horizontal),
            name=f"contact_{index}",
            color=cq.Color(0.72, 0.58, 0.25),
        )
    assembly.save(str(destination))
    print(f"Wrote JST-drawing-derived connector CAD: {destination}")


def make_bourns_inductor(
    part: str,
    body_x: float,
    body_y: float,
    body_z: float,
    terminal_x: float,
    terminal_y: float,
    marking: str,
) -> None:
    """Create a visibly accurate molded-power-inductor model from Bourns drawings."""
    destination = ROOT / f"libraries/3dmodels/exact/Bourns/{part}.step"
    destination.parent.mkdir(parents=True, exist_ok=True)

    terminal_z = 0.12
    terminal_offset = body_x / 2 - terminal_x / 2
    body = rounded_box(body_x, body_y, body_z - terminal_z, 0.32).translate(
        (0, 0, terminal_z)
    )
    body = body.edges(">Z").chamfer(min(0.18, body_z / 12))
    left = (
        cq.Workplane("XY")
        .box(terminal_x, terminal_y, terminal_z, centered=(True, True, False))
        .translate((-terminal_offset, 0, 0))
    )
    right = left.mirror("YZ")
    top_mark = (
        cq.Workplane("XY")
        .text(marking, min(body_x, body_y) * 0.27, 0.04, halign="center", valign="center")
        .translate((0, 0, body_z - 0.01))
    )

    assembly = cq.Assembly(name=part)
    assembly.add(body, name="molded_body", color=cq.Color(0.16, 0.16, 0.17))
    assembly.add(left, name="terminal_1", color=cq.Color(0.72, 0.73, 0.74))
    assembly.add(right, name="terminal_2", color=cq.Color(0.72, 0.73, 0.74))
    assembly.add(top_mark, name="top_marking", color=cq.Color(0.72, 0.72, 0.68))
    assembly.save(str(destination))
    print(f"Wrote Bourns-drawing-derived inductor CAD: {destination}")


def make_bourns_inductors() -> None:
    # SRP5030CA: 5.5 x 5.3 x 2.9 mm, 1.1 x 4.3 mm terminals.
    make_bourns_inductor("SRP5030CA-1R0M", 5.5, 5.3, 2.9, 1.1, 4.3, "1R0")
    # SRP7050TA: 6.7 x 6.6 x 4.8 mm, 1.8 x 3.0 mm terminals.
    make_bourns_inductor("SRP7050TA-2R2M", 6.7, 6.6, 4.8, 1.8, 3.0, "2R2")


if __name__ == "__main__":
    normalize_same_sky_jack()
    make_im73a135()
    make_b6ps_vh()
    make_bourns_inductors()
