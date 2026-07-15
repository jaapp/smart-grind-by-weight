#include "edge_progress_ring.h"
#include <cmath>
#include <algorithm>

// Geometry of the border track (see theme.h for the tunables)
static constexpr float kThickness = THEME_EDGE_PROGRESS_THICKNESS_PX;
static constexpr float kCornerRadius = THEME_EDGE_PROGRESS_CORNER_RADIUS_PX;
static constexpr float kPi = 3.14159265f;

void EdgeProgressRing::create(lv_obj_t* parent) {
    ring_ = lv_obj_create(parent);
    lv_obj_set_size(ring_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(ring_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(ring_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring_, 0, 0);
    lv_obj_set_style_pad_all(ring_, 0, 0);
    lv_obj_set_style_radius(ring_, 0, 0);
    lv_obj_clear_flag(ring_, LV_OBJ_FLAG_CLICKABLE);   // Never intercept touch
    lv_obj_clear_flag(ring_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(ring_, draw_event_cb, LV_EVENT_DRAW_MAIN, this);

    lv_obj_add_flag(ring_, LV_OBJ_FLAG_HIDDEN);
}

void EdgeProgressRing::show() {
    if (ring_) lv_obj_clear_flag(ring_, LV_OBJ_FLAG_HIDDEN);
}

void EdgeProgressRing::hide() {
    if (ring_) lv_obj_add_flag(ring_, LV_OBJ_FLAG_HIDDEN);
}

void EdgeProgressRing::set_progress(int percent) {
    percent = std::clamp(percent, 0, 100);
    if (percent == progress_percent_) return;
    progress_percent_ = percent;
    if (ring_) lv_obj_invalidate(ring_);
}

void EdgeProgressRing::draw_event_cb(lv_event_t* e) {
    auto* self = static_cast<EdgeProgressRing*>(lv_event_get_user_data(e));
    lv_layer_t* layer = lv_event_get_layer(e);
    if (self && layer) {
        self->draw(layer);
    }
}

void EdgeProgressRing::draw(lv_layer_t* layer) {
    // Background track (full path), then the progress indicator on top
    draw_path(layer, lv_color_hex(0x333333), 1e9f);
    if (progress_percent_ > 0) {
        // Total path length is computed inside draw_path; scale by percent there
        draw_path(layer, lv_color_hex(THEME_COLOR_PRIMARY), -(float)progress_percent_);
    }
}

// path_budget semantics: > 0 = absolute pixel budget; < 0 = -percent (0..-100),
// converted to a fraction of the full perimeter.
void EdgeProgressRing::draw_path(lv_layer_t* layer, lv_color_t color, float path_budget) {
    lv_area_t coords;
    lv_obj_get_coords(ring_, &coords);
    const float W = (float)lv_area_get_width(&coords);
    const float H = (float)lv_area_get_height(&coords);
    const float ox = (float)coords.x1;   // screen-space origin of the object
    const float oy = (float)coords.y1;

    const float inset = kThickness / 2.0f + 1.0f;   // center-line of the stroke
    const float x0 = inset, y0 = inset;
    const float x1 = W - inset, y1 = H - inset;
    const float r = kCornerRadius;

    const float top_half = (x1 - r) - (W / 2.0f);   // top-center to corner start
    const float side     = (y1 - r) - (y0 + r);     // vertical straights
    const float bottom   = (x1 - r) - (x0 + r);     // bottom straight
    const float corner   = (kPi / 2.0f) * r;        // quarter-arc length
    // Segments swept: two top halves (= one full top edge), two sides, the bottom
    // edge, and four quarter corners.
    const float perimeter = 2.0f * top_half + 2.0f * side + bottom + 4.0f * corner;

    float budget = (path_budget < 0.0f)
                       ? perimeter * (-path_budget / 100.0f)
                       : path_budget;

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = color;
    line_dsc.width = (int32_t)kThickness;
    line_dsc.round_start = 1;
    line_dsc.round_end = 1;

    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.color = color;
    arc_dsc.width = (int32_t)kThickness;
    arc_dsc.radius = (int32_t)r;
    arc_dsc.rounded = 1;

    // Draw a straight segment up to the remaining budget. Returns false when the
    // budget ran out mid-segment (nothing further should be drawn).
    auto draw_line_seg = [&](float ax, float ay, float bx, float by) -> bool {
        float len = std::abs(bx - ax) + std::abs(by - ay);   // axis-aligned only
        if (len <= 0.5f) return true;
        float t = 1.0f;
        bool complete = true;
        if (budget < len) {
            t = budget / len;
            complete = false;
        }
        line_dsc.p1.x = ox + ax;
        line_dsc.p1.y = oy + ay;
        line_dsc.p2.x = ox + ax + (bx - ax) * t;
        line_dsc.p2.y = oy + ay + (by - ay) * t;
        lv_draw_line(layer, &line_dsc);
        budget -= len;
        return complete;
    };

    // Draw a quarter corner arc (start_deg -> start_deg+90, clockwise) up to budget.
    auto draw_corner = [&](float cx, float cy, float start_deg) -> bool {
        float t = 1.0f;
        bool complete = true;
        if (budget < corner) {
            t = budget / corner;
            complete = false;
        }
        if (t > 0.002f) {
            arc_dsc.center.x = (int32_t)(ox + cx);
            arc_dsc.center.y = (int32_t)(oy + cy);
            arc_dsc.start_angle = (lv_value_precise_t)start_deg;
            arc_dsc.end_angle = (lv_value_precise_t)(start_deg + 90.0f * t);
            lv_draw_arc(layer, &arc_dsc);
        }
        budget -= corner;
        return complete;
    };

    // Clockwise from top-center (12 o'clock), Apple Watch style
    if (!draw_line_seg(W / 2.0f, y0, x1 - r, y0)) return;        // top, right half
    if (!draw_corner(x1 - r, y0 + r, 270.0f)) return;            // top-right corner
    if (!draw_line_seg(x1, y0 + r, x1, y1 - r)) return;          // right edge
    if (!draw_corner(x1 - r, y1 - r, 0.0f)) return;              // bottom-right corner
    if (!draw_line_seg(x1 - r, y1, x0 + r, y1)) return;          // bottom edge
    if (!draw_corner(x0 + r, y1 - r, 90.0f)) return;             // bottom-left corner
    if (!draw_line_seg(x0, y1 - r, x0, y0 + r)) return;          // left edge
    if (!draw_corner(x0 + r, y0 + r, 180.0f)) return;            // top-left corner
    draw_line_seg(x0 + r, y0, W / 2.0f, y0);                     // top, left half
}
