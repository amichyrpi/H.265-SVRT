"""Convert a manufacturer GLB preview into a KiCad-compatible VRML model.

The source GLB uses metres.  KiCad's legacy VRML importer uses 0.1-inch
(2.54 mm) model units, so the mesh is scaled explicitly during export.
This preserves the connector's real tessellated geometry; it does not create
or substitute a package-envelope box.
"""

from pathlib import Path
import sys

import trimesh


def write_wrl(source: Path, destination: Path) -> None:
    scene = trimesh.load(source, force="scene")
    mesh = scene.to_geometry()
    if not isinstance(mesh, trimesh.Trimesh) or mesh.is_empty:
        raise RuntimeError(f"No mesh geometry found in {source}")

    scale = 1000.0 / 2.54  # metres -> millimetres -> KiCad VRML units
    vertices = mesh.vertices * scale

    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("w", encoding="ascii", newline="\n") as output:
        output.write("#VRML V2.0 utf8\n")
        output.write("Shape {\n")
        output.write(
            "  appearance Appearance { material Material { "
            "diffuseColor 0.68 0.70 0.72 specularColor 0.35 0.35 0.35 "
            "shininess 0.55 } }\n"
        )
        output.write("  geometry IndexedFaceSet {\n")
        output.write("    solid FALSE\n")
        output.write("    coord Coordinate { point [\n")
        for x, y, z in vertices:
            output.write(f"      {x:.8g} {y:.8g} {z:.8g},\n")
        output.write("    ] }\n")
        output.write("    coordIndex [\n")
        for a, b, c in mesh.faces:
            output.write(f"      {a}, {b}, {c}, -1,\n")
        output.write("    ]\n")
        output.write("  }\n")
        output.write("}\n")

    extent_mm = mesh.extents * 1000.0
    print(
        f"Wrote {destination}: {len(vertices)} vertices, {len(mesh.faces)} faces, "
        f"{extent_mm[0]:.2f} x {extent_mm[1]:.2f} x {extent_mm[2]:.2f} mm"
    )


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: convert_glb_to_kicad_wrl.py SOURCE.glb DESTINATION.wrl")
    write_wrl(Path(sys.argv[1]), Path(sys.argv[2]))
