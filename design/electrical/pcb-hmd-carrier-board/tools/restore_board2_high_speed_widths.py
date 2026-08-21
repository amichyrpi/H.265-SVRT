"""Restore production net-class widths after a fine-pitch escape trial.

Disposable routing trials may use a temporary narrow width to discover a
topology through 0.35/0.65 mm pitch breakouts.  A topology is not promotable
until every non-via segment has been restored to its preliminary stackup width
and passes KiCad DRC.  This tool performs that restoration on a candidate only.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import pcbnew


def width_mm(name: str) -> float | None:
    upper = name.upper()
    if "HDMI_" in upper or "DSI" in upper or "MIPI" in upper:
        return 0.123
    if "USB3_" in upper or "USB_C_RX" in upper or "USB_C_TX" in upper:
        return 0.170
    if "USB2_" in upper:
        return 0.170
    if "PCIE_" in upper and (name.endswith("_P") or name.endswith("_N")):
        return 0.200
    return None


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    board = pcbnew.LoadBoard(str(args.source.resolve()))
    changed = 0
    for item in board.GetTracks():
        if isinstance(item, pcbnew.PCB_VIA):
            continue
        target = width_mm(str(item.GetNetname()))
        if target is not None and item.GetWidth() != pcbnew.FromMM(target):
            item.SetWidth(pcbnew.FromMM(target))
            changed += 1
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    pcbnew.SaveBoard(str(args.output.resolve()), board)
    print(f"Restored production width on {changed} high-speed segments")
    print(f"Width-restored candidate: {args.output.resolve()}")


if __name__ == "__main__":
    main()
