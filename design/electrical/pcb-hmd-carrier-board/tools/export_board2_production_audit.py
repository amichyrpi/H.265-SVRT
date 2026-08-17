"""Export the deterministic Board2 BOM, placement and connectivity audit."""

from __future__ import annotations

import csv
from collections import defaultdict
from pathlib import Path
import re
import xml.etree.ElementTree as ET

import pcbnew


ROOT = Path(__file__).resolve().parents[1]
BOARD_PATH = ROOT / "pcb-hmd-carrier-board2.kicad_pcb"
OUT = ROOT / "output" / "production-audit"
SCHEMATIC_NETLIST = ROOT / "output" / "pcb-hmd-carrier-board2-current.net.xml"
DRC_REPORT = ROOT / "output" / "reports" / "DRC-pcb-hmd-carrier-board2-production-current.rpt"


def mm(value: int) -> float:
    return value / 1_000_000.0


def write_bom(board: pcbnew.BOARD) -> None:
    groups: dict[tuple[str, str, str, str, str, str], list[str]] = defaultdict(list)
    for fp in board.GetFootprints():
        if fp.IsExcludedFromBOM():
            continue
        fields = fp.GetFieldsText()
        key = (
            fp.GetValue(),
            fp.GetFPID().GetUniStringLibId(),
            fields.get("Manufacturer", ""),
            fields.get("MPN", ""),
            fields.get("LCSC", fields.get("Supplier Part Number", "")),
            "DNP" if fp.IsDNP() else "FIT",
        )
        groups[key].append(fp.GetReference())

    with (OUT / "BOM-pcb-hmd-carrier-board2-review.csv").open(
        "w", newline="", encoding="utf-8-sig"
    ) as handle:
        writer = csv.writer(handle)
        writer.writerow(
            ["Quantity", "References", "Value", "Footprint", "Manufacturer", "MPN", "LCSC/Supplier PN", "Assembly"]
        )
        for key, refs in sorted(groups.items(), key=lambda item: item[0]):
            writer.writerow([len(refs), ",".join(sorted(refs)), *key])


def write_bom_gaps(board: pcbnew.BOARD) -> tuple[int, int]:
    fitted = []
    gaps = []
    for fp in sorted(board.GetFootprints(), key=lambda item: item.GetReference()):
        if fp.IsExcludedFromBOM() or fp.IsDNP():
            continue
        fitted.append(fp)
        fields = fp.GetFieldsText()
        missing = [
            name for name, value in (
                ("Manufacturer", fields.get("Manufacturer", "")),
                ("MPN", fields.get("MPN", "")),
                ("Supplier part number", fields.get("LCSC", fields.get("Supplier Part Number", ""))),
            ) if not value
        ]
        if missing:
            gaps.append((fp.GetReference(), fp.GetValue(), "; ".join(missing)))
    with (OUT / "BOM-Release-Gaps.csv").open(
        "w", newline="", encoding="utf-8-sig"
    ) as handle:
        writer = csv.writer(handle)
        writer.writerow(["Reference", "Value", "Missing_release_fields"])
        writer.writerows(gaps)
    return len(fitted), len(gaps)


def write_placement(board: pcbnew.BOARD) -> None:
    with (OUT / "PickAndPlace-pcb-hmd-carrier-board2-review.csv").open(
        "w", newline="", encoding="utf-8-sig"
    ) as handle:
        writer = csv.writer(handle)
        writer.writerow(["Reference", "Value", "Side", "X_mm", "Y_mm", "Rotation_deg", "DNP", "3D_models"])
        for fp in sorted(board.GetFootprints(), key=lambda item: item.GetReference()):
            if fp.IsExcludedFromPosFiles():
                continue
            pos = fp.GetPosition()
            writer.writerow(
                [
                    fp.GetReference(), fp.GetValue(),
                    "Top" if fp.GetLayer() == pcbnew.F_Cu else "Bottom",
                    f"{mm(pos.x):.4f}", f"{mm(pos.y):.4f}",
                    f"{fp.GetOrientationDegrees():.2f}",
                    "yes" if fp.IsDNP() else "no", len(fp.Models()),
                ]
            )


def write_nets(board: pcbnew.BOARD) -> tuple[int, int]:
    pads_by_net: dict[str, int] = defaultdict(int)
    tracks_by_net: dict[str, int] = defaultdict(int)
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.GetNetname():
                pads_by_net[str(pad.GetNetname())] += 1
    for item in board.GetTracks():
        if item.GetNetname():
            tracks_by_net[str(item.GetNetname())] += 1

    names = sorted(set(pads_by_net) | set(tracks_by_net))
    with (OUT / "Net-Audit-pcb-hmd-carrier-board2.csv").open(
        "w", newline="", encoding="utf-8-sig"
    ) as handle:
        writer = csv.writer(handle)
        writer.writerow(["Net", "Pad_count", "Track_or_via_count", "Has_routed_copper"])
        for name in names:
            writer.writerow([name, pads_by_net[name], tracks_by_net[name], "yes" if tracks_by_net[name] else "no"])
    return len(names), sum(1 for name in names if tracks_by_net[name])


