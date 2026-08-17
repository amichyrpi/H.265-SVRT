"""Integrate the final dual-eye BOE display hardware into carrier board 2.

This script is deliberately additive: it never moves or regenerates the user's
existing carrier placement.  It adds the two exact panel connectors, two
TC358870 bridges, power, clocks, control translation, backlight and documented
bring-up options.  HDMI and MIPI nets are assigned but not routed because their
geometry depends on the board house's controlled-impedance six-layer stackup.
"""

from pathlib import Path
from shutil import copy2

import pcbnew


ROOT = Path(__file__).resolve().parents[1]
BOARD_PATH = ROOT / "pcb-hmd-carrier-board2.kicad_pcb"
BACKUP = ROOT / "output/review/pcb-hmd-carrier-board2-before-display.kicad_pcb"
LOCAL = ROOT / "libraries/footprints/Stearlight_Display.pretty"
KICAD_FP = Path(r"C:/Program Files/KiCad/10.0/share/kicad/footprints")


def mm(value: float) -> int:
    return pcbnew.FromMM(value)


def point(x: float, y: float) -> pcbnew.VECTOR2I:
    return pcbnew.VECTOR2I(mm(x), mm(y))


def load(directory: Path, name: str):
    footprint = pcbnew.FootprintLoad(str(directory), name)
    if footprint is None:
        raise RuntimeError(f"Cannot load footprint {directory.name}:{name}")
    return footprint


def place(board, footprint, ref, value, x, y, angle=0, back=False):
    footprint.SetReference(ref)
    footprint.SetValue(value)
    footprint.SetPosition(point(x, y))
    footprint.SetOrientationDegrees(angle)
    board.Add(footprint)
    if back:
        footprint.Flip(footprint.GetPosition(), False)
    footprint.Reference().SetVisible(False)
    footprint.Value().SetVisible(False)
    return footprint


def system(board, lib, name, ref, value, x, y, angle=0, back=False):
    return place(board, load(KICAD_FP / f"{lib}.pretty", name), ref, value, x, y, angle, back)


def local(board, name, ref, value, x, y, angle=0, back=False):
    return place(board, load(LOCAL, name), ref, value, x, y, angle, back)


def use_project_model(footprint, relative_path):
    """Replace a library model reference with a project-local verified STEP."""
    footprint.Models().clear()
    model = pcbnew.FP_3DMODEL()
    model.m_Filename = "${KIPRJMOD}/" + relative_path.replace("\\", "/")
    footprint.Add3DModel(model)
    return footprint


def get_net(board, name):
    nets = board.GetNetsByName()
    if name in nets:
        return nets[name]
    net = pcbnew.NETINFO_ITEM(board, name)
    board.Add(net)
    return net


def fp_pad(footprint, number):
    for candidate in footprint.Pads():
        if candidate.GetNumber() == str(number):
            return candidate
    raise RuntimeError(f"{footprint.GetReference()} has no pad {number}")


def connect(footprint, mapping, nets):
    for number, name in mapping.items():
        found = False
        for candidate in footprint.Pads():
            if candidate.GetNumber() == str(number):
                candidate.SetNet(nets[name])
                found = True
        if not found:
            raise RuntimeError(f"{footprint.GetReference()} has no pad {number}")


def set_fine_pitch_clearance(footprint, clearance=0.12):
    """Apply the package-manufacturer clearance only inside a fine-pitch part."""
    for candidate in footprint.Pads():
        candidate.SetLocalClearance(mm(clearance))


def add_text(board, text, x, y, back=False, size=0.55):
    item = pcbnew.PCB_TEXT(board)
    item.SetText(text)
    item.SetPosition(point(x, y))
    item.SetLayer(pcbnew.B_SilkS if back else pcbnew.F_SilkS)
    item.SetTextSize(point(size, size))
    item.SetTextThickness(mm(0.09))
    if back:
        item.SetMirrored(True)
    board.Add(item)


def resistor(board, ref, value, x, y, back=False, angle=0):
    return system(board, "Resistor_SMD", "R_0402_1005Metric", ref, value, x, y, angle, back)


def capacitor(board, ref, value, x, y, back=False, angle=0, size="0402"):
    name = "C_0402_1005Metric" if size == "0402" else "C_0603_1608Metric"
    return system(board, "Capacitor_SMD", name, ref, value, x, y, angle, back)


