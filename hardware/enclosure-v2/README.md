# Ignitron enclosure v2 (fit-first OpenSCAD model)

`ignitron-enclosure-v2.scad` is an initial, editable panel-on-rear-case architecture and provisional fit model, not a production-ready or fit-validated mechanical release. It creates two separate printable solids: a flat control panel in one sloped plane and an open-top sloped-wall rear case. The rear case supplies all four enclosure walls, the panel-support ledge, and floor-to-rim M3 insert columns. Mini-display mounting/retention is not yet designed.

## Current envelope and layout

All dimensions are millimetres and are parameters at the top of the source.

| Item | Current value | Status |
|---|---:|---|
| Overall footprint | 220 × 155 | provisional; within Bambu P1S 256 mm cube |
| Top height, front / rear | 32 / 50 | provisional |
| Flat panel / case wall / floor | 3.2 / 2.8 / 3 | provisional print starting points |
| Six switch centres | x = -78, -40, -2; y = 14 / -48 | unchanged |
| MODE / TUNER switch centres | (42, -48), (80, -48) | 38 mm spacing matches lower-row switches; pair centered at main-display X=61 |
| Mini PCB/rear-feature centres | x = -80, -42, -4; y = 35 / -27 | 2 mm left of switch centers so viewports remain centered on switches |
| Main-display centre | (61, 35) | provisional layout |
| Full-panel mini viewport | +2.0 mm from PCB center | PCB/rear geometry is 2 mm left; visible opening centers over the switch |
| Mini-display test coupon viewport | +2.0 mm from coupon/PCB center | established by fit test; toward flex side when viewed from top |
| Mini rear screen / header recesses | 29.15 × 13.85 × 2.0 / 20.82 × 3.14 × 2.0 deep | approved coupon geometry now in production; header pocket remains separate |
| Mini display retention | none in the rear case | old floating posts/rails removed; design support and retention before assembly |
| Switch hole | 12.2 diameter | inherited legacy value; unverified for current switch |
| Case fasteners | six M3 clearance holes and M3 insert bosses | insert type/depth unverified |
| Rear connector X centres (left to right) | USB -74.5, optional TRS -46.5, DC -25.5 | provisional; all at z=14; USB 14 × 8, TRS Ø7, DC Ø12.5 |
| Main display seat | Base top: 76.75 × 53.15 R1 pocket / 1.2 deep; fit coupon/reinforced top: 76.75 × 53.65 R1.5 / 1.45 deep; both use 68.0 × 48 square opening | reinforced/test pocket is 0.5 mm taller and 0.25 mm deeper after fit tests; base top file remains unchanged |

The source defaults to the assembled view. Set `render_mode` to `top`, `bottom`, `layout`, `fit_mini`, or `fit_main` before export. The mini coupon matches production mini geometry; the main coupon adds 0.5 mm to the outer-pocket height for the reinforced variant's print-fit adjustment.

Example, when OpenSCAD is installed:

```sh
openscad -o enclosure-top.stl -D 'render_mode="top"' ignitron-enclosure-v2.scad
openscad -o enclosure-bottom.stl -D 'render_mode="bottom"' ignitron-enclosure-v2.scad
openscad -o fit-test-mini-display.stl fit-test-mini-display.scad
openscad -o fit-test-main-display.stl fit-test-main-display.scad
```

The STLs are regenerated reference exports. Print the coupons and physically fit hardware before committing to a full enclosure print. Run the commands from `hardware/enclosure-v2/`. Re-export the rear case whenever the columns or connectors change.

### Provisional reinforced-top variant

`ignitron-enclosure-v2-reinforced.scad` is a separate parametric copy of the enclosure model with provisional underside ribs; it regenerates the panel geometry rather than unioning ribs into the imported STL. Export it with:

```sh
openscad -o enclosure-top-v2-reinforced.stl -D 'render_mode="top"' ignitron-enclosure-v2-reinforced.scad
```

The v2 contour network replaces both prior grid patterns. Its new 3 mm-wide ribs project 4 mm below the panel with 0.25 mm overlap for fusion, in local sloped-plane coordinates. Each column of mini display and footswitch has a rounded perimeter contour; adjacent columns share their side rib. Upper contours span y=-5..51 and lower contours y=-65..-11, joined by a single cross-corridor at y=-7 and short connections. The lower-right mini/switch contour is open at the front-center M3 column. The cross-corridor feeds a three-sided main-display seat frame (left x=19.5, top y=65.8, bottom y=4.2, right x=102); its top-right end is open to clear the rear M3 boss. Separate rounded MODE/TUNER collars connect directly to the main frame. Contour inner edges leave nominal 1.5 mm at mini PCB sides, about 0.8 mm at the upper header edge, 1 mm at the lower PCB edge, 1.5 mm around 13 mm switch nut envelopes and at least 1 mm around the 76.75 × 53.65 R1.5 main seat; the right main rib leaves about 1.7 mm to the case cavity wall. These are nominal CAD clearances, not an electronics/wiring fit guarantee. The modeled Ø10 M3 columns and the panel/case interface remain clear. The v2 mesh passed OpenSCAD CGAL `Simple: yes`; STL shell inspection found one positive connected shell and six negative blind-header cavity shells, not floating positive solids. It writes `enclosure-top-v2-reinforced.stl`, leaving the accepted base `enclosure-top.stl` and earlier `enclosure-top-reinforced.stl` snapshot untouched. Rib layout, wiring/electronics clearance, print behavior, impact strength and deflection remain unvalidated; inspect/slice and print-fit before relying on this as a stomp-safe part.

### Iterating only the mini display

