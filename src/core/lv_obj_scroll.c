/**
 * @file lv_obj_scroll.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_obj_scroll_private.h"
#include "../misc/lv_anim_private.h"
#include "lv_obj_private.h"
#include "../indev/lv_indev.h"
#include "../indev/lv_indev_private.h"
#include "../indev/lv_indev_scroll.h"
#include "../display/lv_display.h"
#include "../misc/lv_area.h"

/*********************
 *      DEFINES
 *********************/
#define MY_CLASS (&lv_obj_class)
#ifndef SCROLL_ANIM_TIME_MIN
    #define SCROLL_ANIM_TIME_MIN    200    /*ms*/
#endif
#ifndef SCROLL_ANIM_TIME_MAX
    #define SCROLL_ANIM_TIME_MAX    400    /*ms*/
#endif
#define SCROLLBAR_MIN_SIZE      (LV_DPX(10))

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  GLOBAL PROTOTYPES
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void scroll_x_anim(void * obj, int32_t v);
static void scroll_y_anim(void * obj, int32_t v);
static void scroll_end_cb(lv_anim_t * a);
static void scroll_throw_end_cb(lv_anim_t * a);
static void scroll_area_into_view(const lv_area_t * area, lv_obj_t * child, lv_point_t * scroll_value,
                                  lv_anim_enable_t anim_en);
static int32_t scroll_anim_end_value(lv_anim_t * a, int32_t v);
/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/*=====================
 * Setter functions
 *====================*/

void lv_obj_set_scrollbar_mode(lv_obj_t * obj, lv_scrollbar_mode_t mode)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_obj_allocate_spec_attr(obj);

    if(obj->spec_attr->scrollbar_mode == mode) return;
    obj->spec_attr->scrollbar_mode = mode;
    lv_obj_invalidate(obj);
}

void lv_obj_set_scroll_dir(lv_obj_t * obj, lv_dir_t dir)
{
    lv_obj_allocate_spec_attr(obj);

    if(dir != obj->spec_attr->scroll_dir) {
        obj->spec_attr->scroll_dir = dir;
    }
}

void lv_obj_set_scroll_snap_x(lv_obj_t * obj, lv_scroll_snap_t align)
{
    lv_obj_allocate_spec_attr(obj);
    obj->spec_attr->scroll_snap_x = align;
}

void lv_obj_set_scroll_snap_y(lv_obj_t * obj, lv_scroll_snap_t align)
{
    lv_obj_allocate_spec_attr(obj);
    obj->spec_attr->scroll_snap_y = align;
}

/*=====================
 * Getter functions
 *====================*/

lv_scrollbar_mode_t lv_obj_get_scrollbar_mode(const lv_obj_t * obj)
{
    if(obj->spec_attr) return (lv_scrollbar_mode_t) obj->spec_attr->scrollbar_mode;
    else return LV_SCROLLBAR_MODE_AUTO;
}

lv_dir_t lv_obj_get_scroll_dir(const lv_obj_t * obj)
{
    if(obj->spec_attr) return (lv_dir_t) obj->spec_attr->scroll_dir;
    else return LV_DIR_ALL;
}

lv_scroll_snap_t lv_obj_get_scroll_snap_x(const lv_obj_t * obj)
{
    if(obj->spec_attr) return (lv_scroll_snap_t) obj->spec_attr->scroll_snap_x;
    else return LV_SCROLL_SNAP_NONE;
}

lv_scroll_snap_t lv_obj_get_scroll_snap_y(const lv_obj_t * obj)
{
    if(obj->spec_attr) return (lv_scroll_snap_t) obj->spec_attr->scroll_snap_y;
    else return LV_SCROLL_SNAP_NONE;
}

int32_t lv_obj_get_scroll_x(const lv_obj_t * obj)
{
    if(obj->spec_attr == NULL) return 0;
    return -obj->spec_attr->scroll.x;
}

int32_t lv_obj_get_scroll_y(const lv_obj_t * obj)
{
    if(obj->spec_attr == NULL) return 0;
    return -obj->spec_attr->scroll.y;
}

int32_t lv_obj_get_scroll_top(const lv_obj_t * obj)
{
    if(obj->spec_attr == NULL) return 0;
    return -obj->spec_attr->scroll.y;
}

int32_t lv_obj_get_scroll_bottom(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    int32_t child_res = LV_COORD_MIN;
    uint32_t i;
    uint32_t child_cnt = lv_obj_get_child_count(obj);
    for(i = 0; i < child_cnt; i++) {
        const lv_obj_t * child = obj->spec_attr->children[i];
        if(lv_obj_has_flag_any(child,  LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_FLOATING)) continue;

        int32_t tmp_y = child->coords.y2 + lv_obj_get_style_margin_bottom(child, LV_PART_MAIN);
        child_res = LV_MAX(child_res, tmp_y);
    }

    int32_t space_top = lv_obj_get_style_space_top(obj, LV_PART_MAIN);
    int32_t space_bottom = lv_obj_get_style_space_bottom(obj, LV_PART_MAIN);

    if(child_res != LV_COORD_MIN) {
        child_res -= (obj->coords.y2 - space_bottom);
    }

    int32_t self_h = lv_obj_get_self_height(obj);
    self_h = self_h - (lv_obj_get_height(obj) - space_top - space_bottom);
    self_h -= lv_obj_get_scroll_y(obj);
    return LV_MAX(child_res, self_h);
}