def inductor(board, ref, value, x, y, back=False, angle=0, large=False):
    name = "L_Abracon_ASPI-4030S" if large else "L_APV_ANR252010"
    fp = system(board, "Inductor_SMD", name, ref, value, x, y, angle, back)
    if large:
        use_project_model(fp, "libraries/3dmodels/Display_Final/ASPI-4030S.step")
    return fp


def ferrite(board, ref, x, y, back=False, angle=0):
    return system(board, "Inductor_SMD", "L_0603_1608Metric", ref, "600R@100MHz", x, y, angle, back)


def testpoint(board, ref, value, net_name, x, y, nets, back=False):
    fp = system(board, "TestPoint", "TestPoint_Pad_D1.0mm", ref, value, x, y, 0, back)
    connect(fp, {1: net_name}, nets)
    return fp


def assign_cm4(module, number, net, nets):
    fp_pad(module, number).SetNet(nets[net])


PANEL_PINOUT = {
    1: "GND", 2: "DISPLAY_1V8", 3: "LCD_VSP", 4: "DISPLAY_1V8",
    5: "GND", 6: "GND", 7: "LCD_VSN", 8: "S1_TEST", 9: "GND", 10: "GND",
    11: "DSIA_D0_N", 12: "DSIA_D3_N", 13: "DSIA_D0_P", 14: "DSIA_D3_P",
    15: "GND", 16: "GND", 17: "DSIA_D1_N", 18: "DSIA_CLK_N",
    19: "DSIA_D1_P", 20: "DSIA_CLK_P", 21: "GND", 22: "GND",
    23: "DSIB_D2_P", 24: "DSIA_D2_N", 25: "DSIB_D2_N", 26: "DSIA_D2_P",
    27: "GND", 28: "GND", 29: "DSIB_CLK_P", 30: "DSIB_D1_P",
    31: "DSIB_CLK_N", 32: "DSIB_D1_N", 33: "GND", 34: "GND",
    35: "DSIB_D3_P", 36: "DSIB_D0_P", 37: "DSIB_D3_N", 38: "DSIB_D0_N",
    39: "GND", 40: "GND", 41: "LED_BOOST", 42: "LCD_RESET", 43: "GND",
    44: "LCD_TE", 45: "LED1_N", 46: "GND", 47: "LED2_N",
    48: "LCD_LEDPWM", 49: "LED3_N", 50: "GND",
}


def panel_mapping(side):
    result = {}
    for pin, base in PANEL_PINOUT.items():
        if base in {"GND", "DISPLAY_1V8", "LCD_VSP", "LCD_VSN", "LED_BOOST"}:
            result[pin] = base
        else:
            result[pin] = f"{side}_{base}"
    return result