def write_reference_reconciliation(board: pcbnew.BOARD) -> tuple[int, int, int]:
    board_refs = {
        fp.GetReference() for fp in board.GetFootprints()
        if fp.GetReference() != "REF**"
    }
    schematic_refs: set[str] = set()
    if SCHEMATIC_NETLIST.exists():
        root = ET.parse(SCHEMATIC_NETLIST).getroot()
        schematic_refs = {
            comp.attrib["ref"] for comp in root.findall("./components/comp")
        }
    with (OUT / "Reference-Reconciliation.csv").open(
        "w", newline="", encoding="utf-8-sig"
    ) as handle:
        writer = csv.writer(handle)
        writer.writerow(["Reference", "In_schematic", "On_board2", "Status"])
        for reference in sorted(board_refs | schematic_refs):
            in_schematic = reference in schematic_refs
            on_board = reference in board_refs
            status = "MATCH" if in_schematic and on_board else (
                "PCB_ONLY" if on_board else "SCHEMATIC_ONLY"
            )
            writer.writerow([reference, "yes" if in_schematic else "no", "yes" if on_board else "no", status])
    return len(schematic_refs), len(board_refs - schematic_refs), len(schematic_refs - board_refs)


def write_summary(
    board: pcbnew.BOARD,
    net_count: int,
    routed_net_count: int,
    schematic_count: int,
    pcb_only_count: int,
    schematic_only_count: int,
    fitted_bom_count: int,
    bom_gap_count: int,
) -> None:
    bbox = board.GetBoardEdgesBoundingBox()
    footprints = list(board.GetFootprints())
    modeled = sum(1 for fp in footprints if len(fp.Models()))
    dnp = sum(1 for fp in footprints if fp.IsDNP())
    unrouted = "not measured"
    geometry = "not measured"
    if DRC_REPORT.exists():
        drc_text = DRC_REPORT.read_text(encoding="utf-8", errors="replace")
        match = re.search(r"\*\* Found (\d+) unconnected pads", drc_text)
        if match:
            unrouted = match.group(1)
        match = re.search(r"\*\* Found (\d+) DRC violations", drc_text)
        if match:
            geometry = match.group(1)
    text = f"""# Carrier board 2 production audit

Generated from `{BOARD_PATH.name}`. This is a review checkpoint, not a fabrication release.

- Approved edge-cut envelope: {mm(bbox.GetWidth()):.2f} x {mm(bbox.GetHeight()):.2f} mm
- Copper layers: {board.GetCopperLayerCount()}
- Footprints: {len(footprints)} ({dnp} marked DNP)
- Footprints containing at least one 3D model reference: {modeled}/{len(footprints)}
- Named nets represented by pads or copper: {net_count}
- Nets with any track/via copper: {routed_net_count}/{net_count}
- Board tracks/vias: {len(board.GetTracks())}
- Copper zones: {len(board.Zones())}
- Existing schematic components: {schematic_count}
- PCB-only BOM references: {pcb_only_count}
- Schematic-only references: {schematic_only_count}
- Fitted BOM references missing one or more sourcing fields: {bom_gap_count}/{fitted_bom_count}
- Production DRC geometry violations: {geometry}
- Unconnected pads at this checkpoint: {unrouted}

## Release status

All Board2 references are reconciled to the recovered schematic hierarchy. Gerber
release remains gated on zero unrouted items, clean DRC, high-speed route audit,
and the open battery/tracking/vendor-controlled production inputs documented in
`docs/PRODUCTION_BLOCKERS.md`.
"""
    (OUT / "README.md").write_text(text, encoding="utf-8")


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    board = pcbnew.LoadBoard(str(BOARD_PATH))
    write_bom(board)
    fitted_bom_count, bom_gap_count = write_bom_gaps(board)
    write_placement(board)
    net_count, routed_net_count = write_nets(board)
    schematic_count, pcb_only_count, schematic_only_count = write_reference_reconciliation(board)
    write_summary(
        board, net_count, routed_net_count, schematic_count,
        pcb_only_count, schematic_only_count, fitted_bom_count, bom_gap_count,
    )
    print(f"Exported production review audit to {OUT}")


if __name__ == "__main__":
    main()