int32_t lv_obj_get_scroll_left(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    /*Normally can't scroll the object out on the left.
     *So simply use the current scroll position as "left size"*/
    if(lv_obj_get_style_base_dir(obj, LV_PART_MAIN) != LV_BASE_DIR_RTL) {
        if(obj->spec_attr == NULL) return 0;
        return -obj->spec_attr->scroll.x;
    }

    /*With RTL base direction scrolling the left is normal so find the left most coordinate*/
    int32_t space_right = lv_obj_get_style_space_right(obj, LV_PART_MAIN);
    int32_t space_left = lv_obj_get_style_space_left(obj, LV_PART_MAIN);

    int32_t child_res = 0;

    uint32_t i;
    int32_t x1 = LV_COORD_MAX;
    uint32_t child_cnt = lv_obj_get_child_count(obj);
    for(i = 0; i < child_cnt; i++) {
        const lv_obj_t * child = obj->spec_attr->children[i];
        if(lv_obj_has_flag_any(child,  LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_FLOATING)) continue;

        int32_t tmp_x = child->coords.x1 - lv_obj_get_style_margin_left(child, LV_PART_MAIN);
        x1 = LV_MIN(x1, tmp_x);
    }

    if(x1 != LV_COORD_MAX) {
        child_res = x1;
        child_res = (obj->coords.x1 + space_left) - child_res;
    }
    else {
        child_res = LV_COORD_MIN;
    }

    int32_t self_w = lv_obj_get_self_width(obj);
    self_w = self_w - (lv_obj_get_width(obj) - space_right - space_left);
    self_w += lv_obj_get_scroll_x(obj);

    return LV_MAX(child_res, self_w);
}

int32_t lv_obj_get_scroll_right(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    /*With RTL base dir can't scroll to the object out on the right.
     *So simply use the current scroll position as "right size"*/
    if(lv_obj_get_style_base_dir(obj, LV_PART_MAIN) == LV_BASE_DIR_RTL) {
        if(obj->spec_attr == NULL) return 0;
        return obj->spec_attr->scroll.x;
    }

    /*With other base direction (LTR) scrolling to the right is normal so find the right most coordinate*/
    int32_t child_res = LV_COORD_MIN;
    uint32_t i;
    uint32_t child_cnt = lv_obj_get_child_count(obj);
    for(i = 0; i < child_cnt; i++) {
        const lv_obj_t * child = obj->spec_attr->children[i];
        if(lv_obj_has_flag_any(child,  LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_FLOATING)) continue;

        int32_t tmp_x = child->coords.x2 + lv_obj_get_style_margin_right(child, LV_PART_MAIN);
        child_res = LV_MAX(child_res, tmp_x);
    }

    int32_t space_right = lv_obj_get_style_space_right(obj, LV_PART_MAIN);
    int32_t space_left = lv_obj_get_style_space_left(obj, LV_PART_MAIN);

    if(child_res != LV_COORD_MIN) {
        child_res -= (obj->coords.x2 - space_right);
    }

    int32_t self_w;
    self_w = lv_obj_get_self_width(obj);
    self_w = self_w - (lv_obj_get_width(obj) - space_right - space_left);
    self_w -= lv_obj_get_scroll_x(obj);
    return LV_MAX(child_res, self_w);
}

void lv_obj_get_scroll_end(lv_obj_t * obj, lv_point_t * end)
{
    lv_anim_t * a;
    a = lv_anim_get(obj, scroll_x_anim);
    end->x = scroll_anim_end_value(a, lv_obj_get_scroll_x(obj));

    a = lv_anim_get(obj, scroll_y_anim);
    end->y = scroll_anim_end_value(a, lv_obj_get_scroll_y(obj));
}

/*=====================
 * Other functions
 *====================*/

void lv_obj_scroll_by_bounded(lv_obj_t * obj, int32_t dx, int32_t dy, lv_anim_enable_t anim_en)
{
    if(dx == 0 && dy == 0) return;

    /*We need to know the final sizes for bound check*/
    lv_obj_update_layout(obj);

    /*Don't let scroll more than naturally possible by the size of the content*/
    int32_t x_current = -lv_obj_get_scroll_x(obj);
    int32_t x_bounded = x_current + dx;

    if(lv_obj_get_style_base_dir(obj, LV_PART_MAIN) != LV_BASE_DIR_RTL) {
        if(x_bounded > 0) x_bounded = 0;
        if(x_bounded < 0) {
            int32_t  scroll_max = lv_obj_get_scroll_left(obj) + lv_obj_get_scroll_right(obj);
            if(scroll_max < 0) scroll_max = 0;

            if(x_bounded < -scroll_max) x_bounded = -scroll_max;
        }
    }
    else {
        if(x_bounded < 0) x_bounded = 0;
        if(x_bounded > 0) {
            int32_t  scroll_max = lv_obj_get_scroll_left(obj) + lv_obj_get_scroll_right(obj);
            if(scroll_max < 0) scroll_max = 0;

            if(x_bounded > scroll_max) x_bounded = scroll_max;
        }
    }

    /*Don't let scroll more than naturally possible by the size of the content*/
    int32_t y_current = -lv_obj_get_scroll_y(obj);
    int32_t y_bounded = y_current + dy;

    if(y_bounded > 0) y_bounded = 0;
    if(y_bounded < 0) {
        int32_t  scroll_max = lv_obj_get_scroll_top(obj) + lv_obj_get_scroll_bottom(obj);
        if(scroll_max < 0) scroll_max = 0;
        if(y_bounded < -scroll_max) y_bounded = -scroll_max;
    }

    dx = x_bounded - x_current;
    dy = y_bounded - y_current;
    if(dx || dy) {
        lv_obj_scroll_by(obj, dx, dy, anim_en);
    }
}

