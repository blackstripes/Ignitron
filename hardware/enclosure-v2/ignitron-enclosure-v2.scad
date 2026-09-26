/*
  Ignitron enclosure v2 -- panel-on-rear-case fit architecture (millimetres).
  render_mode: assembly, top (control panel), bottom (rear case), layout,
  fit_mini, or fit_main. Values marked PROVISIONAL require physical fitting.
*/

$fn = 48;

// --- Envelope: all component coordinates are in the sloped panel plane -------
case_w = 220;                 // PROVISIONAL; P1S-safe (<256 mm)
case_d = 155;                 // projected front-to-rear footprint, PROVISIONAL
front_h = 32; rear_h = 50;    // top-face heights, PROVISIONAL
panel_t = 3.2;                // flat control-panel thickness, PROVISIONAL
wall = 2.8; floor = 3;        // PROVISIONAL print starting points
ledge_w = 2.0;                // inward panel-support land on rear case
fit = 0.35;                   // normal printed clearance; not used on supplied main pocket
panel_angle = atan((rear_h-front_h)/case_d);
panel_d = case_d / cos(panel_angle); // true length in its own sloped plane

// --- Panel-to-case hardware --------------------------------------------------
m3_clear_d = 3.4;
m3_insert_d = 4.6;            // PROVISIONAL: confirm actual heat-set insert
m3_insert_depth = 5.2;
m3_boss_d = 10;
m3_boss_h = 9;
// These six positions sit in the case rim, clear of the panel openings.
boss_xy = [[-103,-70],[103,-70],[-103,70],[103,70],[0,-70],[0,70]];

// --- Footswitches -------------------------------------------------------------
switch_hole_d = 12.2;         // legacy value; NOT current-hardware verified
switches = [[-78,14],[-40,14],[-2,14],[-78,-48],[-40,-48],[-2,-48],
            [47,-48],[90,-48]];

// --- Six ST7735S 160x80 TFTs; STEP XY centred on 30 x 24 PCB -------------
// STEP: PCB z=0..1.2, glass/bezel to z=2.9; 8 straight through-pins
// on +Y edge, z=-8..3 (incl. header base). PCB front is on 1.5 mm posts.
mini_module_w = 30; mini_module_h = 24; mini_module_t = 1.2;
// User fit target is a 24 x 13.5 mm front viewing aperture.
mini_window_w = 24; mini_window_h = 13.5;
mini_window_x = 1.37; mini_window_y = 0;
// Housing relief retains width fit; reduce height to user's 14.35 mm target.
mini_bezel_w = 29.55; mini_bezel_h = 14.35; mini_bezel_x = 0.175;
mini_recess_depth = 2.0; // user measured PCB-top to display-face height
// Header top includes solder pads at y=9.38..11.92, x=-10.16..10.16.
// This separate rear relief clears the front-projecting pins/base; no socket.
mini_header_w = 20.82; mini_header_h = 3.14; mini_header_y = 10.65;
mini_header_recess = 0.8;
mini_screw_d = 1.6;         // PROVISIONAL M2 self-tapping pilot; test in filament
mini_post_h = 1.5;          // 2 mm display-face projection enters recess by 0.5
mini_pilot_skin = 0.5;      // only 1 mm screw grip: retention MUST be tested
mini_hole_pitch_x = 25; mini_hole_pitch_y = 19; // STEP holes diameter ~2.2
mini_pos = [[-78,47],[-40,47],[-2,47],[-78,-15],[-40,-15],[-2,-15]];

// --- Main 2.8in display -------------------------------------------------------
// Supplied figures remain intentionally distinct and not independently verified.
main_module_w = 65; main_module_h = 50; main_module_t = 7;
main_pocket_w = 76.25; main_pocket_h = 53.15; // supplied outer seat dimensions
main_open_w = 67.5; main_open_h = 48;          // widened 1.0 mm from supplied 66.5
main_recess_depth = 1.2;                      // required constant, panel-normal depth
main_right_land = 2.5;                        // left land = 6.25; offset = +1.875
main_open_dx = (main_pocket_w-main_open_w)/2-main_right_land;
main_pos = [61,35];

// --- Side access placeholders (all connector values PROVISIONAL) -------------
enable_trs = true;
usb_w = 14; usb_h = 8; usb_y = 8; usb_z = 14;
dc_d = 12.5; dc_y = 57; dc_z = 14;
trs_d = 7; trs_y = 36; trs_z = 14;

// Maps local panel coordinates onto the sloped enclosure. Local z=0 is the
// finished outer panel face. This transform makes every cut and pocket normal
// to that face rather than vertical in world coordinates.
module panel_plane() {
  // Local y=0 is the footprint centre; local y +/- panel_d/2 maps to the
  // front/rear edges at the stated front_h/rear_h heights.
  translate([0,0,(front_h+rear_h)/2]) rotate([panel_angle,0,0]) children();
}

