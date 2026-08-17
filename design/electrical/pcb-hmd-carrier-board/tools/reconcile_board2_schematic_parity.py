"""Normalize Board2 metadata before regenerating the recovered hierarchy.

This does not alter placement, geometry, values, references or connected nets.
It gives embedded footprints a stable local-library ID and converts KiCad's
single-pad ``unconnected-(...)`` placeholder nets back to true no-connect pads.
"""

from pathlib import Path

import pcbnew


ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / "pcb-hmd-carrier-board2.kicad_pcb"
LIBRARY = ROOT / "libraries" / "footprints" / "Board2_Recovered.pretty"


def main() -> None:
    board = pcbnew.LoadBoard(str(BOARD))
    relinked = 0
    cleared = 0
    for footprint in board.GetFootprints():
        fpid = footprint.GetFPID()
        item_name = str(fpid.GetLibItemName())
        local_footprint = LIBRARY / f"{item_name}.kicad_mod"
        if (
            item_name
            and str(fpid.GetLibNickname()) != "Board2_Recovered"
            and local_footprint.exists()
        ):
            footprint.SetFPID(pcbnew.LIB_ID("Board2_Recovered", item_name))
            relinked += 1
        for pad in footprint.Pads():
            net_name = pad.GetNetname()
            if net_name.startswith("unconnected-("):
                pad.SetNetCode(0)
                cleared += 1
    pcbnew.SaveBoard(str(BOARD), board)
    print(
        f"Normalized {relinked} footprint library IDs and {cleared} true "
        "no-connect pads; placement and connected nets unchanged"
    )


if __name__ == "__main__":
    main()
