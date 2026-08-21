"""Extract the authoritative Board2 footprint/pad/net data through pcbnew.

The JSON is an auditable intermediate used by recover_board2_schematic.py.  The
PCB remains the authority: this tool never changes it.
"""

import json
from pathlib import Path

import pcbnew


ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / "pcb-hmd-carrier-board2.kicad_pcb"
OUTPUT = ROOT / "output" / "production-audit" / "Board2-Electrical-Authority.json"
RECOVERED_FOOTPRINTS = ROOT / "libraries" / "footprints" / "Board2_Recovered.pretty"


def text_field(fp, name):
    # KiCad 10's Python API exposes footprint fields as a deque rather than
    # providing GetFieldByName().  Iterating also preserves custom production
    # fields such as Manufacturer and MPN in the recovered hierarchy.
    for field in fp.GetFields():
        if field.GetName() == name:
            return field.GetText()
    return ""


def main():
    board = pcbnew.LoadBoard(str(BOARD))
    RECOVERED_FOOTPRINTS.mkdir(parents=True, exist_ok=True)
    footprint_io = pcbnew.PCB_IO_KICAD_SEXPR()
    rows = []
    exported_names = set()
    for fp in sorted(board.GetFootprints(), key=lambda item: item.GetReference()):
        pads = []
        grouped = {}
        for pad in fp.Pads():
            number = pad.GetNumber()
            net_name = pad.GetNetname() or ""
            grouped.setdefault(number, set()).add(net_name)
        for number, net_names in sorted(grouped.items(), key=lambda item: item[0]):
            nonempty = sorted(name for name in net_names if name)
            if len(nonempty) > 1:
                raise RuntimeError(
                    f"{fp.GetReference()} pad {number} has conflicting nets: {nonempty}"
                )
            pads.append({"number": number, "net": nonempty[0] if nonempty else ""})
        fpid = fp.GetFPID()
        nickname = str(fpid.GetLibNickname())
        item_name = str(fpid.GetLibItemName())
        footprint_id = f"{nickname}:{item_name}" if nickname else item_name
        local_footprint = RECOVERED_FOOTPRINTS / f"{item_name}.kicad_mod"
        if item_name and item_name not in exported_names:
            if not local_footprint.exists():
                # Existing local footprints are controlled library assets.  Do
                # not overwrite a reviewed/local-coordinate footprint from a
                # placed board instance (footprint keepout zones may otherwise
                # be written using absolute board coordinates).
                footprint_io.FootprintSave(str(RECOVERED_FOOTPRINTS), fp)
            exported_names.add(item_name)
        rows.append({
            "reference": fp.GetReference(),
            "value": fp.GetValue(),
            "footprint": f"Board2_Recovered:{item_name}" if item_name else footprint_id,
            "datasheet": text_field(fp, "Datasheet") or "~",
            "manufacturer": text_field(fp, "Manufacturer"),
            "mpn": text_field(fp, "MPN"),
            "supplier": text_field(fp, "Supplier"),
            "supplier_part": text_field(fp, "Supplier Part"),
            "dnp": bool(fp.IsDNP()),
            "excluded_bom": bool(fp.IsExcludedFromBOM()),
            "excluded_board": bool(fp.IsExcludedFromPosFiles()),
            "pads": pads,
        })
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(json.dumps({"board": BOARD.name, "footprints": rows}, indent=2), encoding="utf-8")
    print(f"Extracted {len(rows)} footprints to {OUTPUT}")


if __name__ == "__main__":
    main()