The wrapper `fit-test-mini-display.scad` uses `use` and calls the shared `fit_test_mini()` / `mini_local_cuts()` with production geometry and 3.2 mm panel thickness. The coupon is a **55.4 × 49.4 × 3.2 mm** local panel patch centred on one 30 × 24 mm PCB at the origin (12.7 mm / 0.5 in border around its footprint). The viewport is **2.0 mm right toward the flex side** (top view). The full panel uses the same +2.0 mm viewport-to-PCB offset, but places each PCB/rear-feature centre 2.0 mm left of its switch so the visible opening stays centered on the switch. Both rear screen and header reliefs are **2.0 mm deep** in the coupon and full panel. The mini coupon has **no posts, PCB or mounting holes, switch/12 mm hole, or case rails**. It checks panel opening and relief fit only, not PCB support, screw retention, socket/wire clearance or full-case assembly.

The main-display test coupon uses a **76.75 × 53.65 mm R1.5 outer pocket**, **1.45 mm deep**, and a **68.0 × 48 mm square through-opening**. The pocket is 0.5 mm taller than the unreinforced base top (53.15 mm) because the printed reinforced top measured 52.65 mm, and 0.25 mm deeper than the original 1.2 mm test pocket. The reinforced top uses the same test geometry. Only the recessed pocket corners are rounded; the through-opening remains square. Depth is measured normal to the panel face.

## Architecture and assembly intent

* `enclosure-top.stl` is only the 3.2 mm planar control panel. It has no perimeter walls, skirt, shell, or display bosses. Its sloped slab is clipped to a vertical footprint so all four outside perimeter faces are vertical and flush with the rear-case walls. Six M3 clearance holes accept top-down bolts.
* `enclosure-top-v2-reinforced.stl` is the current experimental variant, generated from `ignitron-enclosure-v2-reinforced.scad`. It has connected rounded contour ribs around the mini/switch banks, an open main-display frame and MODE/TUNER collars, rather than a grid. Its main-display seat is 76.75 × 53.65 mm, 1.45 mm deep, with R1.5 corners; the existing unreinforced `enclosure-top.stl` remains 76.75 × 53.15 mm, 1.2 mm deep, with R1 corners. The prior grid-style `enclosure-top-reinforced.stl` is retained as a version-1 snapshot. Rib clearances are nominal and provisional; electronics/wiring clearance, print behavior, impact strength, and deflection have not been physically or structurally validated.
* `enclosure-bottom.stl` is the rear case (the historical filename is retained). It is an open-top tub with all four sloped walls and an inward support ledge. Each of the six case-owned Ø10 columns is one continuous vertical cylinder, overlapping the 3 mm floor and adjacent wall/rim and capped by the sloped panel underside at its matching top-panel hole. The Ø4.6 × 5.2 mm blind M3 insert bores remain normal to the panel and open from the panel side; they do not pierce the exterior floor. Insert type/depth still require fitting.
* Six 160×80 ST7735S TFTs have panel openings but **no rear-case posts, rails or retention**; the prior isolated structure was removed. PCB/rear-feature centers are 2 mm left of the switches; the +2.0 mm viewport offset centers each visible opening on its footswitch from the top side. Display rows are at y=35 and -27: the upper row shares the main display's y-centre, and each row centre is 21 mm from its footswitch centre. The rear screen recess is 29.15 × 13.85 × 2.0 mm; the separate 20.82 × 3.14 mm header pocket is also 2.0 mm deep. MODE/TUNER are 38 mm apart, matching the lower-row pitch, and centered as a pair at main-display X=61. Design and physically verify a display support/retainer before assembly.
* The main display panel seat is a constant-depth panel-normal recess: 1.2 mm for the base top's 76.75 × 53.15 R1 pocket, and 1.45 mm for the reinforced top's 76.75 × 53.65 R1.5 pocket. Both have the square 68.0 × 48 opening, shifted 1.875 mm right of the pocket centre, leaving 6.25 mm left / 2.5 mm right margins. Confirm the decased glass/module actually matches them and determine a final retainer.
* The rear (+Y) wall has USB, optional TRS and DC openings from left to right at `usb_x=-74.5`, `trs_x=-46.5`, `dc_x=-25.5` mm, all centered at z=14 mm. They cut through the full wall, away from the rear insert columns at x=-103, 0 and 103. Set `enable_trs = false` to omit the TRS hole. Connector dimensions, nut clearance and positions remain unverified.

### Sloped-panel geometry

The panel is built in local panel coordinates then rotated as one plane. Display pockets, windows, switch holes, and panel screw holes use that same transform, so they are normal to the panel. The main-display pocket is a constant 1.2 mm deep in the base panel and 1.45 mm in the reinforced variant.

Print the panel with support/bridge strategy for its rear mini recess lips and front main-display pocket as needed. Print the rear case on its exterior floor; its open top will require ordinary wall/bridge evaluation. Print both coupons flat. PETG/ASA is preferable to PLA for a stompbox, but this has not been material-tested.

## Required fit tests

1. Print `fit-test-mini-display.stl`; this coupon tests only the local panel opening and separate **rear** housing/header recesses. It intentionally has no posts, screw holes, switch hole, or case rails. Check the display face, front header tips, and housing fit. It does not test support, screw engagement, rear-pin connector clearance, or full-case retention; validate those separately.
2. Print `fit-test-main-display.scad`; test the display lip and opening before freezing the main display dimensions.
3. Confirm switch body depth, display/header depth, wiring bend clearance, controller-board placement, and rear connector positions before printing the full enclosure. Provide a separate display mounting/retention solution.

See [MEASUREMENTS_NEEDED.md](MEASUREMENTS_NEEDED.md) for every unresolved measurement and the explicitly contradictory main-display information.