void lv_obj_scroll_by(lv_obj_t * obj, int32_t dx, int32_t dy, lv_anim_enable_t anim_en)
{
    if(dx == 0 && dy == 0) return;
    if(anim_en) {
        lv_display_t * d = lv_obj_get_display(obj);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, obj);
        lv_anim_set_deleted_cb(&a, scroll_end_cb);

        if(dx) {
            uint32_t t = lv_anim_speed_clamped((lv_display_get_horizontal_resolution(d)) >> 1, SCROLL_ANIM_TIME_MIN,
                                               SCROLL_ANIM_TIME_MAX);
            lv_anim_set_duration(&a, t);
            int32_t sx = lv_obj_get_scroll_x(obj);
            lv_anim_set_values(&a, -sx, -sx + dx);
            lv_anim_set_exec_cb(&a, scroll_x_anim);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_out);

            lv_result_t res;
            res = lv_obj_send_event(obj, LV_EVENT_SCROLL_BEGIN, &a);
            if(res != LV_RESULT_OK) return;
            lv_anim_start(&a);
        }

        if(dy) {
            uint32_t t = lv_anim_speed_clamped((lv_display_get_vertical_resolution(d)) >> 1, SCROLL_ANIM_TIME_MIN,
                                               SCROLL_ANIM_TIME_MAX);
            lv_anim_set_duration(&a, t);
            int32_t sy = lv_obj_get_scroll_y(obj);
            lv_anim_set_values(&a, -sy, -sy + dy);
            lv_anim_set_exec_cb(&a,  scroll_y_anim);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_out);

            lv_result_t res;
            res = lv_obj_send_event(obj, LV_EVENT_SCROLL_BEGIN, &a);
            if(res != LV_RESULT_OK) return;
            lv_anim_start(&a);
        }
    }
    else {
        /*Remove pending animations*/
        lv_anim_delete(obj, scroll_y_anim);
        lv_anim_delete(obj, scroll_x_anim);

        lv_result_t res;
        res = lv_obj_send_event(obj, LV_EVENT_SCROLL_BEGIN, NULL);
        if(res != LV_RESULT_OK) return;

        res = lv_obj_scroll_by_raw(obj, dx, dy);
        if(res != LV_RESULT_OK) return;

        res = lv_obj_send_event(obj, LV_EVENT_SCROLL_END, NULL);
        if(res != LV_RESULT_OK) return;
    }
}

void lv_obj_scroll_anim_start(lv_obj_t * obj, int32_t dx, int32_t dy)
{
    if(dx == 0 && dy == 0) return;

    lv_display_t * d = lv_obj_get_display(obj);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_deleted_cb(&a, scroll_end_cb);

    if(dx) {
        uint32_t t = lv_anim_speed_clamped((lv_display_get_horizontal_resolution(d)) >> 1,
                                           SCROLL_ANIM_TIME_MIN, SCROLL_ANIM_TIME_MAX);
        lv_anim_set_duration(&a, t);
        int32_t sx = lv_obj_get_scroll_x(obj);
        lv_anim_set_values(&a, -sx, -sx + dx);
        lv_anim_set_exec_cb(&a, scroll_x_anim);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }

    if(dy) {
        uint32_t t = lv_anim_speed_clamped((lv_display_get_vertical_resolution(d)) >> 1,
                                           SCROLL_ANIM_TIME_MIN, SCROLL_ANIM_TIME_MAX);
        lv_anim_set_duration(&a, t);
        int32_t sy = lv_obj_get_scroll_y(obj);
        lv_anim_set_values(&a, -sy, -sy + dy);
        lv_anim_set_exec_cb(&a, scroll_y_anim);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }
}

/**
 * Internal helper: compute scroll throw for one axis and start animation.
 * @param s_cur     current scroll position (scroll_x or scroll_y)
 * @param s_near    scroll margin in negative direction (scroll_top or scroll_left)
 * @param s_far     scroll margin in positive direction (scroll_bottom or scroll_right)
 * @param throw_dist predicted throw distance in scroll_by direction (positive = content moves down/right)
 */
