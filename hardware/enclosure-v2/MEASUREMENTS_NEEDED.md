# Measurements needed before a full enclosure print

Nothing in this list should be read as verified merely because a parameter exists in the SCAD source.

## Main display — blocking conflict

The currently supplied figures conflict:

| Reported item | Value | Consequence |
|---|---:|---|
| Module | 65 × 50 × 7 | reported, not independently verified |
| Glass/lip | 76.25 × 53.15 | user-provided for the recess/lip |
| Visible opening | 66.5 × 48 | user-provided opening |

A 65 mm module width does not by itself explain a 76.25 mm lip. The new panel intentionally makes an exact 76.25 × 53.15, 1.2 mm-deep seat around the 66.5 × 48 through opening, but this does not resolve that conflict. Measure the exact decased display assembly: outer glass/module, active area, thickness, glass offset from PCB, mounting-hole centres/diameters, connector direction, and cable/header clearance. The source intentionally retains all three values separately.

## Mini displays (six landscape 0.96in 80×160 ST7735 modules)

The supplied PCB envelope is 30.2 × 24.2 × 3.5 and the supplied hole pattern is 25.3 × 19.4. The model uses these values and an approximate 24.25 × 13.5 landscape active window. Confirm the active-area offset, exact mounting-hole diameter/location, header position/height, and wire bend radius. The four M2 pilot/posts and their narrow case-owned support ribs are **provisional**; verify that screws do not hit traces, the ribs clear the PCB/header, and the assembly is actually retained.

## Switches and structure

The existing legacy hardware document names 12.2 mm holes for a prior SPST momentary switch, but this does not verify the eight switches for v2. Record the exact switch part, bushing/hole diameter, washer/nut OD and thickness, body diameter, depth below panel, and solder-lug/wire envelope. Confirm 38 mm centre spacing is usable with actual shoes and nuts.

## Controller, power, and connectors

Record the controller/PCB outer dimensions, hole pattern, standoff height, all other boards, and their cable paths. Confirm the actual USB connector envelope and its left-side location. Measure the DC jack and optional TRS jack body, panel cutout, nut clearance, and left-rear locations. The current USB (14 × 8), DC (12.5 diameter), and TRS (7 diameter) cutouts are placeholders only.

## Fasteners and printing

Select an actual M3 bolt length and heat-set insert model; measure its OD and installed depth before using the 4.6 mm / 5.2 mm pilot parameters. Validate wall strength, insert installation access, and bottom removal with the electronics installed. Confirm material, nozzle, layer height, shrink allowance, and whether the display lips bridge without unacceptable support.

## Provisional enclosure assumptions

The 220 × 155 footprint, 32/50 mm front/rear heights, 3.2 mm panel, 2.8 mm walls, 3 mm floor, and every component coordinate are initial architectural assumptions. Each separate solid fits the 256 mm Bambu P1S build cube, but has not been physically collision-checked. The panel cuts are now panel-normal (the main pocket is a constant 1.2 mm depth). The model includes no verified controller-board standoffs, no verified main-display retainer, no wiring keep-outs, and no ventilation requirement; these must be resolved before a full print.
