# Pre-consolidation recovery snapshot

Before consolidating the two local Ignitron working copies, their OpenSCAD
sources and text documentation were confirmed identical. Three generated STL
files differed between copies. The alternate `/tmp/opencode/Ignitron` versions
are preserved in `tmp-workspace-stl-snapshot/`; the root-level STLs are the
versions from `/home/pzwolinski/Ignitron`.

These are recovery artifacts, **not approved print files**. The mini-display
STL in particular must not be assumed to match the current SCAD or the user's
preferred printed test. Preserve both variants until the mini-display model is
re-established from the physical display and the known printed sample.

The recovered source's coordinate convention and pin-relief placement remain
in question. Do not treat its comments about the header edge as verified.