static void scroll_throw_axis(lv_obj_t * obj, int32_t s_cur, int32_t s_near, int32_t s_far,
                               int32_t throw_dist, bool has_elastic,
                               lv_anim_scroll_throw_config_t * config, lv_anim_exec_xcb_t exec_cb)
{
    /* Convert throw_dist from scroll_by direction to scroll coordinate direction (negate) */
    int32_t natural_d = -throw_dist;

    int32_t target = s_cur + natural_d;
    int32_t boundary = s_cur;
    bool needs_overshoot = false;
    bool past_boundary = false;

    if(s_near < 0) {
        boundary = s_cur - s_near;
        target = boundary;
        past_boundary = true;
    }
    else if(s_far < 0) {
        boundary = s_cur + s_far;
        target = boundary;
        past_boundary = true;
    }
    else if(has_elastic) {
        if(natural_d > 0 && natural_d > s_far) {
            boundary = s_cur + s_far;
            needs_overshoot = true;
        }
        else if(natural_d < 0 && (-natural_d) > s_near) {
            boundary = s_cur - s_near;
            needs_overshoot = true;
        }
    }
    else {
        if(natural_d > 0 && natural_d > s_far) target = s_cur + s_far;
        else if(natural_d < 0 && (-natural_d) > s_near) target = s_cur - s_near;
    }

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, exec_cb);
    lv_anim_set_deleted_cb(&a, scroll_throw_end_cb);
    lv_anim_set_user_data(&a, config);

    if(needs_overshoot) {
        /*
         * 2-phase overshoot: throw to boundary, then overshoot+springback.
         * Phase 1 (throw_end_time): s_cur → boundary, ease_out (decelerate)
         * Phase 2 (remaining): boundary → overshoot_peak → boundary, ease_in_out
         */
        int32_t overshoot_pos = boundary + (target - boundary) / 3;

        config->boundary_value = -boundary;
        config->overshoot_peak = -overshoot_pos;

        int32_t dist_to_boundary = boundary - s_cur;
        if(dist_to_boundary < 0) dist_to_boundary = -dist_to_boundary;
        int32_t abs_dist = throw_dist > 0 ? throw_dist : -throw_dist;

        /* Phase 1 duration: proportional to boundary fraction of total distance */
        uint32_t throw_dur = abs_dist > 0 ? (uint32_t)dist_to_boundary * 300 / abs_dist : 100;
        if(throw_dur < 50) throw_dur = 50;
        if(throw_dur > 300) throw_dur = 300;

        /* Phase 2 duration: scale with overshoot amplitude */
        int32_t overshoot_amp = overshoot_pos - boundary;
        if(overshoot_amp < 0) overshoot_amp = -overshoot_amp;
        uint32_t spring_dur = 250 + (uint32_t)overshoot_amp;
        if(spring_dur > 600) spring_dur = 600;

        config->throw_end_time = throw_dur;

        lv_anim_set_values(&a, -s_cur, config->boundary_value);
        lv_anim_set_duration(&a, throw_dur + spring_dur);
        lv_anim_set_path_cb(&a, lv_anim_path_scroll_throw);
    }
    else if(past_boundary) {
        /*
         * Already past boundary (elastic drag release): springback only.
         * Reuse phase 2 of the overshoot path.
         */
        config->boundary_value = -boundary;
        config->overshoot_peak = -s_cur;  /* start from current position */
        config->throw_end_time = 0;

        int32_t bounce_dist = boundary - s_cur;
        if(bounce_dist < 0) bounce_dist = -bounce_dist;
        uint32_t dur = 200 + bounce_dist;
        if(dur > 400) dur = 400;

        lv_anim_set_values(&a, -s_cur, -boundary);
        lv_anim_set_duration(&a, dur);
        lv_anim_set_path_cb(&a, config->bounce_path_cb);
    }
    else {
        /*
         * Normal throw within bounds: simple deceleration.
         * If config has is_finished_cb, use convergence-based termination
         * with a generous max duration. The animation will end when the
         * callback returns true (e.g. remaining < 1px for expo_decay,
         * or velocity < threshold for spring curves).
         */
        lv_anim_set_values(&a, -s_cur, -target);
        lv_anim_set_path_cb(&a, config->throw_path_cb);

        if(config->is_finished_cb) {
            /* Convergence-based termination.
             * Set a large max duration as safety net; is_finished_cb will end it sooner. */
            lv_anim_set_duration(&a, 3000);
            lv_anim_set_is_finished_cb(&a, config->is_finished_cb);
        }
        else {
            /* Time-based termination for simple curves without convergence check */
            lv_display_t * d = lv_obj_get_display(obj);
            uint32_t speed = lv_display_get_horizontal_resolution(d) >> 1;
            if(speed < 10) speed = 10;
            lv_anim_set_duration(&a, lv_anim_speed_clamped(speed, 200, 1000));
        }
    }

    lv_anim_start(&a);
}

void lv_obj_scroll_anim_stop(lv_obj_t * obj)
{
    lv_anim_delete(obj, scroll_x_anim);
    lv_anim_delete(obj, scroll_y_anim);
}

void lv_obj_scroll_throw(lv_obj_t * obj, int32_t throw_dist_x, int32_t throw_dist_y,
                         lv_anim_scroll_throw_config_t * config)
{
    if(config == NULL) return;

    int32_t sx = lv_obj_get_scroll_x(obj);
    int32_t sy = lv_obj_get_scroll_y(obj);
    int32_t st = lv_obj_get_scroll_top(obj);
    int32_t sb = lv_obj_get_scroll_bottom(obj);
    int32_t sl = lv_obj_get_scroll_left(obj);
    int32_t sr = lv_obj_get_scroll_right(obj);
    bool has_elastic = lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLL_ELASTIC);

    if(throw_dist_y != 0 || st < 0 || sb < 0) {
        scroll_throw_axis(obj, sy, st, sb, throw_dist_y, has_elastic, config, scroll_y_anim);
    }
    if(throw_dist_x != 0 || sl < 0 || sr < 0) {
        scroll_throw_axis(obj, sx, sl, sr, throw_dist_x, has_elastic, config, scroll_x_anim);
    }
}

void lv_obj_scroll_to(lv_obj_t * obj, int32_t x, int32_t y, lv_anim_enable_t anim_en)
{
    lv_obj_scroll_to_x(obj, x, anim_en);
    lv_obj_scroll_to_y(obj, y, anim_en);
}

void lv_obj_scroll_to_x(lv_obj_t * obj, int32_t x, lv_anim_enable_t anim_en)
{
    lv_anim_delete(obj, scroll_x_anim);

    int32_t scroll_x = lv_obj_get_scroll_x(obj);
    int32_t diff = -x + scroll_x;

    lv_obj_scroll_by_bounded(obj, diff, 0, anim_en);
}