module wedge(z0, z_front, z_rear, w=case_w, d=case_d) {
  polyhedron(points=[[-w/2,-d/2,z0],[w/2,-d/2,z0],[w/2,d/2,z0],[-w/2,d/2,z0],
                     [-w/2,-d/2,z_front],[w/2,-d/2,z_front],
                     [w/2,d/2,z_rear],[-w/2,d/2,z_rear]],
             faces=[[0,1,2,3],[4,7,6,5],[0,4,5,1],[1,5,6,2],
                    [2,6,7,3],[3,7,4,0]]);
}

module panel_hole(p, d, extra=2) {
  panel_plane() translate([p[0],p[1],-panel_t-extra]) cylinder(d=d,h=panel_t+2*extra);
}

// Local outer face z=0, rear face z=-panel_t. Shared by top and coupon.
module mini_local_cuts(bezel_w,bezel_h,bezel_x,recess_depth,
                       window_w,window_h,window_x,window_y,
                       header_w,header_h,header_y,header_depth) {
  translate([bezel_x,0,-panel_t+recess_depth/2])
    cube([bezel_w,bezel_h,recess_depth],center=true);
  translate([0,header_y,-panel_t+header_depth/2])
    cube([header_w,header_h,header_depth],center=true);
  translate([window_x,window_y,-panel_t/2])
    cube([window_w,window_h,panel_t+2],center=true);
}

module mini_panel_cuts(p) {
  panel_plane() translate([p[0],p[1],0])
    mini_local_cuts(mini_bezel_w,mini_bezel_h,mini_bezel_x,mini_recess_depth,
                    mini_window_w,mini_window_h,mini_window_x,mini_window_y,
                    mini_header_w,mini_header_h,mini_header_y,mini_header_recess);
}

module main_panel_cuts() {
  panel_plane() {
    // Exact supplied 76.25 x 53.15 pocket, cut 1.2 mm along the panel normal.
    translate([main_pos[0],main_pos[1],-main_recess_depth/2])
      cube([main_pocket_w,main_pocket_h,main_recess_depth],center=true);
    translate([main_pos[0]+main_open_dx,main_pos[1],-panel_t/2])
      cube([main_open_w,main_open_h,panel_t+2],center=true);
  }
}

// This part is deliberately just a planar slab in a sloped plane: no skirt,
// perimeter wall, shell, or display bosses are part of the control panel.
module enclosure_top() {
  difference() {
    // panel_plane() local z=0 is the finished outer face; slab extends inward.
    panel_plane() translate([0,0,-panel_t/2]) cube([case_w,panel_d,panel_t],center=true);
    for (p=switches) panel_hole(p,switch_hole_d);
    for (p=mini_pos) mini_panel_cuts(p);
    main_panel_cuts();
    for (p=boss_xy) panel_hole(p,m3_clear_d);
  }
}

module m3_insert_boss(p) {
  // Case-owned boss: insert opening faces the panel and bolt enters from above.
  panel_plane() translate([p[0],p[1],-panel_t]) rotate([180,0,0]) difference() {
    cylinder(d=m3_boss_d,h=m3_boss_h);
    cylinder(d=m3_insert_d,h=m3_insert_depth+0.1);
  }
}

module m3_boss_support(p) {
  // Short case-owned ribs guarantee each insert boss joins an enclosure wall.
  panel_plane()
    if (p[0] != 0)
      translate([min(p[0],p[0] < 0 ? -case_w/2 : case_w/2),p[1],-panel_t-3])
        cube([abs((p[0] < 0 ? -case_w/2 : case_w/2)-p[0]),2.8,3.1]);
    else
      translate([-1.4,min(p[1],p[1] < 0 ? -panel_d/2 : panel_d/2),-panel_t-3])
        cube([2.8,abs((p[1] < 0 ? -panel_d/2 : panel_d/2)-p[1]),3.1]);
}

module mini_post(p) {
  // Case-owned standoff at PCB front: screw passes through the STEP's ~2.2 mm
  // through-hole from behind into the 1 mm blind pilot opening at the post tip.
  panel_plane() translate([p[0],p[1],-panel_t])
    mini_local_post(mini_post_h,mini_screw_d,mini_pilot_skin);
}

// Base on rear face (z=0); blind pilot opens at the negative-z tip.
module mini_local_post(height, pilot_d, skin, overlap=0) {
  difference() {
    // Coupon overlaps its slab by 0.1; case post keeps its original height.
    translate([0,0,-height]) cylinder(d=4.2,h=height+overlap);
    translate([0,0,-height-0.1]) cylinder(d=pilot_d,h=height-skin+0.1);
  }
}

