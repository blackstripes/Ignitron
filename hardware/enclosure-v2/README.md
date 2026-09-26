# Ignitron enclosure v2 (fit-first OpenSCAD model)

`ignitron-enclosure-v2.scad` is an initial, editable panel-on-rear-case architecture and provisional fit model, not a production-ready or fit-validated mechanical release. It creates two separate printable solids: a flat control panel in one sloped plane and an open-top sloped-wall rear case. The rear case supplies all four enclosure walls, the panel-support ledge, M3 insert bosses, and small-display mounting structure.

## Current envelope and layout

All dimensions are millimetres and are parameters at the top of the source.

| Item | Current value | Status |
|---|---:|---|
| Overall footprint | 220 × 155 | provisional; within Bambu P1S 256 mm cube |
| Top height, front / rear | 32 / 50 | provisional |
| Flat panel / case wall / floor | 3.2 / 2.8 / 3 | provisional print starting points |
| Six switch centres | x = -78, -40, -2; y = 14 / -48 | provisional layout |
| MODE / TAP switch centres | (47, -48), (90, -48) | provisional layout |
| Mini-display centres | x = -78, -40, -2; y = 47 / -15 | provisional layout |
| Main-display centre | (61, 35) | provisional layout |
| Mini ST7735S PCB / viewing opening | 30 × 24 × 1.2 / 24 × 13.5, opening x offset +1.37 | user fit target for front aperture; larger than STEP inset face |
| Mini rear screen / header recesses | 29.55 × 14.35 × 2.0 / 20.82 × 3.14 × 0.8 deep | rear screen inset height reduced about 1 mm; depth matches reported 2 mm PCB-to-display-face height; header pocket remains separate |
| Mini display retention locations | four 1.6 mm diameter, 1.0 mm deep blind M2 self-tapping pilots on a 25 × 19 pattern | STEP ~2.2 mm PCB holes; short grip is provisional / high risk |
| Switch hole | 12.2 diameter | inherited legacy value; unverified for current switch |
| Case fasteners | six M3 clearance holes and M3 insert bosses | insert type/depth unverified |
| Main display seat | 76.25 × 53.15 outer pocket, 67.5 × 48 through opening, 1.2 deep | opening widened another 0.5 mm; pocket is panel-normal and leaves 6.25 mm left / 2.5 mm right, 2.575 mm each top/bottom |

The source defaults to the assembled view. Set `render_mode` to `top`, `bottom`, `layout`, `fit_mini`, or `fit_main` before export. The mini wrapper calls `fit_test_mini()` with only its two rear inset depths overridden; `fit_mini` in the main source uses production defaults.

Example, when OpenSCAD is installed:

```sh
openscad -o enclosure-top.stl -D 'render_mode="top"' ignitron-enclosure-v2.scad
openscad -o enclosure-bottom.stl -D 'render_mode="bottom"' ignitron-enclosure-v2.scad
openscad -o fit-test-mini-display.stl fit-test-mini-display.scad
openscad -o fit-test-main-display.stl fit-test-main-display.scad
```

The STLs are regenerated reference exports. Print the coupons and physically fit hardware before committing to a full enclosure print. Run the commands from `hardware/enclosure-v2/`. The current aperture and coupon-only parameter edits do not change the rear case, but the ST7735S post/rail geometry does: regenerate `enclosure-bottom.stl` from the current SCAD rather than using an earlier export.

### Iterating only the mini display

The wrapper `fit-test-mini-display.scad` uses `use` and calls the shared `fit_test_mini()` / `mini_local_cuts()` with production mini-display XY dimensions and offsets, and production 3.2 mm panel thickness. The coupon is a **55.4 × 49.4 × 3.2 mm** local panel patch centred on one 30 × 24 mm PCB at the origin (12.7 mm / 0.5 in border around its footprint). Only the rear screen-housing and header inset depths are overridden to **2.0 mm each**; production remains **2.0 mm screen / 0.8 mm header**. Rerun `openscad -o fit-test-mini-display.stl fit-test-mini-display.scad` after source edits. The coupon has **no posts, PCB or mounting holes, switch/12 mm hole, or case rails**. It checks panel opening and relief fit only, not PCB support, screw retention, socket/wire clearance or full-case assembly. The wrapper's depth overrides do not alter the full panel.