def bridge_mapping(side):
    prefix = side
    return {
        "A1": f"{prefix}_REXT", "A2": f"HDMI_{prefix}_CEC", "A3": f"HDMI_{prefix}_DDC_SCL",
        "A4": f"{prefix}_HPDI", "A5": f"{prefix}_DSIB_D3_P", "A6": f"{prefix}_DSIB_D2_P",
        "A7": f"{prefix}_DSIB_CLK_P", "A8": f"{prefix}_DSIB_D1_P", "A9": f"{prefix}_DSIB_D0_P",
        "A10": "GND", "B1": f"{prefix}_3V3_HDMI", "B2": f"{prefix}_1V15_HDMI",
        "B3": f"HDMI_{prefix}_DDC_SDA", "B4": f"HDMI_{prefix}_HPD", "B5": f"{prefix}_DSIB_D3_N",
        "B6": f"{prefix}_DSIB_D2_N", "B7": f"{prefix}_DSIB_CLK_N", "B8": f"{prefix}_DSIB_D1_N",
        "B9": f"{prefix}_DSIB_D0_N", "B10": f"{prefix}_1V2_MIPI1",
        "C1": f"HDMI_{prefix}_CLK_P", "C2": f"HDMI_{prefix}_CLK_N", "C9": "GND", "C10": f"{prefix}_1V15_CORE",
        "D1": f"HDMI_{prefix}_D0_P", "D2": f"HDMI_{prefix}_D0_N",
        "D4": "GND", "D5": "GND", "D6": "GND", "D7": "GND",
        "D9": f"{prefix}_DSIA_D3_N", "D10": f"{prefix}_DSIA_D3_P",
        "E1": f"HDMI_{prefix}_D1_P", "E2": f"HDMI_{prefix}_D1_N",
        "E4": "GND", "E5": "GND", "E6": "GND", "E7": "GND",
        "E9": f"{prefix}_DSIA_D2_N", "E10": f"{prefix}_DSIA_D2_P",
        "F1": f"HDMI_{prefix}_D2_P", "F2": f"HDMI_{prefix}_D2_N",
        "F4": "GND", "F5": "GND", "F6": "GND", "F7": "GND",
        "F9": f"{prefix}_DSIA_CLK_N", "F10": f"{prefix}_DSIA_CLK_P",
        "G1": f"{prefix}_3V3_HDMI", "G2": f"{prefix}_1V15_HDMI",
        "G4": "GND", "G5": "GND", "G6": "GND", "G7": "GND",
        "G9": f"{prefix}_DSIA_D1_N", "G10": f"{prefix}_DSIA_D1_P",
        "H1": "GND", "H2": f"{prefix}_3V3_IO", "H9": f"{prefix}_DSIA_D0_N", "H10": f"{prefix}_DSIA_D0_P",
        "J1": f"{prefix}_BIASDA", "J2": f"{prefix}_DAOUT", "J3": f"{prefix}_BRIDGE_INT",
        "J4": f"{prefix}_A_OSCK", "J5": f"{prefix}_A_SD3", "J6": f"{prefix}_A_SD2",
        "J7": f"{prefix}_1V8_IO", "J8": f"{prefix}_A_SD1", "J9": f"{prefix}_A_SD0",
        "J10": f"{prefix}_1V2_MIPI0", "K1": f"{prefix}_PCKIN", "K2": f"{prefix}_PFIL",
        "K3": "DISPLAY_I2C_SDA_1V8", "K4": "DISPLAY_I2C_SCL_1V8", "K5": f"{prefix}_A_WFS",
        "K6": f"{prefix}_1V15_CORE", "K7": f"{prefix}_A_SCK", "K8": f"{prefix}_BRIDGE_RESETN",
        "K9": f"{prefix}_REFCLK", "K10": "GND",
    }


def build_net_names():
    names = {
        "GND", "+5V_SYS", "+3V3", "DISPLAY_3V3", "DISPLAY_1V8", "DISPLAY_1V2",
        "DISPLAY_1V15", "LCD_VSP", "LCD_VSN", "LED_BOOST", "I2C_SDA", "I2C_SCL",
        "DISPLAY_I2C_SDA_1V8", "DISPLAY_I2C_SCL_1V8", "DISPLAY_GPIO_INT_N",
        "DISPLAY_GPIO_RESETN", "BL_EN", "BL_PWM",
    }
    for side in ("L", "R"):
        names.update(panel_mapping(side).values())
        names.update(bridge_mapping(side).values())
        names.update({
            f"HDMI_{side}_D0_P", f"HDMI_{side}_D0_N", f"HDMI_{side}_D1_P", f"HDMI_{side}_D1_N",
            f"HDMI_{side}_D2_P", f"HDMI_{side}_D2_N", f"HDMI_{side}_CLK_P", f"HDMI_{side}_CLK_N",
            f"HDMI_{side}_DDC_SDA", f"HDMI_{side}_DDC_SCL", f"HDMI_{side}_HPD", f"HDMI_{side}_CEC",
            f"{side}_3V3_HDMI", f"{side}_3V3_IO", f"{side}_1V8_IO", f"{side}_1V15_HDMI",
            f"{side}_1V15_CORE", f"{side}_1V2_MIPI0", f"{side}_1V2_MIPI1",
        })
    for rail in ("3V3", "1V8", "1V2", "1V15"):
        names.update({f"REG_{rail}_SW", f"REG_{rail}_FB"})
    names.update({"BIAS_SW", "BIAS_FLY1", "BIAS_FLY2", "WLED_SW", "WLED_ISET", "WLED_OVP", "WLED_COMP"})
    return names