void lv_obj_scroll_to_y(lv_obj_t * obj, int32_t y, lv_anim_enable_t anim_en)
{
    lv_anim_delete(obj, scroll_y_anim);

    int32_t scroll_y = lv_obj_get_scroll_y(obj);
    int32_t diff = -y + scroll_y;

    lv_obj_scroll_by_bounded(obj, 0, diff, anim_en);
}

void lv_obj_scroll_to_view(lv_obj_t * obj, lv_anim_enable_t anim_en)
{
    /*Be sure the screens layout is correct*/
    lv_obj_update_layout(obj);

    lv_point_t p = {0, 0};
    scroll_area_into_view(&obj->coords, obj, &p, anim_en);
}

void lv_obj_scroll_to_view_recursive(lv_obj_t * obj, lv_anim_enable_t anim_en)
{
    /*Be sure the screens layout is correct*/
    lv_obj_update_layout(obj);

    lv_point_t p = {0, 0};
    lv_obj_t * child = obj;
    lv_obj_t * parent = lv_obj_get_parent(child);
    while(parent) {
        scroll_area_into_view(&obj->coords, child, &p, anim_en);
        child = parent;
        parent = lv_obj_get_parent(parent);
    }
}

lv_result_t lv_obj_scroll_by_raw(lv_obj_t * obj, int32_t x, int32_t y)
{
    if(x == 0 && y == 0) return LV_RESULT_OK;

    lv_obj_allocate_spec_attr(obj);

    obj->spec_attr->scroll.x += x;
    obj->spec_attr->scroll.y += y;

    lv_obj_move_children_by(obj, x, y, true);
    lv_result_t res = lv_obj_send_event(obj, LV_EVENT_SCROLL, NULL);
    if(res != LV_RESULT_OK) return res;
    lv_obj_invalidate(obj);
    return LV_RESULT_OK;
}

bool lv_obj_is_scrolling(const lv_obj_t * obj)
{
    lv_indev_t * indev = lv_indev_get_next(NULL);
    while(indev) {
        if(lv_indev_get_scroll_obj(indev) == obj) return true;
        indev = lv_indev_get_next(indev);
    }

    if(lv_anim_get((lv_obj_t *)obj, scroll_x_anim) != NULL ||
       lv_anim_get((lv_obj_t *)obj, scroll_y_anim) != NULL) {
        return true;
    }

    return false;
}

void lv_obj_stop_scroll_anim(const lv_obj_t * obj)
{
    lv_anim_delete((lv_obj_t *)obj, scroll_y_anim);
    lv_anim_delete((lv_obj_t *)obj, scroll_x_anim);
}

void lv_obj_update_snap(lv_obj_t * obj, lv_anim_enable_t anim_en)
{
    lv_obj_update_layout(obj);
    lv_point_t p;
    lv_indev_scroll_get_snap_dist(obj, &p);
    if(p.x == LV_COORD_MAX || p.x == LV_COORD_MIN) p.x = 0;
    if(p.y == LV_COORD_MAX || p.y == LV_COORD_MIN) p.y = 0;
    lv_obj_scroll_by(obj, p.x, p.y, anim_en);
}

