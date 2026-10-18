# Preset name position regression (LVGL 9.3.0)

Uses the controller's installed PlatformIO LVGL dependency and `include/lv_conf.h`.
No display hardware, Arduino stubs, flashing, or network access is needed after
`pio run -e panelan-lvgl-controller` has installed that dependency.

```sh
cmake -S tests/preset_name_lvgl -B /tmp/opencode/preset-name-lvgl -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/opencode/preset-name-lvgl -j 8
ctest --test-dir /tmp/opencode/preset-name-lvgl --output-on-failure
```

CMake extracts the position-update block from `PanelLanLVGLUI.cpp`, so the test
executes production placement code rather than duplicated offset arithmetic.
Assertions remain enabled in release builds. It uses real layout and refresh,
the production font, and the bordered button parent. It covers the exact Solo
name, one/two-line transitions, repeated updates, hidden-parent updates, stale
position repair, horizontal centering, description bounds, and FX position.

## Findings and coordinate convention

`Solo Guitar Style Tone 1` measures 280 px at Montserrat 24 / letter spacing 0.
The formatter returns that exact string with `twoLine=false`.

The hero has layout 0 (no flex/grid). LVGL 9.3 `lv_obj_align` sets alignment and
calls `lv_obj_set_pos`, which calls `lv_obj_set_y`. A control test with the old
offset 56 produces stored/object Y56 and absolute screen Y91 after refresh.
Thus the supplied hardware screenshot at the old anchor is **not reproduced**
by the old alignment call in this host fixture. Its physical root cause remains
unconfirmed; replacing align with set_y alone is not a demonstrated root fix.

Production now checks LVGL's stored Y rather than trusting a cached line-mode
bit. This repairs a stale offset even for the same line mode and retains
changed-only updates. Single-line placement subtracts the parent's top content
inset, giving outer-hero Y56 / screen Y90. `lv_obj_get_y` reports **55** because
that API subtracts the parent's 1px top border/content inset. Two-line placement
deliberately retains the old content-relative offset **42**, screen Y77, and
second-line offset 69 (screen Y104). Description remains content Y84 / screen
Y119, height 15; FX remains screen Y143. No formatter or controller behavior is
changed. Hardware verification is still required; this test does not assert
that the supplied flashed-device discrepancy has been explained.