def add_buck(board, nets, ref_no, rail, x, y):
    u = system(board, "Package_TO_SOT_SMD", "SOT-23-5", f"U{ref_no}", "TLV62569DBVR", x, y, 180, True)
    l = inductor(board, f"L{ref_no}", "2.2uH", x + 4.8, y, True, 90, True)
    # Offset inward and below the controller to clear both the regulator
    # courtyard and the left board-edge copper rule.
    cin = capacitor(board, f"C{ref_no}1", "4.7uF 10V", x - 2.5, y - 4.0, True, size="0603")
    cout = capacitor(board, f"C{ref_no}2", "10uF 6.3V", x + 9.3, y, True, size="0603")
    top_values = {"3V3": "453k 1%", "1V8": "200k 1%", "1V2": "100k 1%", "1V15": "91k 1%"}
    rt = resistor(board, f"R{ref_no}1", top_values[rail], x + 2.0, y + 3.0, True)
    rb = resistor(board, f"R{ref_no}2", "100k 1%", x + 4.0, y + 3.0, True)
    rail_net = f"DISPLAY_{rail}"
    connect(u, {1: "+5V_SYS", 2: "GND", 3: f"REG_{rail}_SW", 4: "+5V_SYS", 5: f"REG_{rail}_FB"}, nets)
    connect(l, {1: f"REG_{rail}_SW", 2: rail_net}, nets)
    connect(cin, {1: "+5V_SYS", 2: "GND"}, nets)
    connect(cout, {1: rail_net, 2: "GND"}, nets)
    connect(rt, {1: rail_net, 2: f"REG_{rail}_FB"}, nets)
    connect(rb, {1: f"REG_{rail}_FB", 2: "GND"}, nets)


