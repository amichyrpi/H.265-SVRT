"""Remove entire incomplete power routes that contain undersized segments.

Mixing an old narrow segment into an otherwise wide power route defeats the
current/thermal intent of the net class.  This production-routing preparation
step removes copper only for nets that fail the explicit preliminary width
floor, so the complete net can be rerouted consistently.  Components, pads,
nets, zones and placement are untouched.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path

import pcbnew


HIGH_CURRENT = {
    "+5V_SYS", "BAT+", "BATP", "VBAT_SYS", "VBUS_CHARGE",
    "VBUS_USB_HOST", "LED_BOOST",
}


def required_width(name: str) -> float:
    if name in HIGH_CURRENT:
        return 1.0
    if name.startswith("+") or name in {"LCD_VSP", "LCD_VSN"}:
        return 0.5
    return 0.0


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    inspection = pcbnew.LoadBoard(str(args.source.resolve()))
    widths: dict[str, list[float]] = defaultdict(list)
    for item in inspection.GetTracks():
        if isinstance(item, pcbnew.PCB_VIA):
            continue
        widths[str(item.GetNetname())].append(pcbnew.ToMM(item.GetWidth()))
    failing = {
        name for name, values in widths.items()
        if required_width(name)
        and min(values) + 1e-6 < required_width(name)
    }

    # Reload before removal to avoid retaining SWIG connectivity/item handles.
    board = pcbnew.LoadBoard(str(args.source.resolve()))
    removed = 0
    for item in list(board.GetTracks()):
        if str(item.GetNetname()) in failing:
            board.Remove(item)
            removed += 1
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    pcbnew.SaveBoard(str(args.output.resolve()), board)
    print(f"Power nets scheduled for full-width reroute: {', '.join(sorted(failing)) or 'none'}")
    print(f"Removed undersized-net track/via items: {removed}")
    print(f"Sanitized board: {args.output.resolve()}")


if __name__ == "__main__":
    main()
