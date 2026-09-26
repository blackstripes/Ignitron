# Ignitron Enclosure v2 — OpenCode Modeling Brief

> **Current prototype basis:** The user-supplied `IPS_160x80_SPI_ST7735S_Display_0_96.stp` replaces the earlier OLED model. The six rear-mounted 160×80 TFTs use the STEP PCB (30 × 24 mm), 25 × 19 mm hole pattern, rear screen-housing relief and a separate pocket for front-facing header pins. The JPG and image-only PDF show the pin orientation but provide no dimensioned drawing. STEP dimensions are model-derived rather than physically validated; see [MEASUREMENTS_NEEDED.md](MEASUREMENTS_NEEDED.md). The original questions below are preserved as historical design requirements, not claims of verified fit.

Reference image: `ignitron-enclosure-v2-concept.png`

## Goal

Create a printable new enclosure for Ignitron based on the concept image in this directory. The concept image is a **visual/layout reference**, not a source of mechanical dimensions.

The target layout is a compact guitar-pedal controller with:

- 1 × main 2.8" color display in the upper-right area
- 6 × 0.96" 80×160 ST7735 TFT modules
- 6 × real guitar-pedal momentary footswitches, one below each small display
- 2 × additional momentary footswitches without small displays:
  - MODE
  - TAP / TUNER
- 8 total footswitches
- no per-switch LED-ring hardware
- a physical arrangement generally inspired by the compact usability of an HX Stomp XL:
  - 3 display-equipped switches across the upper-left
  - 3 display-equipped switches across the lower-left
  - main display upper-right
  - MODE and TAP/TUNER lower-right

The concept should feel like a real stompbox that can survive repeated foot use, not a lightweight electronics project enclosure.

---

# PHASE 0 — REQUIRED QUESTIONS BEFORE MODELING

**Do not create CAD, OpenSCAD geometry, STLs, or assume missing dimensions yet.**

First inspect the repository and the reference image, identify every decision or dimension that materially affects the mechanical model, and ask me the questions needed to resolve them.

You may group related questions to make them easy to answer. Distinguish:

1. **Required before modeling** — answers that affect enclosure geometry, clearances, mounting, or component collisions.
2. **Can safely be parameterized/prototyped** — values that can be changed later without invalidating the initial architecture.
3. **Cosmetic/preferences** — optional design choices.

At minimum, explicitly determine or ask about the following if they cannot be verified from the repo:

### Displays
- Exact make/model of the 2.8" display
- PCB width/height/thickness of the 2.8" display
- Active screen dimensions and location relative to its PCB
- 2.8" display mounting-hole positions/diameters, if any
- Connector/header location, orientation, and required cable clearance
- Exact make/model of the six 0.96" 80×160 ST7735 modules
- PCB width/height/thickness of those modules
- Active screen window dimensions and offset
- Mounting-hole locations/diameters
- 8-pin header orientation and whether headers will remain installed, be replaced, or be wired directly
- Desired orientation of each small display

### Footswitches
- Exact footswitch part or link
- Threaded-bushing diameter
- Required panel hole diameter
- Body diameter below panel
- Overall depth below top surface
- Nut/washer outside diameter and thickness
- Whether all eight switches are the same hardware

The old Ignitron documentation uses 12.2 mm panel holes for its SPST soft-touch momentary pedal switches. Reuse that only if the current switches are confirmed to be the same part.

### Controller electronics
- Exact MCU/dev board being used in this build
- Dimensions and mounting-hole positions
- Whether the existing Ignitron PCB is being reused, modified, or replaced
- Any additional boards/modules that must fit inside
- Required access to USB for programming/debugging
- Power-input connector type and dimensions
- Any external MIDI, TRS, USB, power-out, or other connectors needed now or anticipated later
- Preferred connector side: rear, left, right, etc.

### Enclosure / printing
- Printer model and usable build volume
- Preferred filament (PLA, PETG, ASA, etc.)
- Whether the enclosure may be split into more than two major printed sections if required by build volume
- Desired approximate overall footprint, or whether compactness should be optimized automatically around components
- Whether the top should be flat or slightly sloped like the concept
- Preferred front/rear height
- Whether rubber feet are desired
- Preferred screw size for the bottom panel
- Whether heat-set inserts are available/preferred
- Whether labels/logos should be modeled, embossed/debossed, left for a separate overlay, or omitted from the printable prototype

### Assembly/serviceability
- Desired display mounting method if there is a preference
- Whether mini-display retainers should be independent replaceable parts
- Whether the main-display retainer should be separate
- Whether the design should permit replacing a display without removing every footswitch
- Any cable connectors or ribbon cables already purchased that impose bend-radius/clearance constraints

### Ergonomics
- Preferred footswitch center-to-center spacing, if known
- Whether accidental simultaneous presses are a concern
- Whether the upper row must be reachable without contacting the lower row
- Whether MODE and TAP/TUNER should match the spacing of the six primary switches

After asking these questions, **STOP and wait for my answers**. Do not start modeling until I respond.

If repository data definitively answers a question, show what you found rather than asking me to repeat it.

---

# PHASE 1 — REPOSITORY / DIMENSION AUDIT

After I answer the Phase 0 questions:

Inspect the existing repo, especially:

- `hardware/README.md`
- `hardware/tinyOledBezel.scad`
- existing 3D-case images/assets under `hardware/`
- existing schematic/PCB information
- any current BOM or documentation describing ESP32, switches, connectors, displays, or mounting

Create a hardware-dimensions table before creating the final model.

For every component, record when applicable:

- source / part number
- PCB/module width and height
- active display window
- PCB thickness
- mounting-hole locations
- header/connector location
- required cable clearance
- required depth below the top surface
- panel-hole dimensions
- nut/washer clearance
- verified vs measured vs provisional status

**Never infer critical mechanical dimensions from pixels in the concept image.**

If any exact dimension is still unavailable, keep it in a clearly marked parameter block and document it in `MEASUREMENTS_NEEDED.md`.

---

# PHASE 2 — CAD APPROACH

Use **OpenSCAD** unless there is a strong technical reason to use CadQuery. The repository already uses OpenSCAD and the design should remain reproducible from source.

Create the new work under:

`hardware/enclosure-v2/`

Use a clean parametric design with major dimensions near the beginning of the source, including:

- enclosure width/depth/height
- top-panel slope
- wall thickness
- floor thickness
- corner radius
- main-display dimensions/clearances
- mini-display dimensions/clearances
- display bezel dimensions
- switch-hole diameter
- switch spacing
- PCB standoff locations
- screw/insert dimensions
- normal fit clearances/tolerances

Prefer reusable modules such as:

- `enclosure_top()`
- `enclosure_bottom()`
- `main_display_mount()`
- `mini_display_mount()`
- `footswitch_cutout()`
- `pcb_standoff()`
- `connector_cutouts()`
- `venting()` if necessary

---

# MECHANICAL REQUIREMENTS

This is a guitar pedal intended for repeated stomping.

Design for:

- strong top-panel structure around every footswitch
- no unsupported thin spans around switches or display openings
- sufficient switch-nut/washer clearance
- practical footswitch separation
- comfortable access to both rows
- a front edge low enough to reach the lower row comfortably
- rear/top row reachable without unintentionally pressing lower switches
- sufficient internal depth for switch bodies, nuts, wiring, display PCBs, headers, MCU/PCB, and connectors
- cable-management space
- serviceability
- no fasteners trapped behind permanently mounted components
- bottom cover removable after electronics are installed

Prefer a two-piece major enclosure:

1. top/control shell
2. bottom plate

Separate display retainers are encouraged.

Use heat-set inserts and machine screws where sensible.

Assume FDM printing. Starting guidelines unless component geometry requires otherwise:

- shell walls: ~2.0–2.5 mm minimum
- locally thicker/reinforced structure near stomp switches
- filleted/radiused internal corners
- approximately 0.25–0.4 mm normal mating clearance where appropriate
- avoid unnecessarily support-heavy geometry

---

# DISPLAY MOUNTING

The six 0.96" displays must look integrated rather than like raw breakout boards sitting in holes.

Each should have:

- recessed bezel
- only the intended active-screen region visible
- hidden PCB edges
- repeatable underside mounting/retention
- space for header/wiring and strain relief
- replaceable retaining parts where practical

The 2.8" main display should use the same design language with a larger bezel and secure underside mounting.

Prefer separate printable retainers so display fit can be revised without reprinting the entire enclosure.

---

# FIT TESTS BEFORE THE FULL ENCLOSURE

Do not jump directly to a large final print.

Create a small printable mini-display fit-test coupon containing:

- one 0.96" display opening
- bezel/mount
- representative top-panel thickness
- one footswitch hole
- representative switch reinforcement
- heat-set insert test hole if inserts are used

Also create a separate 2.8" display fit-test coupon if its mounting geometry is materially different.

The workflow should explicitly recommend printing and physically validating these coupons before final enclosure dimensions are frozen.

---

# EXPECTED OUTPUTS

Once questions are answered and dimensions are sufficiently verified, create:

- `hardware/enclosure-v2/ignitron-enclosure-v2.scad`
- `hardware/enclosure-v2/enclosure-top.stl`
- `hardware/enclosure-v2/enclosure-bottom.stl`
- display-retainer STL(s), if separate
- `hardware/enclosure-v2/fit-test-mini-display.stl`
- `hardware/enclosure-v2/fit-test-main-display.stl`
- rendered preview PNG(s) from useful angles
- exploded/internal preview showing component placement
- `hardware/enclosure-v2/README.md`
- `hardware/enclosure-v2/MEASUREMENTS_NEEDED.md` if anything remains unverified

Also document:

- overall enclosure dimensions
- front and rear heights
- switch center-to-center spacing
- small-display coordinates
- main-display coordinates
- internal depth
- critical component clearances
- screw and insert sizes

---

# DESIGN TARGET

Use `ignitron-enclosure-v2-concept.png` as the visual target.

Preserve the concept's general organization:

- compact, professional stompbox
- six display-equipped switches in a 3×2 group on the left
- large 2.8" display upper-right
- MODE and TAP/TUNER switches lower-right
- clean understated industrial design
- proportions loosely inspired by HX Stomp XL ergonomics without copying its enclosure

Mechanical correctness, ergonomics, printability, and serviceability take priority over matching the concept pixel-for-pixel.

No per-switch LED-ring hardware is planned; the six small TFTs provide switch state/status indication.

---

# VALIDATION BEFORE CALLING IT COMPLETE

Do not declare the model complete merely because it renders.

Validate that:

- all exported parts are manifold
- STLs export successfully
- no display/switch/PCB/connector collisions exist
- footswitch nuts and washers fit
- display PCBs and headers clear neighboring parts
- wiring paths are realistic
- the enclosure can actually be assembled
- the bottom can be installed/removed
- screw bosses do not collide with electronics
- display retainers can be installed and removed
- the design fits the intended printer build volume
- likely print orientation and support requirements are documented

At completion, summarize:

1. verified dimensions
2. user-measured dimensions
3. provisional assumptions
4. generated files
5. unresolved measurements
6. mechanical concerns / risks
7. what should be physically test-printed before the full enclosure
