# Measurements needed before a full enclosure print

Nothing in this list should be read as verified merely because a parameter exists in the SCAD source.

## Main display — blocking conflict

The currently supplied figures conflict:

| Reported item | Value | Consequence |
|---|---:|---|
| Module | 65 × 50 × 7 | reported, not independently verified |
| Glass/lip | 76.25 × 53.15 | user-provided for the recess/lip |
| Visible opening | 66.5 × 48 originally; current CAD is 67.5 × 48 | user-provided opening, widened 1.0 mm in two 0.5 mm steps |

A 65 mm module width does not by itself explain a 76.25 mm lip. The panel retains the 76.25 × 53.15, 1.2 mm-deep seat and now has a 67.5 × 48 through opening (6.25 mm left / 2.5 mm right land, +1.875 mm opening offset); this does not resolve the module/lip conflict. Measure the exact decased display assembly: outer glass/module, active area, thickness, glass offset from PCB, mounting-hole centres/diameters, connector direction, and cable/header clearance. The source intentionally retains all three reported dimensions separately.

## Mini displays (six 0.96in 160×80 ST7735S TFT modules)

Source: user ZIP `ips_160x80_spi_st7735s_display_0_96-1.snapshot.8.zip` (STEP/FCStd/JPG/PDF), inspected from STEP Cartesian vertices; image-only PDF has no dimensioned drawing. STEP is faceted, so hole diameters and inferred housing/window bounds are model-derived, **not vendor tolerances or measured hardware**. With PCB XY centred and front plane z=1.2: PCB x=±15, y=±12, z=0..1.2; four holes centred (±12.5, ±9.5), nominal diameter ~2.2 (STEP polygon extremes 2.201). Housing spans x=-13.9..14.25, y=±6.85; inset screen rectangle x=-9.48..12.22, y=±5.4. Raised screen face is at z≈2.9 (1.7 above PCB front); verify actual *lit* area independently. Eight straight pins on +Y, pitch 2.54, centres x=-8.89,-6.35,-3.81,-1.27,1.27,3.81,6.35,8.89, y=10.65; pin section ~0.64, model range z=-8..3. Solder/base envelope x≈±10.16, y=9.38..11.92; this is broader than the pin shafts. Pins extend **both** toward the panel and into the case; a header socket on the rear tails, if planned, is not in the model.

The front viewing opening is currently 24 × 13.5 mm at x=+1.37 per user fit target; it is larger than the STEP inset face and its lit-pixel area needs visual confirmation. The rear housing recess is 29.55 × 14.35 × 2.0 mm at x=+0.175: its height was reduced about 1 mm per user fit check, and its depth matches the reported 2 mm PCB-top-to-display-face height. Housing width gives 0.7 mm per-side clearance; height gives about 0.325 mm per-side based on STEP bounds. The separate header-base pocket remains 20.82 × 3.14 × 0.8 mm at y=+10.65. Four 4.2 mm OD posts on the hole centres project 1.5 mm from the case top with 1.6 mm × **1 mm** blind M2 pilots. With a 2 mm face projection the display enters the rear recess by 0.5 mm, leaving 1.5 mm nominal gap to its roof; front pin tips retain 0.5 mm nominal clearance to the header relief roof. The dedicated mini coupon shares production XY cuts and thickness, but overrides **both** rear inset depths to 2.0 mm (production header depth stays 0.8 mm); fitting the coupon does not change production. **Only 1 mm of self-tapping screw engagement is a substantial retention risk**: prove an appropriate screw/retainer (or revise the mount) before full printing. Measure screen glass and populated pins, mounting stress, PCB thickness/warp, printing error and final connector/wire bend envelope. Test the narrow post annulus, screw length, traces/pads and +Y support rails and the mating rear-side header against the actual unit. Adjust heights/clearances after fit test, do not force the glass or short pins against the panel.

## Switches and structure

The existing legacy hardware document names 12.2 mm holes for a prior SPST momentary switch, but this does not verify the eight switches for v2. Record the exact switch part, bushing/hole diameter, washer/nut OD and thickness, body diameter, depth below panel, and solder-lug/wire envelope. Confirm 38 mm centre spacing is usable with actual shoes and nuts.

## Controller, power, and connectors

Record the controller/PCB outer dimensions, hole pattern, standoff height, all other boards, and their cable paths. Confirm the actual USB connector envelope and its left-side location. Measure the DC jack and optional TRS jack body, panel cutout, nut clearance, and left-rear locations. The current USB (14 × 8), DC (12.5 diameter), and TRS (7 diameter) cutouts are placeholders only.

## Fasteners and printing

Select an actual M3 bolt length and heat-set insert model; measure its OD and installed depth before using the 4.6 mm / 5.2 mm pilot parameters. Validate wall strength, insert installation access, and bottom removal with the electronics installed. Confirm material, nozzle, layer height, shrink allowance, and whether the display lips bridge without unacceptable support.

## Provisional enclosure assumptions

The 220 × 155 footprint, 32/50 mm front/rear heights, 3.2 mm panel, 2.8 mm walls, 3 mm floor, and every component coordinate are initial architectural assumptions. Each separate solid fits the 256 mm Bambu P1S build cube, but has not been physically collision-checked. The panel cuts are now panel-normal (the main pocket is a constant 1.2 mm depth). The model includes no verified controller-board standoffs, no verified main-display retainer, no wiring keep-outs, and no ventilation requirement; these must be resolved before a full print.
