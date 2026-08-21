"""Generate the local mechanical model for the CM4 Lite microSD socket.

The model follows the Molex 503398-1892 sales drawing envelope and is used
with Raspberry Pi's official CM4 IO Board land pattern.  It is intentionally
mechanical (not a photorealistic vendor model), but includes the stamped shell,
card mouth, insulator and contact region so orientation can be checked in the
KiCad 3D viewer.
"""

from pathlib import Path

import cadquery as cq


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "libraries/3dmodels/Molex"


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)

    # Drawing maximum envelope: 13.10 x 14.05 x 1.28 mm.  The footprint
    # origin is centred on the shell outline, with the card mouth at -Y.
    shell = cq.Workplane("XY").box(13.10, 14.05, 0.18).translate((0, 0, 1.19))
    # Stamped inspection windows and latch slots keep the shell from rendering
    # as an anonymous cuboid and match the construction shown in the drawing.
    for x in (-4.6, -1.55, 1.55, 4.6):
        shell = shell.cut(cq.Workplane("XY").box(1.45, 3.15, 0.40).translate((x, -1.0, 1.19)))
    shell = shell.cut(cq.Workplane("XY").box(4.6, 1.2, 0.40).translate((1.6, 4.65, 1.19)))
    roof = cq.Workplane("XY").box(12.30, 11.70, 0.12).translate((0, 0.75, 1.04))
    left_rail = cq.Workplane("XY").box(0.45, 12.70, 1.10).translate((-6.30, 0.30, 0.55))
    right_rail = cq.Workplane("XY").box(0.45, 12.70, 1.10).translate((6.30, 0.30, 0.55))
    rear_wall = cq.Workplane("XY").box(12.65, 0.40, 1.10).translate((0, 6.75, 0.55))
    metal = shell.union(roof).union(left_rail).union(right_rail).union(rear_wall)

    insulator = cq.Workplane("XY").box(11.85, 2.15, 0.46).translate((0, -5.45, 0.27))
    mouth_top = cq.Workplane("XY").box(11.60, 0.24, 0.38).translate((0, -6.75, 0.78))
    contacts = None
    for index in range(8):
        x = -3.85 + index * 1.10
        contact = cq.Workplane("XY").box(0.42, 3.55, 0.10).translate((x, -3.85, 0.12))
        contacts = contact if contacts is None else contacts.union(contact)

    # Card-detect actuator on the right edge is an important orientation cue.
    detect = cq.Workplane("XY").box(0.40, 2.50, 0.34).translate((5.85, 3.65, 0.32))
    assembly = cq.Assembly(name="Molex_503398-1892")
    assembly.add(metal, name="stamped_shell", color=cq.Color(0.72, 0.74, 0.76))
    assembly.add(insulator, name="insulator", color=cq.Color(0.06, 0.06, 0.07))
    assembly.add(mouth_top, name="card_mouth", color=cq.Color(0.10, 0.10, 0.11))
    assembly.add(contacts, name="contacts", color=cq.Color(0.78, 0.58, 0.16))
    assembly.add(detect, name="detect_spring", color=cq.Color(0.78, 0.58, 0.16))

    assembly.save(str(OUT / "Molex_503398-1892.step"), exportType="STEP")
    print(OUT / "Molex_503398-1892.step")


if __name__ == "__main__":
    main()
