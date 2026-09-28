/*
  Ignitron enclosure v2 -- panel-on-rear-case fit architecture (millimetres).
  Reinforced-top variant. render_mode: assembly, top (reinforced panel),
  bottom (rear case), layout,
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
panel_edge_overhang = panel_t*abs(tan(panel_angle))+0.2; // material for vertical front/rear edge trimming

// --- Underside reinforcement (PROVISIONAL; see README) ----------------------
rib_width = 3;
rib_depth = 4;
rib_overlap = 0.25;           // overlaps panel to ensure fused ribs

// --- Panel-to-case hardware --------------------------------------------------
m3_clear_d = 3.4;
m3_insert_d = 4.6;            // PROVISIONAL: confirm actual heat-set insert
m3_insert_depth = 5.2;
m3_boss_d = 10;
// These six positions sit in the case rim, clear of the panel openings.
boss_xy = [[-103,-70],[103,-70],[-103,70],[103,70],[0,-70],[0,70]];

// --- Footswitches -------------------------------------------------------------
switch_hole_d = 12.2;         // legacy value; NOT current-hardware verified
switches = [[-78,14],[-40,14],[-2,14],[-78,-48],[-40,-48],[-2,-48],
            [42,-48],[80,-48]]; // 38 mm pitch; MODE/TUNER pair centered at main display X=61

// --- Six ST7735S 160x80 TFTs; STEP XY centred on 30 x 24 PCB -------------
// STEP: PCB z=0..1.2, glass/bezel to z=2.9; 8 straight through-pins
// on +Y edge, z=-8..3 (incl. header base). PCB front is on 1.5 mm posts.
mini_module_w = 30; mini_module_h = 24; mini_module_t = 1.2;
// User fit target is a 24 x 13.5 mm front viewing aperture.
mini_window_w = 24; mini_window_h = 13.5;
mini_window_x = 2.0; mini_window_y = 0; // actual viewport is +2mm from PCB centre toward flex side
// Current rear housing relief dimensions are provisional fit targets.
mini_bezel_w = 29.15; mini_bezel_h = 13.85; mini_bezel_x = 0.175; // rear pocket width -0.4 mm, height -0.5 mm
mini_recess_depth = 2.0; // user measured PCB-top to display-face height
// Header top includes solder pads at y=9.38..11.92, x=-10.16..10.16.
// This separate rear relief clears the front-projecting pins/base; no socket.
mini_header_w = 20.82; mini_header_h = 3.14; mini_header_y = 10.65;
mini_header_recess = 2.0;
// PCB/rear-feature centres sit 2 mm left so the +2 mm viewport offset remains
// centered on each footswitch when viewed from the top surface.
mini_pos = [[-80,35],[-42,35],[-4,35],[-80,-27],[-42,-27],[-4,-27]];

// --- Main 2.8in display -------------------------------------------------------
// Supplied figures remain intentionally distinct and not independently verified.
main_module_w = 65; main_module_h = 50; main_module_t = 7;
main_pocket_w = 76.75; main_pocket_h = 53.65; // +0.5mm after full-top print measured undersize
main_pocket_radius = 1.5;                    // approved reinforced/test outer recess radius
main_open_w = 68.0; main_open_h = 48;         // square through-opening
main_recess_depth = 1.45;                     // 0.25 mm deeper after test-piece fit
main_right_land = 2.5;                        // left land = 6.25; offset = +1.875
main_open_dx = (main_pocket_w-main_open_w)/2-main_right_land;
main_pos = [61,35];

// --- Rear access placeholders (all connector values PROVISIONAL) -------------
enable_trs = true;
usb_w = 14; usb_h = 8; usb_x = -74.5; usb_z = 14;
trs_d = 7; trs_x = -46.5; trs_z = 14;
dc_d = 12.5; dc_x = -25.5; dc_z = 14;

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

// Outside dimensions are retained by shrinking the square before rounding.
module main_pocket_profile(w=main_pocket_w,r=main_pocket_radius,h=main_pocket_h) {
  offset(r=r) offset(delta=-r) square([w,h],center=true);
}

module main_panel_cuts() {
  panel_plane() {
    // Rounded outer recess; its floor is 1.2 mm below the sloped panel face.
    translate([main_pos[0],main_pos[1],-main_recess_depth])
      linear_extrude(height=main_recess_depth+0.02) main_pocket_profile();
    translate([main_pos[0]+main_open_dx,main_pos[1],-panel_t/2])
      cube([main_open_w,main_open_h,panel_t+2],center=true);
  }
}

// This part is deliberately just a planar slab in a sloped plane: no skirt,
// perimeter wall, shell, or display bosses are part of the control panel.
module enclosure_top_unreinforced() {
  difference() {
    // Sloped slab clipped by a vertical footprint. This makes all four outer
    // perimeter faces vertical and flush with the rear-case walls.
    intersection() {
      panel_plane() translate([0,0,-panel_t/2])
        cube([case_w,panel_d+2*panel_edge_overhang,panel_t],center=true);
      translate([-case_w/2,-case_d/2,-1]) cube([case_w,case_d,rear_h+2]);
    }
    for (p=switches) panel_hole(p,switch_hole_d);
    for (p=mini_pos) mini_panel_cuts(p);
    main_panel_cuts();
    for (p=boss_xy) panel_hole(p,m3_clear_d);
  }
}

// Rounded constant-width paths; endpoints and elbows have no sharp stress corners.
module rib_path(a,b) {
  hull() {
    translate(a) circle(d=rib_width);
    translate(b) circle(d=rib_width);
  }
}

module rounded_rib_ring(cx,cy,w,h,r=5) {
  translate([cx,cy]) difference() {
    offset(r=r) square([w-2*r,h-2*r],center=true);
    offset(r=r-rib_width)
      square([w-2*r,h-2*r],center=true);
  }
}

module reinforcement_lattice() {
  // All XY dimensions are in the sloped panel plane. The three paired mini
  // PCB/switch columns share their side ribs, rather than a second grid.
  // Inner sides are >=1.5 mm beyond the 30x24 PCBs; inner top/bottom
  // edges clear the +Y headers and 13 mm switch nut envelopes (nominally
  // about 0.8 mm at the upper header, 1 mm at the lower PCB edge).
  panel_plane() translate([0,0,-panel_t-rib_depth])
    linear_extrude(height=rib_depth+rib_overlap)
      union() {
        for (x=[-80,-42,-4]) {
          rounded_rib_ring(x,23,39,56);   // y=-5..51; upper PCB + switch
          if (x != -4) rounded_rib_ring(x,-38,39,54); // y=-65..-11
        }
        // Bottom of the third lower ring would hit the centre front M3 column.
        // Keep its sides and top, and omit only the boss-facing bottom span.
        difference() {
          rounded_rib_ring(-4,-38,39,54);
          translate([0,-65]) square([20,12],center=true);
        }
        // Central cross-corridor joins both banks and the main-display frame.
        rib_path([-80,-7],[19.5,-7]);
        for (x=[-80,-42,-4]) {
          rib_path([x,-7],[x,-4]);
          rib_path([x,-12],[x,-7]);
        }
        // Three-sided main seat frame: inset from the inside of the case wall,
        // and open at top right to miss the rear-right M3 column. Inner edges
        // lie >=1 mm outside the 76.75 x 53.65 R1.5 pocket.
        rib_path([19.5,4.2],[19.5,65.8]);
        rib_path([19.5,65.8],[89,65.8]);
        rib_path([19.5,4.2],[102,4.2]);
        rib_path([102,4.2],[102,59.5]);
        rib_path([19.5,-7],[19.5,4.2]);
        // Isolated MODE/TUNER nuts: 1.5 mm air gap to the 13 mm envelope.
        // Stems connect both circular collars directly to the main frame.
        for (x=[42,80]) {
          translate([x,-48]) difference() {
            circle(r=17.5);
            circle(r=14.5);
          }
          rib_path([x,-31],[x,4.2]);
        }
      }
}

module enclosure_top() {
  union() {
    enclosure_top_unreinforced();
    reinforcement_lattice();
  }
}

module m3_insert_boss(p) {
  // One vertical column overlaps the floor and the adjacent inner rim/wall.
  // Clip its cap to the actual panel underside (not the case's approximate
  // wedge roof), so no part projects into the mating panel.
  intersection() {
    translate([p[0],p[1],floor-0.3])
      cylinder(d=m3_boss_d,h=rear_h+1);
     wedge(0,front_h-panel_t/cos(panel_angle),rear_h-panel_t/cos(panel_angle));
  }
}

module m3_insert_bore(p) {
  // Cut through the entire fused shell/column union, not just the column: the rim
  // overlaps the boss at these locations. Blind depth stays panel-normal.
  panel_plane() translate([p[0],p[1],-panel_t+0.1]) rotate([180,0,0])
    cylinder(d=m3_insert_d,h=m3_insert_depth+0.1);
}

module connector_cutouts() {
  // The current inner cavity is inset by wall+ledge_w, so rear wall thickness
  // is wall+ledge_w. Center each cutter on the wall and overcut both faces.
  port_cut_depth = wall+ledge_w+2;
  port_y = case_d/2-(wall+ledge_w)/2;
  translate([usb_x,port_y,usb_z])
    cube([usb_w,port_cut_depth,usb_h],center=true);
  translate([dc_x,port_y,dc_z]) rotate([90,0,0])
    cylinder(d=dc_d,h=port_cut_depth,center=true);
  if (enable_trs) translate([trs_x,port_y,trs_z]) rotate([90,0,0])
    cylinder(d=trs_d,h=port_cut_depth,center=true);
}

module enclosure_bottom() {
  // The rear case is an open-top sloped-wall tub. Its widened upper rim is the
  // panel support ledge; display support/retention is not yet designed.
  // The underside plane at fixed world Y is panel_t/cos(angle) below the face.
  bottom_front = front_h-panel_t/cos(panel_angle);
  bottom_rear = rear_h-panel_t/cos(panel_angle);
  difference() {
    union() {
      difference() {
        wedge(0,bottom_front,bottom_rear);
        wedge(floor,bottom_front+1,bottom_rear+1,
              case_w-2*(wall+ledge_w),case_d-2*(wall+ledge_w));
      }
      for (p=boss_xy) m3_insert_boss(p);
    }
    connector_cutouts();
    for (p=boss_xy) m3_insert_bore(p);
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

module fit_test_main(pocket_w=main_pocket_w,open_w=main_open_w,pocket_r=main_pocket_radius) {
  difference() {
    translate([-(pocket_w+18)/2,-(main_pocket_h+18)/2,0]) cube([pocket_w+18,main_pocket_h+18,panel_t]);
    translate([0,0,panel_t-main_recess_depth])
      linear_extrude(height=main_recess_depth+0.02)
        main_pocket_profile(pocket_w,pocket_r);
    translate([main_open_dx,0,panel_t/2]) cube([open_w,main_open_h,panel_t+2],center=true);
  }
}

if (is_undef(render_mode) || render_mode == "assembly") { enclosure_bottom(); enclosure_top(); }
else if (render_mode == "top") enclosure_top();
else if (render_mode == "bottom") enclosure_bottom();
else if (render_mode == "layout") layout_preview();
else if (render_mode == "fit_mini") fit_test_mini();
else if (render_mode == "fit_main") fit_test_main();
else { enclosure_bottom(); enclosure_top(); }