void lv_obj_get_scrollbar_area(lv_obj_t * obj, lv_area_t * hor_area, lv_area_t * ver_area)
{
    lv_area_set(hor_area, 0, 0, -1, -1);
    lv_area_set(ver_area, 0, 0, -1, -1);

    if(lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE) == false) return;

    lv_scrollbar_mode_t sm = lv_obj_get_scrollbar_mode(obj);
    if(sm == LV_SCROLLBAR_MODE_OFF)  return;

    /*If there is no indev scrolling this object but `mode==active` return*/
    lv_indev_t * indev = lv_indev_get_next(NULL);
    if(sm == LV_SCROLLBAR_MODE_ACTIVE) {
        while(indev) {
            if(lv_indev_get_scroll_obj(indev) == obj) break;
            indev = lv_indev_get_next(indev);
        }
        if(indev == NULL)  return;
    }

    int32_t st = lv_obj_get_scroll_top(obj);
    int32_t sb = lv_obj_get_scroll_bottom(obj);
    int32_t sl = lv_obj_get_scroll_left(obj);
    int32_t sr = lv_obj_get_scroll_right(obj);

    lv_dir_t dir = lv_obj_get_scroll_dir(obj);

    bool ver_draw = false;
    if((dir & LV_DIR_VER) &&
       ((sm == LV_SCROLLBAR_MODE_ON) ||
        (sm == LV_SCROLLBAR_MODE_AUTO && (st > 0 || sb > 0)) ||
        (sm == LV_SCROLLBAR_MODE_ACTIVE && lv_indev_get_scroll_dir(indev) == LV_DIR_VER))) {
        ver_draw = true;
    }

    bool hor_draw = false;
    if((dir & LV_DIR_HOR) &&
       ((sm == LV_SCROLLBAR_MODE_ON) ||
        (sm == LV_SCROLLBAR_MODE_AUTO && (sl > 0 || sr > 0)) ||
        (sm == LV_SCROLLBAR_MODE_ACTIVE && lv_indev_get_scroll_dir(indev) == LV_DIR_HOR))) {
        hor_draw = true;
    }

    if(!hor_draw && !ver_draw) return;

    bool rtl = lv_obj_get_style_base_dir(obj, LV_PART_SCROLLBAR) == LV_BASE_DIR_RTL;

    int32_t top_space = lv_obj_get_style_pad_top(obj, LV_PART_SCROLLBAR);
    int32_t bottom_space = lv_obj_get_style_pad_bottom(obj, LV_PART_SCROLLBAR);
    int32_t left_space = lv_obj_get_style_pad_left(obj, LV_PART_SCROLLBAR);
    int32_t right_space = lv_obj_get_style_pad_right(obj, LV_PART_SCROLLBAR);
    int32_t thickness = lv_obj_get_style_width(obj, LV_PART_SCROLLBAR);
    int32_t length = lv_obj_get_style_length(obj, LV_PART_SCROLLBAR);

    int32_t obj_h = lv_obj_get_height(obj);
    int32_t obj_w = lv_obj_get_width(obj);

    /*Space required for the vertical and horizontal scrollbars*/
    int32_t ver_reg_space = ver_draw ? thickness : 0;
    int32_t hor_req_space = hor_draw ? thickness : 0;
    int32_t rem;

    if(lv_obj_get_style_bg_opa(obj, LV_PART_SCROLLBAR) <= LV_OPA_MIN &&
       lv_obj_get_style_border_opa(obj, LV_PART_SCROLLBAR) <= LV_OPA_MIN) {
        return;
    }

    /*Draw vertical scrollbar if the mode is ON or can be scrolled in this direction*/
    int32_t content_h = obj_h + st + sb;
    if(ver_draw && content_h) {
        ver_area->y1 = obj->coords.y1;
        ver_area->y2 = obj->coords.y2;
        if(rtl) {
            ver_area->x1 = obj->coords.x1 + left_space;
            ver_area->x2 = ver_area->x1 + thickness - 1;
        }
        else {
            ver_area->x2 = obj->coords.x2 - right_space;
            ver_area->x1 = ver_area->x2 - thickness + 1;
        }

        int32_t sb_h = ((obj_h - top_space - bottom_space - hor_req_space) * obj_h) / content_h;
        sb_h = LV_MAX(length > 0 ? length : sb_h, SCROLLBAR_MIN_SIZE); /*Style-defined size, calculated size, or minimum size*/
        sb_h = LV_MIN(sb_h, obj_h); /*Limit scrollbar length to parent height*/
        rem = (obj_h - top_space - bottom_space - hor_req_space) -
              sb_h;  /*Remaining size from the scrollbar track that is not the scrollbar itself*/
        int32_t scroll_h = content_h - obj_h; /*The size of the content which can be really scrolled*/
        if(scroll_h <= 0) {
            ver_area->y1 = obj->coords.y1 + top_space;
            ver_area->y2 = obj->coords.y2 - bottom_space - hor_req_space - 1;
        }
        else {
            int32_t sb_y = (rem * sb) / scroll_h;
            sb_y = rem - sb_y;

            ver_area->y1 = obj->coords.y1 + sb_y + top_space;
            ver_area->y2 = ver_area->y1 + sb_h - 1;
            if(ver_area->y1 < obj->coords.y1 + top_space) {
                ver_area->y1 = obj->coords.y1 + top_space;
                if(ver_area->y1 + SCROLLBAR_MIN_SIZE > ver_area->y2) {
                    ver_area->y2 = ver_area->y1 + SCROLLBAR_MIN_SIZE;
                }
            }
            if(ver_area->y2 > obj->coords.y2 - hor_req_space - bottom_space) {
                ver_area->y2 = obj->coords.y2 - hor_req_space - bottom_space;
                if(ver_area->y2 - SCROLLBAR_MIN_SIZE < ver_area->y1) {
                    ver_area->y1 = ver_area->y2 - SCROLLBAR_MIN_SIZE;
                }
            }
        }
    }

    /*Draw horizontal scrollbar if the mode is ON or can be scrolled in this direction*/
    int32_t content_w = obj_w + sl + sr;
    if(hor_draw && content_w) {
        hor_area->y2 = obj->coords.y2 - bottom_space;
        hor_area->y1 = hor_area->y2 - thickness + 1;
        hor_area->x1 = obj->coords.x1;
        hor_area->x2 = obj->coords.x2;

        int32_t sb_w = ((obj_w - left_space - right_space - ver_reg_space) * obj_w) / content_w;
        sb_w = LV_MAX(length > 0 ? length : sb_w, SCROLLBAR_MIN_SIZE); /*Style-defined size, calculated size, or minimum size*/
        sb_w = LV_MIN(sb_w, obj_w); /*Limit scrollbar length to parent width*/
        rem = (obj_w - left_space - right_space - ver_reg_space) -
              sb_w;  /*Remaining size from the scrollbar track that is not the scrollbar itself*/
        int32_t scroll_w = content_w - obj_w; /*The size of the content which can be really scrolled*/
        if(scroll_w <= 0) {
            if(rtl) {
                hor_area->x1 = obj->coords.x1 + left_space + ver_reg_space - 1;
                hor_area->x2 = obj->coords.x2 - right_space;
            }
            else {
                hor_area->x1 = obj->coords.x1 + left_space;
                hor_area->x2 = obj->coords.x2 - right_space - ver_reg_space - 1;
            }
        }
        else {
            int32_t sb_x = (rem * sr) / scroll_w;
            sb_x = rem - sb_x;

            if(rtl) {
                hor_area->x1 = obj->coords.x1 + sb_x + left_space + ver_reg_space;
                hor_area->x2 = hor_area->x1 + sb_w - 1;
                if(hor_area->x1 < obj->coords.x1 + left_space + ver_reg_space) {
                    hor_area->x1 = obj->coords.x1 + left_space + ver_reg_space;
                    if(hor_area->x1 + SCROLLBAR_MIN_SIZE > hor_area->x2) {
                        hor_area->x2 = hor_area->x1 + SCROLLBAR_MIN_SIZE;
                    }
                }
                if(hor_area->x2 > obj->coords.x2 - right_space) {
                    hor_area->x2 = obj->coords.x2 - right_space;
                    if(hor_area->x2 - SCROLLBAR_MIN_SIZE < hor_area->x1) {
                        hor_area->x1 = hor_area->x2 - SCROLLBAR_MIN_SIZE;
                    }
                }
            }
            else {
                hor_area->x1 = obj->coords.x1 + sb_x + left_space;
                hor_area->x2 = hor_area->x1 + sb_w - 1;
                if(hor_area->x1 < obj->coords.x1 + left_space) {
                    hor_area->x1 = obj->coords.x1 + left_space;
                    if(hor_area->x1 + SCROLLBAR_MIN_SIZE > hor_area->x2) {
                        hor_area->x2 = hor_area->x1 + SCROLLBAR_MIN_SIZE;
                    }
                }
                if(hor_area->x2 > obj->coords.x2 - ver_reg_space - right_space) {
                    hor_area->x2 = obj->coords.x2 - ver_reg_space - right_space;
                    if(hor_area->x2 - SCROLLBAR_MIN_SIZE < hor_area->x1) {
                        hor_area->x1 = hor_area->x2 - SCROLLBAR_MIN_SIZE;
                    }
                }
            }
        }
    }
}

