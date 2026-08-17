"""Add the two continuous internal ground-reference planes to board2.

The production stack assigns In1.Cu and In4.Cu as uninterrupted GND reference
layers.  Existing outer-layer local copper zones are preserved.
"""

from pathlib import Path
from shutil import copy2

import pcbnew


ROOT = Path(__file__).resolve().parents[1]
BOARD_PATH = ROOT / "pcb-hmd-carrier-board2.kicad_pcb"
BACKUP = ROOT / "output/review/pcb-hmd-carrier-board2-before-ground-planes.kicad_pcb"


def mm(value: float) -> int:
    return pcbnew.FromMM(value)


def main() -> None:
    board = pcbnew.LoadBoard(str(BOARD_PATH))
    nets = board.GetNetsByName()
    if "GND" not in nets:
        raise RuntimeError("The board has no GND net")
    ground = nets["GND"]

    present = {(str(zone.GetNetname()), zone.GetLayer()) for zone in board.Zones()}
    required = (pcbnew.In1_Cu, pcbnew.In4_Cu)
    missing = [layer for layer in required if ("GND", layer) not in present]
    if not missing:
        print("Production GND planes already exist")
        return

    outline = pcbnew.SHAPE_POLY_SET()
    if not board.GetBoardPolygonOutlines(outline, True):
        raise RuntimeError("Cannot construct a closed board outline")

    BACKUP.parent.mkdir(parents=True, exist_ok=True)
    if not BACKUP.exists():
        copy2(BOARD_PATH, BACKUP)

    for layer in missing:
        zone = pcbnew.ZONE(board)
        zone.SetNet(ground)
        zone.SetLayer(layer)
        for polygon_index in range(outline.OutlineCount()):
            zone.AddPolygon(outline.Outline(polygon_index))
        zone.SetLocalClearance(mm(0.20))
        zone.SetMinThickness(mm(0.15))
        zone.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        board.Add(zone)

    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(BOARD_PATH), board)
    print("Added continuous GND reference planes on In1.Cu and In4.Cu")


if __name__ == "__main__":
    main()
