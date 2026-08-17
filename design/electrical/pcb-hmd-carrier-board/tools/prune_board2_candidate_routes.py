"""Prune unsafe or incomplete autorouter copper from a Board2 candidate.

Freerouting may export partial traces for connections it could not finish.  A
raw segment count therefore is not a release criterion.  This tool retains only
nets for which every physical pad is in one KiCad connectivity component.  For
high-speed differential nets it additionally requires both members of the pair
to be complete and their routed copper lengths to match within a caller-selected
limit.  An optional KiCad DRC report can be used to reject any net whose track or
via participates in a reported violation.

The authoritative Board2 file is never modified by this tool.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path
import re

import pcbnew


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BASE = ROOT / "pcb-hmd-carrier-board2.kicad_pcb"


def mate_name(name: str) -> str | None:
    if name.endswith("_P"):
        return name[:-2] + "_N"
    if name.endswith("_N"):
        return name[:-2] + "_P"
    return None


def is_high_speed(name: str) -> bool:
    upper = name.upper()
    return any(
        token in upper
        for token in ("HDMI_", "DSI", "MIPI", "PCIE_", "USB3_", "USB_C_RX", "USB_C_TX")
    ) and mate_name(name) is not None


def electrically_complete_nets(board: pcbnew.BOARD) -> set[str]:
    board.BuildConnectivity()
    connectivity = board.GetConnectivity()
    connectivity.RecalculateRatsnest()
    pads_by_net: dict[str, list[pcbnew.PAD]] = defaultdict(list)
    tracks_by_net: dict[str, list[pcbnew.PCB_TRACK]] = defaultdict(list)
    for footprint in board.GetFootprints():
        for pad in footprint.Pads():
            if pad.GetNetCode():
                pads_by_net[str(pad.GetNetname())].append(pad)
    for item in board.GetTracks():
        if item.GetNetCode():
            tracks_by_net[str(item.GetNetname())].append(item)
    complete: set[str] = set()
    for name, pads in pads_by_net.items():
        if len(pads) < 2 or not tracks_by_net.get(name):
            continue
        connected_items = connectivity.GetConnectedItems(pads[0])
        connected_pads = {
            item.m_Uuid.AsString()
            for item in connected_items
            if isinstance(item, pcbnew.PAD)
        }
        if all(pad.m_Uuid.AsString() in connected_pads for pad in pads):
            complete.add(name)
    return complete


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("candidate", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--base", type=Path, default=DEFAULT_BASE,
        help="clean Board2 base into which promotable candidate copper is copied",
    )
    parser.add_argument(
        "--drc-report", type=Path,
        help="reject nets whose track/via is named in this KiCad DRC report",
    )
    parser.add_argument(
        "--max-high-speed-mismatch-mm", type=float, default=0.50,
        help="maximum permitted P/N copper-length mismatch (default: 0.50 mm)",
    )
    args = parser.parse_args()

    board = pcbnew.LoadBoard(str(args.candidate.resolve()))
    board.BuildConnectivity()
    connectivity = board.GetConnectivity()
    connectivity.RecalculateRatsnest()

    pads_by_net: dict[str, list[pcbnew.PAD]] = defaultdict(list)
    tracks_by_net: dict[str, list[pcbnew.PCB_TRACK]] = defaultdict(list)
    lengths: dict[str, float] = defaultdict(float)
    for footprint in board.GetFootprints():
        for pad in footprint.Pads():
            if pad.GetNetCode():
                pads_by_net[str(pad.GetNetname())].append(pad)
    for item in board.GetTracks():
        name = str(item.GetNetname())
        if not name:
            continue
        tracks_by_net[name].append(item)
        if not isinstance(item, pcbnew.PCB_VIA):
            lengths[name] += pcbnew.ToMM(item.GetLength())

    complete: set[str] = set()
    for name, pads in pads_by_net.items():
        if len(pads) < 2 or not tracks_by_net.get(name):
            continue
        connected_items = connectivity.GetConnectedItems(pads[0])
        connected_pads = {
            item.m_Uuid.AsString()
            for item in connected_items
            if isinstance(item, pcbnew.PAD)
        }
        if all(pad.m_Uuid.AsString() in connected_pads for pad in pads):
            complete.add(name)

    rejected_drc: set[str] = set()
    if args.drc_report:
        report = args.drc_report.read_text(encoding="utf-8", errors="replace")
        rejected_drc.update(
            re.findall(r"(?:Track|Via|Arc) \[([^\]]+)\]", report)
        )

    rejected_pair: set[str] = set()
    for name in sorted(complete):
        if not is_high_speed(name):
            continue
        mate = mate_name(name)
        if mate not in complete:
            rejected_pair.update((name, mate))
            continue
        if abs(lengths[name] - lengths[mate]) > args.max_high_speed_mismatch_mm:
            rejected_pair.update((name, mate))

    base_analysis = pcbnew.LoadBoard(str(args.base.resolve()))
    already_complete = electrically_complete_nets(base_analysis)
    keep = complete - rejected_drc - rejected_pair - already_complete
    orphaned_pair_members = {
        name for name in keep
        if is_high_speed(name) and mate_name(name) not in keep
    }
    keep -= orphaned_pair_members
    accepted_items = [
        item.Duplicate()
        for item in board.GetTracks()
        if str(item.GetNetname()) in keep
    ]
    # Build the result from the clean authoritative base.  Removing hundreds of
    # SWIG-owned track objects from a board with a live connectivity graph can
    # crash pcbnew; cloning the accepted additions is both safer and makes it
    # impossible for partial candidate copper to leak into the result.
    result = pcbnew.LoadBoard(str(args.base.resolve()))
    replaced_base_items = 0
    for item in list(result.GetTracks()):
        if str(item.GetNetname()) in keep:
            result.Remove(item)
            replaced_base_items += 1
    copied_items = 0
    for item in accepted_items:
        result.Add(item)
        copied_items += 1

    pcbnew.ZONE_FILLER(result).Fill(result.Zones())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    pcbnew.SaveBoard(str(args.output.resolve()), result)
    print(f"Complete routed nets before gates: {len(complete)}")
    print(f"Rejected by DRC report: {len(rejected_drc & complete)}")
    print(f"Rejected by differential-pair gate: {len(rejected_pair & complete)}")
    print(f"Already complete in base: {len(already_complete & complete)}")
    print(f"Rejected orphaned differential members: {len(orphaned_pair_members)}")
    print(f"Promotable routed nets: {len(keep)}")
    print(f"Kept nets: {', '.join(sorted(keep)) if keep else 'none'}")
    print(f"Replaced base track/via items: {replaced_base_items}")
    print(f"Copied candidate track/via items: {copied_items}")
    print(f"Pruned candidate: {args.output.resolve()}")


if __name__ == "__main__":
    main()