void lv_obj_scrollbar_invalidate(lv_obj_t * obj)
{
    lv_area_t hor_area;
    lv_area_t ver_area;
    lv_obj_get_scrollbar_area(obj, &hor_area, &ver_area);

    if(lv_area_get_size(&hor_area) <= 0 && lv_area_get_size(&ver_area) <= 0) return;

    if(lv_area_get_size(&hor_area) > 0) lv_obj_invalidate_area(obj, &hor_area);
    if(lv_area_get_size(&ver_area) > 0) lv_obj_invalidate_area(obj, &ver_area);
}

void lv_obj_readjust_scroll(lv_obj_t * obj, lv_anim_enable_t anim_en)
{
    /*Be sure the bottom side is not remains scrolled in*/
    /*With snapping the content can't be scrolled in*/
    if(lv_obj_get_scroll_snap_y(obj) == LV_SCROLL_SNAP_NONE) {
        int32_t st = lv_obj_get_scroll_top(obj);
        int32_t sb = lv_obj_get_scroll_bottom(obj);
        if(sb < 0 && st > 0) {
            sb = LV_MIN(st, -sb);
            lv_obj_scroll_by(obj, 0, sb, anim_en);
        }
    }

    if(lv_obj_get_scroll_snap_x(obj) == LV_SCROLL_SNAP_NONE) {
        int32_t sl = lv_obj_get_scroll_left(obj);
        int32_t sr = lv_obj_get_scroll_right(obj);
        if(lv_obj_get_style_base_dir(obj, LV_PART_MAIN) != LV_BASE_DIR_RTL) {
            /*Be sure the left side is not remains scrolled in*/
            if(sr < 0 && sl > 0) {
                sr = LV_MIN(sl, -sr);
                lv_obj_scroll_by(obj, sr, 0, anim_en);
            }
        }
        else {
            /*Be sure the right side is not remains scrolled in*/
            if(sl < 0 && sr > 0) {
                sr = LV_MIN(sr, -sl);
                lv_obj_scroll_by(obj, sl, 0, anim_en);
            }
        }
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void scroll_x_anim(void * obj, int32_t v)
{
    lv_obj_scroll_by_raw(obj, v + lv_obj_get_scroll_x(obj), 0);
}

static void scroll_y_anim(void * obj, int32_t v)
{
    lv_obj_scroll_by_raw(obj, 0, v + lv_obj_get_scroll_y(obj));
}

static void scroll_end_cb(lv_anim_t * a)
{
    /*Do not sent END event if there wasn't a BEGIN*/
    if(a->start_cb_called) lv_obj_send_event(a->var, LV_EVENT_SCROLL_END, NULL);
}

/**
 * Completed/deleted callback for throw animations.
 * Sends SCROLL_END and clears scroll_obj on the owning indev
 * via the back-pointer stored in lv_anim_scroll_throw_config_t.
 */
static void scroll_throw_end_cb(lv_anim_t * a)
{
    if(a->start_cb_called) lv_obj_send_event(a->var, LV_EVENT_SCROLL_END, NULL);

    lv_anim_scroll_throw_config_t * config = a->user_data;
    if(config && config->indev) {
        lv_indev_t * indev = config->indev;
        if(indev->pointer.scroll_obj == a->var) {
            indev->pointer.scroll_obj = NULL;
        }
    }
}

static void scroll_area_into_view(const lv_area_t * area, lv_obj_t * child, lv_point_t * scroll_value,
                                  lv_anim_enable_t anim_en)
{
    lv_obj_t * parent = lv_obj_get_parent(child);
    if(!lv_obj_has_flag(parent, LV_OBJ_FLAG_SCROLLABLE)) return;

    lv_dir_t scroll_dir = lv_obj_get_scroll_dir(parent);
    int32_t snap_goal = 0;
    int32_t act = 0;
    const lv_area_t * area_tmp;

    int32_t y_scroll = 0;
    lv_scroll_snap_t snap_y = lv_obj_get_scroll_snap_y(parent);
    if(snap_y != LV_SCROLL_SNAP_NONE) area_tmp = &child->coords;
    else area_tmp = area;

    int32_t stop = lv_obj_get_style_space_top(parent, LV_PART_MAIN);
    int32_t sbottom = lv_obj_get_style_space_bottom(parent, LV_PART_MAIN);
    int32_t top_diff = parent->coords.y1 + stop - area_tmp->y1 - scroll_value->y;
    int32_t bottom_diff = -(parent->coords.y2 - sbottom - area_tmp->y2 - scroll_value->y);
    int32_t parent_h = lv_obj_get_height(parent) - stop - sbottom;
    if((top_diff >= 0 && bottom_diff >= 0)) y_scroll = 0;
    else if(top_diff > 0) {
        y_scroll = top_diff;
        /*Do not let scrolling in*/
        int32_t st = lv_obj_get_scroll_top(parent);
        if(st - y_scroll < 0) y_scroll = 0;
    }
    else if(bottom_diff > 0) {
        y_scroll = -bottom_diff;
        /*Do not let scrolling in*/
        int32_t sb = lv_obj_get_scroll_bottom(parent);
        if(sb + y_scroll < 0) y_scroll = 0;
    }

    switch(snap_y) {
        case LV_SCROLL_SNAP_START:
            snap_goal = parent->coords.y1 + stop;
            act = area_tmp->y1 + y_scroll;
            y_scroll += snap_goal - act;
            break;
        case LV_SCROLL_SNAP_END:
            snap_goal = parent->coords.y2 - sbottom;
            act = area_tmp->y2 + y_scroll;
            y_scroll += snap_goal - act;
            break;
        case LV_SCROLL_SNAP_CENTER:
            snap_goal = parent->coords.y1 + stop + parent_h / 2;
            act = lv_area_get_height(area_tmp) / 2 + area_tmp->y1 + y_scroll;
            y_scroll += snap_goal - act;
            break;
        case LV_SCROLL_SNAP_NONE:
            break;
    }

    int32_t x_scroll = 0;
    lv_scroll_snap_t snap_x = lv_obj_get_scroll_snap_x(parent);
    if(snap_x != LV_SCROLL_SNAP_NONE) area_tmp = &child->coords;
    else area_tmp = area;

    int32_t sleft = lv_obj_get_style_space_left(parent, LV_PART_MAIN);
    int32_t sright = lv_obj_get_style_space_right(parent, LV_PART_MAIN);
    int32_t left_diff = parent->coords.x1 + sleft - area_tmp->x1 - scroll_value->x;
    int32_t right_diff = -(parent->coords.x2 - sright - area_tmp->x2 - scroll_value->x);
    if((left_diff >= 0 && right_diff >= 0)) x_scroll = 0;
    else if(left_diff > 0) {
        x_scroll = left_diff;
        /*Do not let scrolling in*/
        int32_t sl = lv_obj_get_scroll_left(parent);
        if(sl - x_scroll < 0) x_scroll = 0;
    }
    else if(right_diff > 0) {
        x_scroll = -right_diff;
        /*Do not let scrolling in*/
        int32_t sr = lv_obj_get_scroll_right(parent);
        if(sr + x_scroll < 0) x_scroll = 0;
    }

    int32_t parent_w = lv_obj_get_width(parent) - sleft - sright;
    switch(snap_x) {
        case LV_SCROLL_SNAP_START:
            snap_goal = parent->coords.x1 + sleft;
            act = area_tmp->x1 + x_scroll;
            x_scroll += snap_goal - act;
            break;
        case LV_SCROLL_SNAP_END:
            snap_goal = parent->coords.x2 - sright;
            act = area_tmp->x2 + x_scroll;
            x_scroll += snap_goal - act;
            break;
        case LV_SCROLL_SNAP_CENTER:
            snap_goal = parent->coords.x1 + sleft + parent_w / 2;
            act = lv_area_get_width(area_tmp) / 2 + area_tmp->x1 + x_scroll;
            x_scroll += snap_goal - act;
            break;
        case LV_SCROLL_SNAP_NONE:
            break;
    }

    /*Remove any pending scroll animations.*/
    lv_anim_delete(parent, scroll_y_anim);
    lv_anim_delete(parent, scroll_x_anim);

    if((scroll_dir & LV_DIR_LEFT) == 0 && x_scroll < 0) x_scroll = 0;
    if((scroll_dir & LV_DIR_RIGHT) == 0 && x_scroll > 0) x_scroll = 0;
    if((scroll_dir & LV_DIR_TOP) == 0 && y_scroll < 0) y_scroll = 0;
    if((scroll_dir & LV_DIR_BOTTOM) == 0 && y_scroll > 0) y_scroll = 0;

    scroll_value->x += anim_en ? x_scroll : 0;
    scroll_value->y += anim_en ? y_scroll : 0;
    lv_obj_scroll_by(parent, x_scroll, y_scroll, anim_en);
}

static int32_t scroll_anim_end_value(lv_anim_t * a, int32_t v)
{
#if LV_USE_FLOAT
    return a ? (int32_t)(-a->end_value) : v;
#else
    return a ? -a->end_value : v;
#endif
}