## Architecture and assembly intent

* `enclosure-top.stl` is only the 3.2 mm planar control panel. It has no perimeter walls, skirt, shell, or display bosses. Six M3 clearance holes accept top-down bolts.
* `enclosure-bottom.stl` is the rear case (the historical filename is retained). It is an open-top tub with all four sloped walls and an inward support ledge. Its six case-owned bosses accept M3 heat-set inserts from the panel side before final assembly.
* Six 160×80 ST7735S TFTs mount from behind on four case-owned 1.5 mm posts each, with provisional 4.2 mm post OD and 1.6 mm blind pilots (**only 1 mm grip**, 0.5 mm blind roof) for M2 screws through the model's ~2.2 mm PCB holes. This screw retention is **not validated**; use the coupon to test a safe retainer before committing to a full print. Screen housing x=-13.9..14.25, y=±6.85 is relieved from the **rear** by 0.7 mm clearance per side (29.55 × 14.35 mm recess). The top-visible aperture is 24 × 13.5 mm at x=+1.37 per user fit target. This is larger than the STEP inset face rectangle and may expose some surrounding area; confirm the visible border on the physical display before finalizing. The separate header pocket at the +Y edge clears the *front-projecting* modeled pins/base, not the rear pin tails or a mating socket. With PCB front 1.5 mm behind panel rear and a 2 mm display-face height, the screen enters the 2.0 mm rear recess by 0.5 mm, leaving 1.5 mm nominal gap to its roof; header tips 1.8 mm above PCB front enter the separate 0.8 mm header relief by 0.3 mm, leaving 0.5 mm nominal roof gap. Inspect all clearances in the coupon before assembly.
* The main display panel seat is a real constant 1.2 mm panel-normal recess. Its outer pocket is exactly the supplied 76.25 × 53.15 and the 67.5 × 48 opening passes through it, shifted 1.875 mm to the right of the pocket centre; opening width is enlarged 1.0 mm from the supplied 66.5. The seat leaves 6.25 mm left / 2.5 mm right margins. Confirm the decased glass/module actually matches them and determine a final retainer.
* The left side has a USB opening that extends through the full wall, plus generic left-rear DC and optional TRS placeholders. Set `enable_trs = false` to omit the TRS hole. Connector dimensions and positions remain unverified.

### Sloped-panel geometry

The panel is built in local panel coordinates then rotated as one plane. Display pockets, windows, switch holes, and panel screw holes use that same transform, so they are normal to the panel. In particular, the main-display pocket is a constant 1.2 mm deep.

Print the panel with support/bridge strategy for its rear mini recess lips and front main-display pocket as needed. Print the rear case on its exterior floor; its open top will require ordinary wall/bridge evaluation. Print both coupons flat. PETG/ASA is preferable to PLA for a stompbox, but this has not been material-tested.

## Required fit tests

1. Print `fit-test-mini-display.stl`; this coupon tests only the local panel opening and separate **rear** housing/header recesses. It intentionally has no posts, screw holes, switch hole, or case rails. Check the display face, front header tips, and housing fit. It does not test support, screw engagement, rear-pin connector clearance, or full-case retention; validate those separately.
2. Print `fit-test-main-display.scad`; test the display lip and opening before freezing the main display dimensions.
3. Confirm switch body depth, display/header depth, wiring bend clearance, controller-board placement, and side connector positions before exporting the full enclosure.

See [MEASUREMENTS_NEEDED.md](MEASUREMENTS_NEEDED.md) for every unresolved measurement and the explicitly contradictory main-display information.
