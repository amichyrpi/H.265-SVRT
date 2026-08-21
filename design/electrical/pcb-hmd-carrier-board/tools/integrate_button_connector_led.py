"""Add the exact button-flex mate and a CM4-controlled RGB power indicator.

Only new interface parts and their routes are added. Existing footprints in the
manually arranged carrier are never moved or regenerated.
"""

from pathlib import Path
from shutil import copy2

import pcbnew


ROOT = Path(__file__).resolve().parents[1]
BOARD_PATH = ROOT / "pcb-hmd-carrier-board2.kicad_pcb"
LOCAL_FOOTPRINTS = ROOT / "libraries/footprints/Stearlight.pretty"
KICAD_FOOTPRINTS = Path(r"C:/Program Files/KiCad/10.0/share/kicad/footprints")
BACKUP = ROOT / "output/review/pcb-hmd-carrier-board2-before-button-led.kicad_pcb"


def mm(value: float) -> int:
    return pcbnew.FromMM(value)


def point(x: float, y: float) -> pcbnew.VECTOR2I:
    return pcbnew.VECTOR2I(mm(x), mm(y))


def fp_position(footprint) -> tuple[float, float]:
    position = footprint.GetPosition()
    return pcbnew.ToMM(position.x), pcbnew.ToMM(position.y)


def load_footprint(directory: Path, name: str):
    result = pcbnew.FootprintLoad(str(directory), name)
    if result is None:
        raise RuntimeError(f"Unable to load {directory.name}:{name}")
    return result


def place(board, footprint, reference, value, x, y, angle=0):
    footprint.SetReference(reference)
    footprint.SetValue(value)
    footprint.SetPosition(point(x, y))
    footprint.SetOrientationDegrees(angle)
    board.Add(footprint)
    footprint.Reference().SetLayer(pcbnew.F_Fab)
    footprint.Reference().SetVisible(False)
    footprint.Reference().SetTextSize(point(0.60, 0.60))
    footprint.Reference().SetTextThickness(mm(0.10))
    footprint.Value().SetVisible(False)
    return footprint


def get_or_add_net(board, name):
    nets = board.GetNetsByName()
    if name in nets:
        return nets[name]
    result = pcbnew.NETINFO_ITEM(board, name)
    board.Add(result)
    return result


def pads(footprint, number):
    result = [item for item in footprint.Pads() if item.GetNumber() == str(number)]
    if not result:
        raise RuntimeError(f"{footprint.GetReference()} has no pad {number}")
    return result


def pad(footprint, number):
    return pads(footprint, number)[0]


def set_pad_net(footprint, number, net):
    for item in pads(footprint, number):
        item.SetNet(net)


def add_track(board, start, end, net, layer, width=0.15):
    result = pcbnew.PCB_TRACK(board)
    result.SetStart(start)
    result.SetEnd(end)
    result.SetNet(net)
    result.SetLayer(layer)
    result.SetWidth(mm(max(width, 0.20)))
    board.Add(result)
    return result


def route(board, coordinates, net, layer, width=0.15):
    positions = [item if isinstance(item, pcbnew.VECTOR2I) else point(*item) for item in coordinates]
    for start, end in zip(positions, positions[1:]):
        add_track(board, start, end, net, layer, width)


def add_via(board, x, y, net, diameter=0.65, drill=0.30):
    result = pcbnew.PCB_VIA(board)
    result.SetPosition(point(x, y))
    result.SetWidth(mm(diameter))
    result.SetDrill(mm(drill))
    result.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    result.SetNet(net)
    board.Add(result)
    return result


def fanout_cm4(board, module, pin_number, via, net):
    target = pad(module, pin_number)
    target.SetNet(net)
    add_track(board, target.GetPosition(), via.GetPosition(), net, pcbnew.B_Cu, 0.10)


def add_text(board, text, x, y, size=0.50):
    result = pcbnew.PCB_TEXT(board)
    result.SetText(text)
    result.SetPosition(point(x, y))
    result.SetLayer(pcbnew.F_SilkS)
    result.SetTextSize(point(size, size))
    result.SetTextThickness(mm(0.09))
    board.Add(result)


