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
| Mini display PCB / active window | 30.2 × 24.2 × 3.5 / approx. 24.25 × 13.5 | user values; active-window approximation still needs physical check |
| Mini display retention locations | four M2 pilots on a 25.3 × 19.4 pattern | provisional; not a validated retention design |
| Switch hole | 12.2 diameter | inherited legacy value; unverified for current switch |
| Case fasteners | six M3 clearance holes and M3 insert bosses | insert type/depth unverified |
| Main display seat | 76.25 × 53.15 outer pocket, 66.5 × 48 through opening, 1.2 deep | supplied dimensions; pocket is panel-normal and leaves 4.875 mm side / 2.575 mm end land |

The source defaults to the assembled view. Set `render_mode` to `top`, `bottom`, `layout`, `fit_mini`, or `fit_main` before export. The two wrapper files directly open the fit coupons.

Example, when OpenSCAD is installed:

```sh
openscad -o enclosure-top.stl -D 'render_mode="top"' ignitron-enclosure-v2.scad
openscad -o enclosure-bottom.stl -D 'render_mode="bottom"' ignitron-enclosure-v2.scad
openscad -o fit-test-mini-display.stl fit-test-mini-display.scad
openscad -o fit-test-main-display.stl fit-test-main-display.scad
```

The committed STLs are regenerated reference exports. Print the coupons and physically fit hardware before committing to a full enclosure print.

## Architecture and assembly intent

* `enclosure-top.stl` is only the 3.2 mm planar control panel. It has no perimeter walls, skirt, shell, or display bosses. Six M3 clearance holes accept top-down bolts.
* `enclosure-bottom.stl` is the rear case (the historical filename is retained). It is an open-top tub with all four sloped walls and an inward support ledge. Its six case-owned bosses accept M3 heat-set inserts from the panel side before final assembly.
* The six small displays remain rear-mounted: their provisional M2 posts and narrow supporting ribs are case-owned under-panel structure, not panel geometry. Confirm PCB traces, headers, and screw choice.
* The main display panel seat is a real constant 1.2 mm panel-normal recess. Its outer pocket is exactly the supplied 76.25 × 53.15 and the 66.5 × 48 opening passes through it; no clearance was silently added to those supplied figures. Confirm the decased glass/module actually matches them and determine a final retainer.
* The left side has a USB opening that extends through the full wall, plus generic left-rear DC and optional TRS placeholders. Set `enable_trs = false` to omit the TRS hole. Connector dimensions and positions remain unverified.

### Sloped-panel geometry

The panel is built in local panel coordinates then rotated as one plane. Display pockets, windows, switch holes, and panel screw holes use that same transform, so they are normal to the panel. In particular, the main-display pocket is a constant 1.2 mm deep.

Print the panel outer face down if the display-pocket bridging quality is acceptable, or support its recess lips as needed. Print the rear case on its exterior floor; its open top will require ordinary wall/bridge evaluation. Print both coupons flat. PETG/ASA is preferable to PLA for a stompbox, but this has not been material-tested.

## Required fit tests

1. Print `fit-test-mini-display.scad`; it keeps its display, four M2 pilot/boss tests, switch hole/ring, and M3 insert test on one coupon. It only tests sample geometry: it does not validate module retention or PCB trace clearance.
2. Print `fit-test-main-display.scad`; test the display lip and opening before freezing the main display dimensions.
3. Confirm switch body depth, display/header depth, wiring bend clearance, controller-board placement, and side connector positions before exporting the full enclosure.

See [MEASUREMENTS_NEEDED.md](MEASUREMENTS_NEEDED.md) for every unresolved measurement and the explicitly contradictory main-display information.