def add_bridge_support(board, nets, side, base_ref, x, y, mirror=False):
    bridge = local(board, "Toshiba_P-VFBGA80_7x7mm_P0.65", f"U{base_ref}", "TC358870XBG(NOK) C3008712", x, y, 0 if not mirror else 180)
    connect(bridge, bridge_mapping(side), nets)
    clock_x = x - 5.5
    clock = system(board, "Oscillator", "Oscillator_SMD_SiT_PQFN-4Pin_2.0x1.6mm", f"Y{base_ref}", "SIT8008ACE73-18E-48.000000E", clock_x, y + 5.6)
    use_project_model(clock, "libraries/3dmodels/Display_Final/SIT8008_2016.step")
    connect(clock, {1: "DISPLAY_1V8", 2: "GND", 3: f"{side}_REFCLK", 4: "DISPLAY_1V8"}, nets)
    cclk = capacitor(board, f"C{base_ref}0", "100n", clock_x - 3.0, y + 5.6)
    connect(cclk, {1: "DISPLAY_1V8", 2: "GND"}, nets)

    # Six independently filtered bridge domains.  VDD11_HDMI and VDDC11 are
    # both supplied from the verified 1.15 V nominal source, never from 1.10 V.
    domains = [
        ("3V3_HDMI", "DISPLAY_3V3"), ("3V3_IO", "DISPLAY_3V3"),
        ("1V8_IO", "DISPLAY_1V8"), ("1V15_HDMI", "DISPLAY_1V15"),
        ("1V15_CORE", "DISPLAY_1V15"), ("1V2_MIPI0", "DISPLAY_1V2"),
        ("1V2_MIPI1", "DISPLAY_1V2"),
    ]
    direction = 1
    for index, (domain, source) in enumerate(domains):
        fx = x + direction * (5.0 + (index % 2) * 2.1)
        fy = y - 3.4 + (index // 2) * 3.00
        bead = ferrite(board, f"FB{base_ref}{index + 1}", fx, fy, False, 90)
        connect(bead, {1: source, 2: f"{side}_{domain}"}, nets)
        dec = capacitor(board, f"C{base_ref}{index + 1}", "100n", fx + direction * 4.5, fy)
        connect(dec, {1: f"{side}_{domain}", 2: "GND"}, nets)
    bulk_data = [("3V3_HDMI", "1u"), ("1V8_IO", "1u"), ("1V15_CORE", "2.2u"), ("1V2_MIPI0", "2.2u")]
    for index, (domain, value) in enumerate(bulk_data):
        cap = capacitor(board, f"C{base_ref}{20 + index}", value, x + direction * (4.8 + index * 3.0), y + 8.0, size="0603")
        connect(cap, {1: f"{side}_{domain}", 2: "GND"}, nets)

    rext = resistor(board, f"R{base_ref}0", "2.00k 1%", x - direction * 4.8, y - 3.0)
    connect(rext, {1: f"{side}_REXT", 2: f"{side}_3V3_HDMI"}, nets)
    hpdi = resistor(board, f"R{base_ref}1", "10k", x - direction * 4.8, y - 1.8)
    connect(hpdi, {1: f"{side}_HPDI", 2: f"{side}_3V3_HDMI"}, nets)
    # INT doubles as the documented I2C address strap during reset.
    strap = resistor(board, f"R{base_ref}2", "10k ADDR 0x0F" if side == "L" else "10k ADDR 0x1F", x - direction * 4.8, y - 0.6)
    connect(strap, {1: f"{side}_BRIDGE_INT", 2: "GND" if side == "L" else "DISPLAY_1V8"}, nets)
    # Required unused analog-audio PLL terminations.
    for index, signal in enumerate(("BIASDA", "PCKIN", "PFIL")):
        cap = capacitor(board, f"C{base_ref}{30 + index}", "100n", x - direction * 4.8, y + 0.8 + index * 1.1)
        connect(cap, {1: f"{side}_{signal}", 2: "GND"}, nets)
    return bridge


def add_panel_options(board, nets, side, ref_base, x, y, mirror=False):
    direction = 1 if not mirror else -1
    ciov = capacitor(board, f"C{ref_base}", "2.2uF", x + direction * 6.8, y + 2.2, size="0603")
    cvsp = capacitor(board, f"C{ref_base + 1}", "1uF 10V", x + direction * 6.8, y + 4.2, size="0603")
    cvsn = capacitor(board, f"C{ref_base + 2}", "1uF 10V", x + direction * 6.8, y + 6.2, size="0603")
    connect(ciov, {1: "DISPLAY_1V8", 2: "GND"}, nets)
    connect(cvsp, {1: "LCD_VSP", 2: "GND"}, nets)
    connect(cvsn, {1: "LCD_VSN", 2: "GND"}, nets)
    rpd = resistor(board, f"R{ref_base}", "100k RESET pulldown", x + direction * 9.8, y + 2.2)
    connect(rpd, {1: f"{side}_LCD_RESET", 2: "GND"}, nets)
    # LEDPWM alternatives: default DNP.  They deliberately remain independent.
    r_pwm = resistor(board, f"R{ref_base + 1}", "0R DNP to BL_PWM", x + direction * 9.8, y + 4.6)
    connect(r_pwm, {1: f"{side}_LCD_LEDPWM", 2: "BL_PWM"}, nets)
    testpoint(board, f"TP{ref_base}", f"{side} S1_TEST NC", f"{side}_S1_TEST", x + direction * 13.0, y + 2.2, nets)
    testpoint(board, f"TP{ref_base + 1}", f"{side} TE", f"{side}_LCD_TE", x + direction * 13.0, y + 4.6, nets)
    testpoint(board, f"TP{ref_base + 2}", f"{side} LEDPWM", f"{side}_LCD_LEDPWM", x + direction * 13.0, y + 7.0, nets)


def add_control(board, nets):
    # CM4 I2C is 3.3 V; TC358870, panel controls and the GPIO expander are in
    # the 1.8 V domain.  This is required level translation, not an option.
    level = system(board, "Package_SO", "TSSOP-8_3x3mm_P0.65mm", "U209", "PCA9306DCTR C123752", 200.0, 102.0, 0, True)
    connect(level, {1: "GND", 2: "+3V3", 3: "DISPLAY_1V8", 4: "DISPLAY_1V8", 5: "DISPLAY_I2C_SCL_1V8", 6: "DISPLAY_I2C_SDA_1V8", 7: "I2C_SDA", 8: "I2C_SCL"}, nets)
    for ref, net, rail, x in (("R290", "I2C_SDA", "+3V3", 198.0), ("R291", "I2C_SCL", "+3V3", 199.5), ("R292", "DISPLAY_I2C_SDA_1V8", "DISPLAY_1V8", 201.0), ("R293", "DISPLAY_I2C_SCL_1V8", "DISPLAY_1V8", 202.5)):
        r = resistor(board, ref, "2.2k I2C", x, 106.5, True, 90)
        connect(r, {1: net, 2: rail}, nets)
    c = capacitor(board, "C290", "100n", 204.0, 102.0, True)
    connect(c, {1: "DISPLAY_1V8", 2: "GND"}, nets)

    gpio = system(board, "Package_SO", "TSSOP-24_4.4x7.8mm_P0.65mm", "U210", "TCA9539PWR 1V8 DISPLAY GPIO", 210.0, 102.0, 0, True)
    gpio_map = {
        1: "DISPLAY_GPIO_INT_N", 2: "GND", 3: "DISPLAY_GPIO_RESETN",
        4: "L_BRIDGE_RESETN", 5: "R_BRIDGE_RESETN", 6: "L_BRIDGE_INT", 7: "R_BRIDGE_INT",
        8: "L_LCD_RESET", 9: "R_LCD_RESET", 10: "L_LCD_TE", 11: "R_LCD_TE", 12: "GND",
        13: "BL_EN", 14: "L_LCD_LEDPWM", 15: "R_LCD_LEDPWM", 16: "L_HPDI",
        17: "R_HPDI", 18: "LCD_BIAS_ENP", 19: "LCD_BIAS_ENN", 20: "DISPLAY_SPARE_GPIO",
        21: "GND", 22: "DISPLAY_I2C_SCL_1V8", 23: "DISPLAY_I2C_SDA_1V8", 24: "DISPLAY_1V8",
    }
    for extra in ("LCD_BIAS_ENP", "LCD_BIAS_ENN", "DISPLAY_SPARE_GPIO"):
        if extra not in nets:
            nets[extra] = get_net(board, extra)
    connect(gpio, gpio_map, nets)
    for ref, net, rail, x in (("R294", "DISPLAY_GPIO_RESETN", "DISPLAY_1V8", 207.0), ("R295", "DISPLAY_GPIO_INT_N", "DISPLAY_1V8", 209.0)):
        r = resistor(board, ref, "10k", x, 108.5, True, 90)
        connect(r, {1: net, 2: rail}, nets)
    c = capacitor(board, "C291", "100n", 212.0, 108.5, True)
    connect(c, {1: "DISPLAY_1V8", 2: "GND"}, nets)


def add_bias(board, nets):
    u = local(board, "TI_DSBGA15_YFF_2.108x1.514mm_P0.4", "U207", "TPS65132B5YFFR", 202.0, 116.0, back=True)
    set_fine_pitch_clearance(u, 0.12)
    connect(u, {
        "A1": "LCD_BIAS_ENN", "A2": "LCD_VSN", "A3": "BIAS_FLY2",
        "B1": "LCD_BIAS_ENP", "B2": "DISPLAY_I2C_SCL_1V8", "B3": "GND",
        "C1": "+5V_SYS", "C2": "DISPLAY_I2C_SDA_1V8", "C3": "BIAS_FLY1",
        "D1": "BIAS_SW", "D2": "GND", "D3": "BIAS_REG",
        "E1": "GND", "E2": "BIAS_REG", "E3": "LCD_VSP",
    }, nets)
    if "BIAS_REG" not in nets:
        nets["BIAS_REG"] = get_net(board, "BIAS_REG")
        connect(u, {"D3": "BIAS_REG", "E2": "BIAS_REG"}, nets)
    l = inductor(board, "L207", "4.7uH 1.6A DFE252010", 205.0, 116.0, True, 90, False)
    connect(l, {1: "+5V_SYS", 2: "BIAS_SW"}, nets)
    caps = [
        ("C270", "4.7uF 10V", "+5V_SYS", "GND", 198.5, 112.0),
        ("C271", "2.2uF 10V", "BIAS_FLY1", "BIAS_FLY2", 198.5, 114.0),
        ("C272", "4.7uF 10V", "LCD_VSP", "GND", 198.5, 116.0),
        ("C273", "4.7uF 10V", "LCD_VSN", "GND", 198.5, 118.0),
        ("C274", "1uF", "BIAS_REG", "GND", 208.0, 116.0),
    ]
    for ref, value, a, b, x, y in caps:
        c = capacitor(board, ref, value, x, y, True, size="0603")
        connect(c, {1: a, 2: b}, nets)


def add_backlight(board, nets):
    u = system(board, "Package_DFN_QFN", "TQFN-24-1EP_4x4mm_P0.5mm_EP2.6x2.6mm", "U208", "MP3387AGRT-P", 224.0, 115.0, 90, True)
    use_project_model(u, "libraries/3dmodels/Display_Final/MP3387A_TQFN24.step")
    set_fine_pitch_clearance(u, 0.12)
    mapping = {
        1: "BL_EN", 2: "WLED_FREQ", 3: "GND", 4: "WLED_MIX", 5: "GND", 6: "BL_PWM",
        7: "GND", 8: "GND", 9: "R_LED3_N", 10: "R_LED2_N", 11: "WLED_ISET",
        12: "R_LED1_N", 13: "L_LED3_N", 14: "L_LED2_N", 15: "L_LED1_N", 16: "WLED_OVP",
        17: "GND", 18: "GND", 19: "WLED_SW", 20: "WLED_SW", 21: "GND", 22: "WLED_COMP",
        23: "+5V_SYS", 24: "+5V_SYS", 25: "GND",
    }
    for extra in ("WLED_FREQ", "WLED_MIX"):
        if extra not in nets:
            nets[extra] = get_net(board, extra)
    connect(u, mapping, nets)
    l = inductor(board, "L208", "10uH >=2.5A WLED", 218.5, 115.0, True, 90, True)
    connect(l, {1: "+5V_SYS", 2: "WLED_SW"}, nets)
    d = system(board, "Diode_SMD", "D_SOD-123", "D201", "DFLS160-7", 218.5, 109.5, 90, True)
    connect(d, {1: "LED_BOOST", 2: "WLED_SW"}, nets)
    parts = [
        (resistor, ("R280", "37.4k 1% ISET=33.2mA", 232.0, 109.5, True), {1: "WLED_ISET", 2: "GND"}),
        (resistor, ("R281", "49.9k 1% 1.25MHz", 234.0, 109.5, True), {1: "WLED_FREQ", 2: "GND"}),
        (resistor, ("R282", "147k 1% OVP", 232.0, 111.5, True), {1: "LED_BOOST", 2: "WLED_OVP"}),
        (resistor, ("R283", "20k 1% OVP", 234.0, 111.5, True), {1: "WLED_OVP", 2: "GND"}),
        (resistor, ("R284", "3k COMP", 232.0, 113.5, True), {1: "WLED_COMP", 2: "WLED_COMP_RC"}),
        (capacitor, ("C280", "68n COMP", 234.0, 113.5, True), {1: "WLED_COMP_RC", 2: "GND"}),
        (capacitor, ("C281", "10uF 10V", 215.0, 120.0, True), {1: "+5V_SYS", 2: "GND"}),
        (capacitor, ("C282", "10uF 16V", 218.0, 120.0, True), {1: "LED_BOOST", 2: "GND"}),
        (capacitor, ("C283", "10uF 16V", 221.0, 120.0, True), {1: "LED_BOOST", 2: "GND"}),
    ]
    for factory, args, mapping in parts:
        for net in mapping.values():
            if net not in nets:
                nets[net] = get_net(board, net)
        fp = factory(board, *args)
        connect(fp, mapping, nets)


def main():
    board = pcbnew.LoadBoard(str(BOARD_PATH))
    if any(fp.GetReference() == "U201" for fp in board.GetFootprints()):
        raise RuntimeError("Display subsystem already present; refusing to duplicate it")
    BACKUP.parent.mkdir(parents=True, exist_ok=True)
    if not BACKUP.exists():
        copy2(BOARD_PATH, BACKUP)

    modules = [fp for fp in board.GetFootprints() if fp.GetReference() == "Module1" and pcbnew.ToMM(fp.GetPosition().x) > 95]
    if len(modules) != 1:
        raise RuntimeError(f"Expected one target CM4, found {len(modules)}")
    module = modules[0]

    nets = {name: get_net(board, name) for name in build_net_names()}
    # Nets introduced by control and support networks.
    for name in ("LCD_BIAS_ENP", "LCD_BIAS_ENN", "DISPLAY_SPARE_GPIO", "BIAS_REG", "WLED_COMP_RC", "WLED_FREQ", "WLED_MIX"):
        nets[name] = get_net(board, name)

    # Exact display connectors at the top/face edge, with bridges kept close.
    jl = local(board, "Kyocera_245863050104829_50P_P0.35", "J201", "J_DISPLAY_LEFT 245863050104829+", 112.0, 94.0, 0)
    jr = local(board, "Kyocera_245863050104829_50P_P0.35", "J202", "J_DISPLAY_RIGHT 245863050104829+", 219.0, 93.0, 180)
    connect(jl, panel_mapping("L"), nets)
    connect(jr, panel_mapping("R"), nets)
    add_panel_options(board, nets, "L", 260, 106.0, 94.0, False)
    add_panel_options(board, nets, "R", 265, 234.0, 99.0, True)

    add_bridge_support(board, nets, "L", 201, 127.5, 100.0, False)
    add_bridge_support(board, nets, "R", 202, 207.5, 100.0, True)

    # Four shared, correctly calculated low-voltage rails on the rear-left.
    add_buck(board, nets, 203, "3V3", 106.0, 108.5)
    add_buck(board, nets, 204, "1V8", 106.0, 119.0)
    add_buck(board, nets, 205, "1V2", 124.0, 108.5)
    add_buck(board, nets, 206, "1V15", 124.0, 119.0)
    add_bias(board, nets)
    add_backlight(board, nets)
    add_control(board, nets)

    # Assign official CM4 HDMI pins without disturbing other GPIO assignments.
    cm4_hdmi = {
        "146": "HDMI_R_D2_P", "148": "HDMI_R_D2_N", "152": "HDMI_R_D1_P", "154": "HDMI_R_D1_N",
        "158": "HDMI_R_D0_P", "160": "HDMI_R_D0_N", "164": "HDMI_R_CLK_P", "166": "HDMI_R_CLK_N",
        "145": "HDMI_R_DDC_SDA", "147": "HDMI_R_DDC_SCL", "143": "HDMI_R_HPD", "149": "HDMI_R_CEC",
        "170": "HDMI_L_D2_P", "172": "HDMI_L_D2_N", "176": "HDMI_L_D1_P", "178": "HDMI_L_D1_N",
        "182": "HDMI_L_D0_P", "184": "HDMI_L_D0_N", "188": "HDMI_L_CLK_P", "190": "HDMI_L_CLK_N",
        "199": "HDMI_L_DDC_SDA", "200": "HDMI_L_DDC_SCL", "153": "HDMI_L_HPD", "151": "HDMI_L_CEC",
        "56": "I2C_SCL", "58": "I2C_SDA", "31": "BL_PWM",
    }
    for pin, net in cm4_hdmi.items():
        assign_cm4(module, pin, net, nets)

    # Bring-up test pads. No high-speed pair receives a stub.
    test_data = [
        (201, "DISPLAY_5V", "+5V_SYS"), (202, "DISPLAY_3V3", "DISPLAY_3V3"),
        (203, "DISPLAY_1V8", "DISPLAY_1V8"), (204, "DISPLAY_1V2", "DISPLAY_1V2"),
        (205, "DISPLAY_1V15", "DISPLAY_1V15"), (206, "LCD_VSP +5V5", "LCD_VSP"),
        (207, "LCD_VSN -5V5", "LCD_VSN"), (208, "BL_EN", "BL_EN"),
        (209, "BL_PWM GPIO12", "BL_PWM"), (210, "I2C_SDA_1V8", "DISPLAY_I2C_SDA_1V8"),
        (211, "I2C_SCL_1V8", "DISPLAY_I2C_SCL_1V8"),
    ]
    for index, (ref, value, net) in enumerate(test_data):
        testpoint(board, f"TP{ref}", value, net, 139.0 + index * 2.4, 94.0, nets)

    add_text(board, "DISPLAY LEFT / 1440x1600", 112.0, 91.7, False, 0.65)
    add_text(board, "DISPLAY RIGHT / 1440x1600", 216.0, 91.7, False, 0.65)
    add_text(board, "TC358870 / MIPI - STACKUP REQUIRED", 168.0, 107.0, False, 0.55)
    add_text(board, "DISPLAY POWER 3V3 1V8 1V2 1V15", 115.0, 105.0, True, 0.50)
    add_text(board, "LCD BIAS +5V5/-5V5 + 6CH WLED", 216.0, 105.0, True, 0.50)

    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(BOARD_PATH), board)
    print("Integrated final dual-eye display subsystem without moving existing parts")


if __name__ == "__main__":
    main()
