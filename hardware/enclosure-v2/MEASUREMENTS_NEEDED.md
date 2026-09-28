# Measurements needed before a full enclosure print

Nothing in this list should be read as verified merely because a parameter exists in the SCAD source.

## Main display — blocking conflict

The currently supplied figures conflict:

| Reported item | Value | Consequence |
|---|---:|---|
| Module | 65 × 50 × 7 | reported, not independently verified |
| Glass/lip | 76.25 × 53.15 reported; current CAD seat 76.75 × 53.15 R1 | approved fit-coupon seat promoted to panel; physical lip still needs verification |
| Visible opening | 66.5 × 48 originally; current CAD is 68.0 × 48 | approved fit-coupon square opening promoted to panel |

A 65 mm module width does not by itself explain the reported 76.25 mm lip. The unreinforced base top has a 76.75 × 53.15 R1, 1.2 mm-deep seat; the fit coupon and reinforced top use 76.75 × 53.65 R1.5 at 1.45 mm depth after the printed reinforced part measured 52.65 mm, and the user requested an additional 0.25 mm depth. Both have a square 68.0 × 48 through opening (6.25 mm left / 2.5 mm right land, +1.875 mm opening offset); this does not resolve the module/lip conflict. Measure the exact decased display assembly: outer glass/module, active area, thickness, glass offset from PCB, mounting-hole centres/diameters, connector direction, and cable/header clearance. The source retains the reported module dimensions separately from the fit geometry.

## Mini displays (six 0.96in 160×80 ST7735S TFT modules)

Latest layout/fit changes: rear screen recess is 29.15 × 13.85 mm (width reduced 0.4 mm, height reduced 0.5 mm from the printed baseline). The viewport is +2.0 mm from PCB center toward the flex side; the full panel shifts each PCB/rear-feature center 2.0 mm left so the visible viewport stays centered over the switch from the top side. Mini-display rows are at y=35 and -27, and both are 21 mm from their switch centers. MODE/TUNER are 38 mm apart, matching the lower-row pitch, and centered as a pair at x=61. The mini coupon/header relief is now integrated at 2.0 mm depth. The main fit coupon and reinforced top use a 76.75 × 53.65 mm outer seat after the reinforced top measured 52.65 mm in the prior print; the base top remains 53.15 mm. The paragraph below records earlier STEP baseline values, not the current fit dimensions.

Source: user ZIP `ips_160x80_spi_st7735s_display_0_96-1.snapshot.8.zip` (STEP/FCStd/JPG/PDF), inspected from STEP Cartesian vertices; image-only PDF has no dimensioned drawing. STEP is faceted, so hole diameters and inferred housing/window bounds are model-derived, **not vendor tolerances or measured hardware**. With PCB XY centred and front plane z=1.2: PCB x=±15, y=±12, z=0..1.2; four holes centred (±12.5, ±9.5), nominal diameter ~2.2 (STEP polygon extremes 2.201). Housing spans x=-13.9..14.25, y=±6.85; inset screen rectangle x=-9.48..12.22, y=±5.4. Raised screen face is at z≈2.9 (1.7 above PCB front); verify actual *lit* area independently. Eight straight pins on +Y, pitch 2.54, centres x=-8.89,-6.35,-3.81,-1.27,1.27,3.81,6.35,8.89, y=10.65; pin section ~0.64, model range z=-8..3. Solder/base envelope x≈±10.16, y=9.38..11.92; this is broader than the pin shafts. Pins extend **both** toward the panel and into the case; a header socket on the rear tails, if planned, is not in the model.

The top viewport opening is 24 × 13.5 mm at +2.0 mm relative to the PCB center and is centered over the switch because the PCB/rear-feature center is 2.0 mm left of it. Its lit-pixel alignment still needs visual confirmation. The rear housing recess is 29.15 × 13.85 × 2.0 mm at +0.175 mm relative to the PCB center, close to the STEP housing bounds of x=-13.9..14.25, y=±6.85. The separate header-base pocket is 20.82 × 3.14 × 2.0 mm at y=+10.65 in both coupon and production top. Measure screen glass and populated pins, mounting stress, PCB thickness/warp, printing error and final connector/wire bend envelope. **Current bottom has no mini-display posts or rails.** Design and verify a non-floating support/retainer and rear-side header clearance before assembling the displays; the coupon tests openings only.

## Switches and structure

The existing legacy hardware document names 12.2 mm holes for a prior SPST momentary switch, but this does not verify the eight switches for v2. Record the exact switch part, bushing/hole diameter, washer/nut OD and thickness, body diameter, depth below panel, and solder-lug/wire envelope. Confirm 38 mm centre spacing is usable with actual shoes and nuts.

## Controller, power, and connectors

Record the controller/PCB outer dimensions, hole pattern, standoff height, all other boards, and their cable paths. Confirm the actual USB connector envelope and its rear-wall location (x=-74.5). Measure the DC jack and optional TRS jack body, panel cutout, nut clearance, and rear-wall locations (x=-25.5 and -46.5 respectively). All three are centered at z=14. The current USB (14 × 8), DC (12.5 diameter), and TRS (7 diameter) cutouts are placeholders only.

## Fasteners and printing

Select an actual M3 bolt length and heat-set insert model; measure its OD and installed depth before using the 4.6 mm / 5.2 mm blind pilot parameters. Six one-piece vertical Ø10 insert columns overlap the case floor and adjacent wall/rim, with sloped caps at the panel underside and panel-normal blind bores; check insert installation access, column/wall strength and bottom removal with the electronics installed. Confirm material, nozzle, layer height, shrink allowance, and whether the display lips bridge without unacceptable support.

## Provisional enclosure assumptions

The 220 × 155 footprint, 32/50 mm front/rear heights, 3.2 mm panel, 2.8 mm walls, 3 mm floor, and every component coordinate are initial architectural assumptions. Each separate solid fits the 256 mm Bambu P1S build cube, but has not been physically collision-checked. The panel cuts are now panel-normal (the main pocket is a constant 1.2 mm depth). The model includes no verified controller-board standoffs, no verified main-display retainer, no wiring keep-outs, and no ventilation requirement; these must be resolved before a full print.
