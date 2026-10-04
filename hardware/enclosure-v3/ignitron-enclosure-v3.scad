/*
  Ignitron enclosure v3 top-panel concept.
  This is a separate concept file: V2 remains unchanged and supplies the
  enclosure shell, panel construction, and measured/provisional cut geometry.
  Coordinates below are in the sloped panel plane (millimetres).
*/

render_mode = "top";
include <../enclosure-v2/ignitron-enclosure-v2.scad>;

// Increase the front-to-rear footprint to make room for a larger gap between
// the main display and the two mini-display/switch rows.
case_d = 185;

// Four perimeter-corner mounting holes; the centre pair from V2 is omitted.
boss_xy = [[-103,-84],[103,-84],[-103,84],[103,84]];

// Three display/switch pairs in each lower row.  The mini display's viewing
// aperture is offset +2 mm from its PCB centre, so shift each switch +2 mm to
// centre the actual display opening over its footswitch opening.
mini_pos = [[-70,-52], [0,-52], [70,-52],
            [-70,8],   [0,8],   [70,8]];
switches = [[-68,-72], [2,-72], [72,-72],
            [-68,-12], [2,-12], [72,-12],
            [-68,60], [72,60]]; // MODE/TUNER aligned with outer switches

// Main 2.8in display is centred horizontally in the top row.
main_pos = [0,60];

// V3 rear reinforcement.  The lattice is positioned around the V3 openings,
// rather than reusing V2's fixed-coordinate lattice.
rib_width = 3;
rib_depth = 4;
rib_overlap = 0.25;

module v3_rib_path(a,b) {
  hull() {
    translate(a) circle(d=rib_width);
    translate(b) circle(d=rib_width);
  }
}

module v3_rib_ring(cx,cy,w,h,r=5) {
  translate([cx,cy]) difference() {
    offset(r=r) square([w-2*r,h-2*r],center=true);
    offset(r=r-rib_width) square([w-2*r,h-2*r],center=true);
  }
}

module v3_reinforcement() {
  panel_plane() translate([0,0,-panel_t-rib_depth])
    linear_extrude(height=rib_depth+rib_overlap)
      union() {
        // Each rib frame surrounds one display and its switch, leaving the
        // display/header and switch openings clear. Raise only the top edge
        // 2 mm: its inner edge now clears the header relief by 0.78 mm.
        for (x=[-70,0,70], y=[-52,8])
          v3_rib_ring(x,y-9,40,50);

        // Tie adjacent display/switch frames across each row and between rows.
        // Land on rib centrelines, not outer edges or rounded corner voids.
        for (y=[-62,-2]) {
          v3_rib_path([-51.5,y],[-18.5,y]);
          v3_rib_path([18.5,y],[51.5,y]);
        }
        for (x=[-70,0,70]) v3_rib_path([x,-37.5],[x,-24.5]);

        // Main-display frame: the lower rail joins the centre mini frame
        // without crossing the recess (rail edge 32.5, recess edge 33.425).
        v3_rib_path([-42,31],[-42,89]);
        v3_rib_path([42,31],[42,89]);
        v3_rib_path([-42,89],[42,89]);
        v3_rib_path([-42,31],[42,31]);
        // Join the upper mini-display frames to the main-display support, and
        // connect the main frame directly to the MODE/TUNER collars.
        v3_rib_path([-55,22.5],[-42,31]);
        v3_rib_path([0,22.5],[0,31]);
        v3_rib_path([55,22.5],[42,31]);
        // Stop in each collar's display-facing wall, never across its bore.
        // Rounded caps overlap the annulus by 2 mm and clear the bore by 1 mm.
        v3_rib_path([-42,60],[-56,60]);
        v3_rib_path([42,60],[60,60]);
        for (x=[-68,72])
          translate([x,60]) difference() {
            // 19 mm clear bore leaves extra clearance around switch hardware;
            // the 3 mm annulus increases bearing area under the panel.
            circle(r=12.5);
            circle(r=9.5);
          }
      }
}

// Preserve V2's cut geometry and add V3 rear ribs as one fused panel.
module enclosure_top() {
  union() {
    difference() {
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
    v3_reinforcement();
  }
}

// The inherited V2 render selector invokes the V3 enclosure_top override, so
// render_mode=top/bottom/assembly exports only the requested part(s).  Both
// the top clearance holes and inherited bottom insert bores use boss_xy.