module mini_underpanel_structure(p) {
  // Route +Y rail OUTSIDE the PCB edge: the old rail through y=+9.5 crossed
  // the through-hole header row. Short arms join posts without crossing pins.
  for (dx=[-mini_hole_pitch_x/2,mini_hole_pitch_x/2], dy=[-mini_hole_pitch_y/2,mini_hole_pitch_y/2]) {
    mini_post([p[0]+dx,p[1]+dy]);
    rail_y = dy > 0 ? mini_module_h/2+1.4 : dy;
    panel_plane() translate([-case_w/2,p[1]+rail_y-1.2,-panel_t-0.8])
      cube([p[0]+dx-(-case_w/2),2.4,0.8]);
    if (dy > 0) panel_plane() translate([p[0]+dx-1.2,p[1]+dy,-panel_t-0.8])
      cube([2.4,rail_y-dy,0.8]);
  }
}

module connector_cutouts() {
  translate([-case_w/2-wall-1,usb_y-usb_w/2,usb_z-usb_h/2]) cube([2*wall+2,usb_w,usb_h]);
  translate([-case_w/2-1,dc_y,dc_z]) rotate([0,90,0]) cylinder(d=dc_d,h=wall+3);
  if (enable_trs) translate([-case_w/2-1,trs_y,trs_z]) rotate([0,90,0]) cylinder(d=trs_d,h=wall+3);
}

module enclosure_bottom() {
  // The rear case is an open-top sloped-wall tub. Its widened upper rim is the
  // panel support ledge; all enclosure walls and display posts live here.
  bottom_front = front_h-panel_t*cos(panel_angle);
  bottom_rear = rear_h-panel_t*cos(panel_angle);
  difference() {
    union() {
      difference() {
        wedge(0,bottom_front,bottom_rear);
        wedge(floor,bottom_front+1,bottom_rear+1,
              case_w-2*(wall+ledge_w),case_d-2*(wall+ledge_w));
      }
      for (p=boss_xy) { m3_insert_boss(p); m3_boss_support(p); }
      for (p=mini_pos) mini_underpanel_structure(p);
    }
    connector_cutouts();
  }
}

module layout_preview() {
  color("dimgray") enclosure_bottom();
  color("gainsboro") enclosure_top();
  panel_plane() {
    color("deepskyblue",0.7) for (p=mini_pos) translate([p[0]+mini_window_x,p[1]+mini_window_y,0.15]) cube([mini_window_w,mini_window_h,0.3],center=true);
    color("orange",0.7) translate([main_pos[0]+main_open_dx,main_pos[1],0.15]) cube([main_open_w,main_open_h,0.3],center=true);
  }
}

module mini_display_mount(p=[0,0]) { mini_underpanel_structure(p); }
module main_display_mount() { main_panel_cuts(); }
module pcb_standoff(p=[0,0]) { m3_insert_boss(p); } // retained public hook; PCB unknown
module footswitch_cutout(p=[0,0]) { panel_hole(p,switch_hole_d); }
module venting() {}

module fit_test_mini(bezel_w=mini_bezel_w,bezel_h=mini_bezel_h,
                     bezel_x=mini_bezel_x,recess_depth=mini_recess_depth,
                     window_w=mini_window_w,window_h=mini_window_h,
                     window_x=mini_window_x,window_y=mini_window_y,
                     header_w=mini_header_w,header_h=mini_header_h,
                     header_y=mini_header_y,header_depth=mini_header_recess) {
  // Clip one display from the top panel in local coordinates: face z=0,
  // rear z=-panel_t; 12.7 mm border around the 30 x 24 PCB footprint.
  // No posts, holes or rails; all XY cuts use production defaults.
  assert(recess_depth > 0 && recess_depth < panel_t && header_depth > 0 && header_depth < panel_t);
  difference() {
    translate([0,0,-panel_t/2])
      cube([mini_module_w+25.4,mini_module_h+25.4,panel_t],center=true);
    mini_local_cuts(bezel_w,bezel_h,bezel_x,recess_depth,
                    window_w,window_h,window_x,window_y,
                    header_w,header_h,header_y,header_depth);
  }
}

module fit_test_main() {
  difference() {
    translate([-(main_pocket_w+18)/2,-(main_pocket_h+18)/2,0]) cube([main_pocket_w+18,main_pocket_h+18,panel_t]);
    translate([0,0,panel_t-main_recess_depth/2]) cube([main_pocket_w,main_pocket_h,main_recess_depth],center=true);
    translate([main_open_dx,0,panel_t/2]) cube([main_open_w,main_open_h,panel_t+2],center=true);
  }
}

if (is_undef(render_mode) || render_mode == "assembly") { enclosure_bottom(); enclosure_top(); }
else if (render_mode == "top") enclosure_top();
else if (render_mode == "bottom") enclosure_bottom();
else if (render_mode == "layout") layout_preview();
else if (render_mode == "fit_mini") fit_test_mini();
else if (render_mode == "fit_main") fit_test_main();
else { enclosure_bottom(); enclosure_top(); }