def main() -> None:
    board = pcbnew.LoadBoard(str(BOARD_PATH))
    if any(item.GetReference() == "J11" for item in board.GetFootprints()):
        raise RuntimeError("J11 already exists; refusing to duplicate the interface")

    BACKUP.parent.mkdir(parents=True, exist_ok=True)
    if not BACKUP.exists():
        copy2(BOARD_PATH, BACKUP)

    modules = [
        item
        for item in board.GetFootprints()
        if item.GetReference() == "Module1" and fp_position(item)[0] > 95
    ]
    if len(modules) != 1:
        raise RuntimeError(f"Expected one manually placed CM4, found {len(modules)}")
    module = modules[0]

    net_names = (
        "GND", "+3V3", "PWR_KEY_N", "PWR_KEY_GPIO26_N",
        "VOL_UP_N", "VOL_UP_GPIO24_N", "VOL_DOWN_N", "VOL_DOWN_GPIO17_N",
        "LED_RED_K", "LED_GREEN_K", "LED_BLUE_K", "nPI_LED_PWR",
        "LED_GREEN_GPIO13_N", "LED_BLUE_GPIO16_N",
    )
    nets = {name: get_or_add_net(board, name) for name in net_names}

    # Exact BM28 DS mate. A 180-degree in-plane rotation puts the three used
    # odd-row contacts toward the carrier interior and preserves pin-1 marking.
    connector = place(
        board,
        load_footprint(LOCAL_FOOTPRINTS, "Hirose_BM28B0.6-20DS_2-0.35V_BUTTONS"),
        "J11",
        "BM28B0.6-20DS/2-0.35V BUTTON FLEX",
        231.5,
        98.5,
        180,
    )
    for number, name in ((1, "PWR_KEY_N"), (9, "VOL_UP_N"), (17, "VOL_DOWN_N"), (21, "GND")):
        set_pad_net(connector, number, nets[name])

    resistor_lib = KICAD_FOOTPRINTS / "Resistor_SMD.pretty"
    button_rows = [
        ("R76", 233.075, 1, "PWR_KEY_N", "PWR_KEY_GPIO26_N", "24", pcbnew.In1_Cu, (171.9, 128.8), [(233.075, 104.0), (220.0, 103.5), (200.0, 103.5), (185.0, 112.0), (176.0, 124.0), (173.0, 126.0), (171.9, 128.8)]),
        ("R77", 231.675, 9, "VOL_UP_N", "VOL_UP_GPIO24_N", "45", pcbnew.In2_Cu, (167.5, 129.2), [(231.675, 105.2), (220.0, 105.2), (200.0, 105.2), (185.0, 114.0), (170.0, 126.0), (167.5, 129.2)]),
        ("R78", 230.275, 17, "VOL_DOWN_N", "VOL_DOWN_GPIO17_N", "50", pcbnew.In3_Cu, (166.7, 128.3), [(230.275, 106.4), (220.0, 107.0), (200.0, 107.0), (185.0, 115.0), (170.0, 126.0), (166.7, 128.3)]),
    ]
    for reference, x, connector_pin, raw_name, gpio_name, cm4_pin, layer, target_xy, inner_route in button_rows:
        series = place(
            board,
            load_footprint(resistor_lib, "R_0402_1005Metric"),
            reference,
            "1k",
            x,
            102.0,
            90,
        )
        # Pad 2 faces the connector; each signal escapes perpendicular to the
        # 0.35 mm connector row before changing direction.
        set_pad_net(series, 2, nets[raw_name])
        set_pad_net(series, 1, nets[gpio_name])
        route(board, [pad(connector, connector_pin).GetPosition(), pad(series, 2).GetPosition()], nets[raw_name], pcbnew.F_Cu, 0.13)
        source_via = add_via(board, *inner_route[0], nets[gpio_name])
        route(board, [pad(series, 1).GetPosition(), source_via.GetPosition()], nets[gpio_name], pcbnew.F_Cu, 0.13)
        target_via = add_via(board, *target_xy, nets[gpio_name])
        route(board, inner_route, nets[gpio_name], layer, 0.13)
        fanout_cm4(board, module, cm4_pin, target_via, nets[gpio_name])

    # The buttons use CM4 internal pulls and software debounce. J11's dedicated
    # power contact 21 is the common return; contact 22 remains reserved.
    connector_ground = add_via(board, 235.5, 98.5, nets["GND"])
    route(board, [pad(connector, 21).GetPosition(), connector_ground.GetPosition()], nets["GND"], pcbnew.F_Cu, 0.22)
    ground_target = add_via(board, 176.3, 95.2, nets["GND"])
    fanout_cm4(board, module, "101", ground_target, nets["GND"])
    route(board, [connector_ground.GetPosition(), (220.0, 101.0), (200.0, 101.0), (185.0, 101.0), ground_target.GetPosition()], nets["GND"], pcbnew.In4_Cu, 0.22)

    # Common-anode RGB status indicator. Red uses nPI_LED_PWR, so it reports
    # power from boot; green/blue are active-low GPIO PWM channels for software.
    led = place(
        board,
        load_footprint(LOCAL_FOOTPRINTS, "TOGIALED_TJ-S3227SW1TCGLCCYRGB-A5"),
        "D11",
        "TJ-S3227 RGB COMMON ANODE C601674",
        233.7,
        111.0,
    )
    for number, name in ((1, "+3V3"), (2, "LED_RED_K"), (3, "LED_GREEN_K"), (4, "LED_BLUE_K")):
        set_pad_net(led, number, nets[name])

    led_data = [
        ("R73", "1k", 229.8, 108.0, 0, 2, 2, "LED_RED_K", "nPI_LED_PWR", "95", pcbnew.In2_Cu, (157.5, 129.2), [(228.6, 108.0), (220.0, 109.0), (200.0, 109.0), (185.0, 119.0), (176.0, 128.5), (170.0, 131.0), (160.0, 131.0), (157.5, 129.2)]),
        ("R74", "330R", 229.8, 113.0, 0, 3, 2, "LED_GREEN_K", "LED_GREEN_GPIO13_N", "28", pcbnew.In4_Cu, (171.1, 128.3), [(228.6, 113.0), (220.0, 110.0), (205.0, 109.0), (190.0, 124.0), (175.0, 126.0), (171.1, 128.3)]),
        ("R75", "330R", 233.7, 114.5, 90, 4, 2, "LED_BLUE_K", "LED_BLUE_GPIO16_N", "29", pcbnew.In3_Cu, (170.7, 129.1), [(233.7, 116.2), (235.0, 116.2), (235.0, 110.5), (225.0, 110.5), (200.0, 111.0), (185.0, 120.0), (174.0, 126.0), (174.0, 129.8), (170.7, 129.8), (170.7, 129.1)]),
    ]
    for reference, value, x, y, angle, led_pin, cathode_pad, cathode_name, control_name, cm4_pin, layer, target_xy, inner_route in led_data:
        resistor = place(
            board,
            load_footprint(resistor_lib, "R_0402_1005Metric"),
            reference,
            value,
            x,
            y,
            angle,
        )
        control_pad = 1 if cathode_pad == 2 else 2
        set_pad_net(resistor, cathode_pad, nets[cathode_name])
        set_pad_net(resistor, control_pad, nets[control_name])
        route(board, [pad(led, led_pin).GetPosition(), pad(resistor, cathode_pad).GetPosition()], nets[cathode_name], pcbnew.F_Cu, 0.13)
        source_xy = inner_route[0]
        source_via = add_via(board, *source_xy, nets[control_name])
        route(board, [pad(resistor, control_pad).GetPosition(), source_via.GetPosition()], nets[control_name], pcbnew.F_Cu, 0.13)
        target_via = add_via(board, *target_xy, nets[control_name])
        route(board, inner_route, nets[control_name], layer, 0.13)
        fanout_cm4(board, module, cm4_pin, target_via, nets[control_name])

    led_power = add_via(board, 234.0, 107.5, nets["+3V3"])
    route(board, [pad(led, 1).GetPosition(), led_power.GetPosition()], nets["+3V3"], pcbnew.F_Cu, 0.18)
    power_target = add_via(board, 159.9, 128.3, nets["+3V3"])
    fanout_cm4(board, module, "84", power_target, nets["+3V3"])
    route(board, [led_power.GetPosition(), (234.0, 102.0), (220.0, 102.0), (200.0, 102.0), (185.0, 107.5), (170.0, 120.0), power_target.GetPosition()], nets["+3V3"], pcbnew.In4_Cu, 0.22)

    add_text(board, "J11", 231.5, 95.0, 0.80)
    add_text(board, "RGB", 230.0, 114.8, 0.80)
    add_text(board, "CA", 234.8, 108.1, 0.80)

    # Refill existing copper zones after adding nets, otherwise KiCad would
    # retain stale fill geometry and report false zone-to-new-route collisions.
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(BOARD_PATH), board)
    print("Integrated exact J11 button mate and CM4-controlled D11 RGB indicator")


if __name__ == "__main__":
    main()
