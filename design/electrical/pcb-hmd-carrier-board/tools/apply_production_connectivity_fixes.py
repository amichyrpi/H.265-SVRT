"""Apply verified connector/net fixes found by the production audit."""

from pathlib import Path
from shutil import copy2

import pcbnew


ROOT = Path(__file__).resolve().parents[1]
BOARD_PATH = ROOT / "pcb-hmd-carrier-board2.kicad_pcb"
BACKUP = ROOT / "output/review/pcb-hmd-carrier-board2-before-connectivity-fixes.kicad_pcb"


def by_reference(board, reference: str):
    items = [item for item in board.GetFootprints() if str(item.GetReference()) == reference]
    if len(items) != 1:
        raise RuntimeError(f"Expected one {reference}, found {len(items)}")
    return items[0]


def point(x: float, y: float) -> pcbnew.VECTOR2I:
    return pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))


def main() -> None:
    board = pcbnew.LoadBoard(str(BOARD_PATH))
    BACKUP.parent.mkdir(parents=True, exist_ok=True)
    if not BACKUP.exists():
        copy2(BOARD_PATH, BACKUP)

    nets = board.GetNetsByName()

    def assign(footprint, mapping):
        count = 0
        for pad in footprint.Pads():
            number = str(pad.GetNumber())
            if number in mapping:
                if mapping[number] not in nets:
                    net = pcbnew.NETINFO_ITEM(board, mapping[number])
                    board.Add(net)
                    nets[mapping[number]] = net
                pad.SetNet(nets[mapping[number]])
                count += 1
        return count

    # Collect board-level objects before removing footprints.  KiCad 10's
    # Python wrapper can invalidate the Tracks() iterator after BOARD.Remove().
    obsolete_zones = [zone for zone in board.Zones() if zone.GetLayer() == pcbnew.F_Cu]
    obsolete_vias = [
        track for track in board.GetTracks()
        if isinstance(track, pcbnew.PCB_VIA)
        and track.GetPosition() in (point(112.0, 94.5), point(199.0, 94.5))
    ]
    expander_ground_segments = [
        track for track in board.GetTracks()
        if isinstance(track, pcbnew.PCB_TRACK)
        and not isinstance(track, pcbnew.PCB_VIA)
        and {
            (track.GetStart().x, track.GetStart().y),
            (track.GetEnd().x, track.GetEnd().y),
        } in (
            {(point(210.7, 116.322182).x, point(210.7, 116.322182).y),
             (point(210.5, 116.322182).x, point(210.5, 116.322182).y)},
            {(point(211.8, 117.622182).x, point(211.8, 117.622182).y),
             (point(210.5, 117.622182).x, point(210.5, 117.622182).y)},
        )
    ]

    # Duplicate REF** designators make the board non-annotated and prevent
    # Specctra export.  Give the six preserved mounting footprints stable H refs;
    # no geometry or mounting feature changes.
    mounting = sorted(
        (fp for fp in board.GetFootprints() if str(fp.GetReference()) == "REF**"),
        key=lambda fp: (fp.GetPosition().y, fp.GetPosition().x),
    )
    for index, footprint in enumerate(mounting, start=1):
        footprint.SetReference(f"H{index}")
    j9 = by_reference(board, "J9")
    mapping = {
        "A1": "GND", "B12": "GND", "A12": "GND", "B1": "GND",
        "A4": "VBUS_CHARGE", "B9": "VBUS_CHARGE",
        "A9": "VBUS_CHARGE", "B4": "VBUS_CHARGE",
        "A5": "CC1", "B5": "CC2",
        "A6": "USB2_SERVICE_DP", "B6": "USB2_SERVICE_DP",
        "A7": "USB2_SERVICE_DM", "B7": "USB2_SERVICE_DM",
    }
    assigned = assign(j9, mapping)

    # Restore the accessory receptacle's complete USB-C pin assignment.  The
    # pre-cleanup Board2 retained the footprint and placement but all of its pad
    # nets had been lost.
    j8_mapping = {
        "A1": "GND", "A2": "USB_C_TX1_P", "A3": "USB_C_TX1_N",
        "A4": "VBUS_USB_HOST", "A5": "CC1", "A6": "USB2_HOST_DP",
        "A7": "USB2_HOST_DM", "A9": "VBUS_USB_HOST",
        "A10": "USB_C_RX2_N", "A11": "USB_C_RX2_P", "A12": "GND",
        "B1": "GND", "B2": "USB_C_TX2_P", "B3": "USB_C_TX2_N",
        "B4": "VBUS_USB_HOST", "B5": "CC2", "B6": "USB2_HOST_DP",
        "B7": "USB2_HOST_DM", "B9": "VBUS_USB_HOST",
        "B10": "USB_C_RX1_N", "B11": "USB_C_RX1_P", "B12": "GND",
        # Board2 has no isolated chassis domain or chassis-tie network.  Ground
        # the internal receptacle shell directly; this also prevents a floating
        # EMI shield and matches the mechanically overlapping microSD shell pad.
        "SH": "GND",
    }
    assigned += assign(by_reference(board, "J8"), j8_mapping)

    # Restore the official CM4 carrier pin assignments required by the circuits
    # already populated on Board2.  Ground and module power pins follow the
    # official CM4 IO symbol; functional GPIO/high-speed pins follow the Board2
    # net names used by their peer devices.
    cm4_mapping = {
        **{str(number): "GND" for number in (
            1, 2, 7, 8, 13, 14, 22, 23, 32, 33, 42, 43, 52, 53, 59, 60,
            65, 66, 71, 74, 98, 107, 108, 113, 114, 119, 120, 125, 126,
            131, 132, 137, 138, 144, 150, 155, 156, 161, 162, 167, 168,
            173, 174, 179, 180, 185, 186, 191, 192, 197, 198,
        )},
        **{str(number): "+5V_SYS" for number in (77, 79, 81, 83, 85, 87)},
        "24": "PWR_KEY_GPIO26_N", "25": "I2S_DOUT", "26": "I2S_LRCLK",
        "27": "I2S_DIN", "28": "LED_GREEN_GPIO13_N", "29": "LED_BLUE_GPIO16_N",
        "31": "BL_PWM", "34": "AUDIO_RESET_N", "45": "VOL_UP_GPIO24_N",
        "46": "TYPEC_INT_N", "47": "PROX_INT", "48": "TYPEC_FLIP",
        "49": "I2S_BCLK", "50": "VOL_DOWN_GPIO17_N", "51": "UART_RX",
        "54": "AUDIO_MCLK", "55": "UART_TX", "56": "I2C_SCL", "58": "I2C_SDA",
        "57": "SD_CLK", "61": "SD_DAT3", "62": "SD_CMD", "63": "SD_DAT0",
        "67": "SD_DAT1", "69": "SD_DAT2", "75": "SD_PWR_ON",
        "78": "+3V3", "84": "+3V3", "86": "+3V3", "88": "+1V8", "90": "+1V8",
        "89": "WIFI_ENABLE_N", "91": "BT_ENABLE_N", "92": "RUN_PG",
        "93": "EMMC_BOOT_N", "95": "nPI_LED_PWR", "100": "CM4_EXT_RESET_N",
        "102": "PCIE_CLKREQ_N", "103": "USB2_SERVICE_DM", "105": "USB2_SERVICE_DP",
        "109": "PCIE_RST_N", "110": "PCIE_CLK_P", "112": "PCIE_CLK_N",
        "116": "PCIE_TX_P", "118": "PCIE_TX_N", "122": "PCIE_RX_P", "124": "PCIE_RX_N",
        "143": "HDMI_R_HPD", "145": "HDMI_R_DDC_SDA", "146": "HDMI_R_D2_P",
        "147": "HDMI_R_DDC_SCL", "148": "HDMI_R_D2_N", "149": "HDMI_R_CEC",
        "151": "HDMI_L_CEC", "152": "HDMI_R_D1_P", "153": "HDMI_L_HPD",
        "154": "HDMI_R_D1_N", "158": "HDMI_R_D0_P", "160": "HDMI_R_D0_N",
        "164": "HDMI_R_CLK_P", "166": "HDMI_R_CLK_N", "170": "HDMI_L_D2_P",
        "172": "HDMI_L_D2_N", "176": "HDMI_L_D1_P", "178": "HDMI_L_D1_N",
        "182": "HDMI_L_D0_P", "184": "HDMI_L_D0_N", "188": "HDMI_L_CLK_P",
        "190": "HDMI_L_CLK_N", "199": "HDMI_L_DDC_SDA", "200": "HDMI_L_DDC_SCL",
    }
    assigned += assign(by_reference(board, "Module1"), cm4_mapping)

    # Manufacturer-defined unused/control states.  Toshiba audio-output nets are
    # not used because Board2 has a separate TLV320 codec.  U210 pin 20 is the
    # documented spare output.  MP3387A MIX low selects deterministic direct-PWM
    # dimming; its internal default is high, so tie the pin to GND explicitly.
    unused_bridge_nets = {
        f"{side}_{signal}"
        for side in ("L", "R")
        for signal in ("DAOUT", "A_OSCK", "A_SD3", "A_SD2", "A_SD1", "A_SD0", "A_WFS", "A_SCK")
    }
    for reference in ("U201", "U202"):
        for pad in by_reference(board, reference).Pads():
            if str(pad.GetNetname()) in unused_bridge_nets:
                pad.SetNetCode(0)
    for pad in by_reference(board, "U210").Pads():
        if str(pad.GetNetname()) == "DISPLAY_SPARE_GPIO":
            pad.SetNetCode(0)
    assign(by_reference(board, "U208"), {"4": "GND"})

    # The restored J301 body reaches U207's old courtyard.  U207 is the only
    # populated part moved: 1.6 mm right and 3.7 mm downward clears both J301 and
    # U6 while keeping the device inside its display-bias block.
    u207 = by_reference(board, "U207")
    u207.SetPosition(point(218.2, 127.5))

    # The dense production assembly uses Fab drawings for references.  Keep the
    # connector's user-facing CHARGE label, but remove the redundant J9/J10 refs.
    j9.Reference().SetVisible(False)
    by_reference(board, "J10").Reference().SetVisible(False)

    # Keep the CM4 Lite microSD circuit physically present and fitted in the
    # primary Rev A BOM.  The eMMC assembly variant marks these same references
    # DNP; it does not use a different PCB.
    microsd_refs = {"J301", "U301", "C301", "R301", "R302", "R303"}
    for item in board.GetFootprints():
        if str(item.GetReference()) in microsd_refs:
            item.SetDNP(str(item.GetReference()) in {"R302", "R303"})

    # Fine-pitch pin-1 graphics that touch their own pads are kept on Fab for
    # assembly documentation instead of allowing the board house to clip them.
    for reference in ("U201", "U202", "U207", "J301"):
        footprint = by_reference(board, reference)
        for graphic in footprint.GraphicalItems():
            if graphic.GetLayer() == pcbnew.F_SilkS:
                graphic.SetLayer(pcbnew.F_Fab)
            elif graphic.GetLayer() == pcbnew.B_SilkS:
                graphic.SetLayer(pcbnew.B_Fab)

    # These two configurable LEDPWM links are intentionally open for Rev A
    # bring-up; encode that assembly state in KiCad instead of only in Value.
    for reference in ("R261", "R266"):
        by_reference(board, reference).SetDNP(True)

    # Remove two obsolete local F.Cu islands.  In1/In4 are now continuous GND
    # planes; the islands had no valid stitching location and one crossed the
    # TPS25751 drain-pad region.
    for zone in obsolete_zones:
        board.Remove(zone)
    for via in obsolete_vias:
        board.Remove(via)
    for track in expander_ground_segments:
        board.Remove(track)

    # Normalize two tiny GND bridges around U210.  Earlier PCB-only cleanup had
    # assigned them through a stale net object, which made reruns duplicate them.
    for start, end in (
        (point(210.7, 116.322182), point(210.5, 116.322182)),
        (point(211.8, 117.622182), point(210.5, 117.622182)),
    ):
        segment = pcbnew.PCB_TRACK(board)
        segment.SetStart(start)
        segment.SetEnd(end)
        segment.SetLayer(pcbnew.In3_Cu)
        segment.SetWidth(pcbnew.FromMM(0.20))
        segment.SetNet(nets["GND"])
        board.Add(segment)

    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(BOARD_PATH), board)
    print(f"Assigned {assigned} verified USB-C charge/service contacts and "
          "applied approved placement/DFM fixes")


if __name__ == "__main__":
    main()
