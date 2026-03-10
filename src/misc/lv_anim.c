/**
 * @file lv_anim.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_anim_private.h"

#include "../core/lv_global.h"
#include "../tick/lv_tick.h"
#include "lv_assert.h"
#include "lv_timer.h"
#include "lv_math.h"
#include "../stdlib/lv_mem.h"
#include "../stdlib/lv_string.h"
#include <math.h>

/*********************
 *      DEFINES
 *********************/

/**Perform linear animations in max 1024 steps. Used in `path_cb`s*/
#define LV_ANIM_RESOLUTION 1024

/**log2(LV_ANIM_RESOLUTION)*/
#define LV_ANIM_RES_SHIFT 10

/**In an anim. time this bit indicates that the value is speed, and not time*/
#define LV_ANIM_SPEED_MASK 0x80000000

#define state LV_GLOBAL_DEFAULT()->anim_state
#define anim_ll_p &(state.anim_ll)

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void anim_timer(lv_timer_t * param);
static void anim_vsync_event(lv_event_t * e);
static void anim_mark_list_change(void);
static void anim_completed_handler(lv_anim_t * a);
static lv_value_precise_t lv_anim_path_cubic_bezier(const lv_anim_t * a, int32_t x1,
                                                    int32_t y1, int32_t x2, int32_t y2);
static void lv_anim_pause_for_internal(lv_anim_t * a, uint32_t ms);
static void resolve_time(lv_anim_t * a);
static bool remove_concurrent_anims(const lv_anim_t * a_current);
static void remove_anim(void * a);
static bool anim_finished_time_cb(const lv_anim_t * a);


/**
 * Divide a scaled value by a shift unit (e.g., 1 << shift_bits).
 * Uses division instead of right shift to ensure truncation toward zero
 * for both positive and negative values.
 * @param v the scaled value
 * @param shift_unit the divisor (typically 1 << shift_bits)
 * @return the result of v / shift_unit (truncated toward zero)
 */
static lv_value_precise_t lv_anim_shift_divide(lv_value_precise_t v, int32_t shift_unit)
{
#if LV_USE_FLOAT
    /* For floating-point, just perform normal division */
    return v / (lv_value_precise_t)(1 << shift_unit);
#else
    /* For integer types, use division to ensure truncation toward zero */
    if(v >= 0) {
        return v >> shift_unit;
    }
    else {
        return -((-v) >> shift_unit);
    }
#endif
}
/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/
#if LV_USE_LOG && LV_LOG_TRACE_ANIM
    #define LV_TRACE_ANIM(...) LV_LOG_TRACE(__VA_ARGS__)
#else
    #define LV_TRACE_ANIM(...)
#endif

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_anim_core_init(void)
{
    lv_ll_init(anim_ll_p, sizeof(lv_anim_t));
    state.timer = lv_timer_create(anim_timer, LV_DEF_REFR_PERIOD, NULL);
    anim_mark_list_change(); /*Turn off the animation timer*/
    state.anim_list_changed = false;
    state.anim_run_round = false;
}

void lv_anim_core_deinit(void)
{
    lv_anim_delete_all();
}

void lv_anim_enable_vsync_mode(bool enable)
{
    if(enable) {
        /* Remove animation timer, use vsync instead */
        if(state.timer) {
            lv_timer_delete(state.timer);
            state.timer = NULL;
        }
    }
    else {
        if(!state.timer) {
            state.timer = lv_timer_create(anim_timer, LV_DEF_REFR_PERIOD, NULL);
            LV_ASSERT_NULL(state.timer);

            if(state.anim_vsync_registered) {
                lv_display_unregister_vsync_event(NULL, anim_vsync_event, NULL);
                state.anim_vsync_registered = false;
            }
        }
    }

    anim_mark_list_change();
}

void lv_anim_init(lv_anim_t * a)
{
    lv_memzero(a, sizeof(lv_anim_t));
    a->duration = 500;
    a->start_value = 0;
    a->end_value = 100;
    a->repeat_cnt = 1;
    a->path_cb = lv_anim_path_linear;
    a->early_apply = 1;
#if LV_USE_EXT_DATA
    a->ext_data.free_cb = NULL;
    a->ext_data.data = NULL;
#endif
    a->is_finished_cb = anim_finished_time_cb;
}

lv_anim_t * lv_anim_start(const lv_anim_t * a)
{
    LV_TRACE_ANIM("begin");

    /*Do not let two animations for the same 'var' with the same 'exec_cb'*/
    if(a->early_apply && (a->exec_cb || a->custom_exec_cb)) {
        remove_concurrent_anims(a);
    }

    /*Add the new animation to the animation linked list*/
    lv_anim_t * new_anim = lv_ll_ins_head(anim_ll_p);
    LV_ASSERT_MALLOC(new_anim);
    if(new_anim == NULL) return NULL;

    /*Initialize the animation descriptor*/
    lv_memcpy(new_anim, a, sizeof(lv_anim_t));
    if(a->var == a) new_anim->var = new_anim;
    new_anim->run_round = state.anim_run_round;
    new_anim->last_timer_run = lv_tick_get();
    new_anim->is_paused = false;

    /*Set the start value*/
    if(new_anim->early_apply) {
        if(new_anim->get_value_precise_cb) {
            lv_value_precise_t v_ofs = new_anim->get_value_precise_cb(new_anim);
            new_anim->start_value += v_ofs;
            new_anim->end_value += v_ofs;
        }
        else if(new_anim->get_value_cb) {
            int32_t v_ofs = new_anim->get_value_cb(new_anim);
            new_anim->start_value += (lv_value_precise_t)v_ofs;
            new_anim->end_value += (lv_value_precise_t)v_ofs;
        }

        resolve_time(new_anim);

        new_anim->current_value = new_anim->path_cb(new_anim);
        if(new_anim->exec_cb) {
            new_anim->exec_cb(new_anim->var, (int32_t)new_anim->current_value);
        }
        if(new_anim->exec_precise_cb) {
            new_anim->exec_precise_cb(new_anim->var, new_anim->current_value);
        }
        if(new_anim->custom_exec_cb) {
            new_anim->custom_exec_cb(new_anim, (int32_t)new_anim->current_value);
        }
        if(new_anim->custom_exec_precise_cb) {
            new_anim->custom_exec_precise_cb(new_anim, new_anim->current_value);
        }
    }

    /*Creating an animation changed the linked list.
     *It's important if it happens in a ready callback. (see `anim_timer`)*/
    anim_mark_list_change();

    LV_TRACE_ANIM("finished");
    return new_anim;
}

uint32_t lv_anim_get_playtime(const lv_anim_t * a)
{
    if(a->repeat_cnt == LV_ANIM_REPEAT_INFINITE) {
        return LV_ANIM_PLAYTIME_INFINITE;
    }

    uint32_t repeat_cnt = a->repeat_cnt;
    if(repeat_cnt < 1) repeat_cnt = 1;

    uint32_t playtime = a->repeat_delay + a->duration + a->reverse_delay + a->reverse_duration;
    playtime = playtime * repeat_cnt;
    return playtime;
}

bool lv_anim_delete(void * var, lv_anim_exec_xcb_t exec_cb)
{
    lv_anim_t * a;
    bool del_any = false;
    a        = lv_ll_get_head(anim_ll_p);
    while(a != NULL) {
        bool del = false;
        if((a->var == var || var == NULL) && (a->exec_cb == exec_cb || exec_cb == NULL)) {
            remove_anim(a);
            anim_mark_list_change(); /*Read by `anim_timer`. It need to know if a delete occurred in
                                       the linked list*/
            del_any = true;
            del = true;
        }

        /*Always start from the head on delete, because we don't know
         *how `anim_ll_p` was changes in `a->deleted_cb` */
        a = del ? lv_ll_get_head(anim_ll_p) : lv_ll_get_next(anim_ll_p, a);
    }

    return del_any;
}

void lv_anim_delete_all(void)
{
    lv_ll_clear_custom(anim_ll_p, remove_anim);
    anim_mark_list_change();
}

lv_anim_t * lv_anim_get(void * var, lv_anim_exec_xcb_t exec_cb)
{
    lv_anim_t * a;
    LV_LL_READ(anim_ll_p, a) {
        if(a->var == var && (a->exec_cb == exec_cb || exec_cb == NULL)) {
            return a;
        }
    }

    return NULL;
}

lv_timer_t * lv_anim_get_timer(void)
{
    return state.timer;
}

uint16_t lv_anim_count_running(void)
{
    uint16_t cnt = 0;
    lv_anim_t * a;
    LV_LL_READ(anim_ll_p, a) cnt++;

    return cnt;
}

uint32_t lv_anim_speed_clamped(uint32_t speed, uint32_t min_time, uint32_t max_time)
{

    if(speed > 10000) {
        LV_LOG_WARN("speed is truncated to 10000 (was %"LV_PRIu32")", speed);
        speed = 10230;
    }
    if(min_time > 10000) {
        LV_LOG_WARN("min_time is truncated to 10000 (was %"LV_PRIu32")", min_time);
        min_time = 10230;
    }
    if(max_time > 10000) {
        LV_LOG_WARN("max_time is truncated to 10000 (was %"LV_PRIu32")", max_time);
        max_time = 10230;
    }

    /*Lower the resolution to fit the 0.1023 range*/
    speed = (speed + 5) / 10;
    min_time = (min_time + 5) / 10;
    max_time = (max_time + 5) / 10;

    return LV_ANIM_SPEED_MASK + (max_time << 20) + (min_time << 10) + speed;

}

uint32_t lv_anim_speed(uint32_t speed)
{
    return lv_anim_speed_clamped(speed, 0, 10000);
}

uint32_t lv_anim_speed_to_time(uint32_t speed, int32_t start, int32_t end)
{
    uint32_t d = LV_ABS(start - end);
    uint32_t time = (d * 1000) / speed;

    time = time == 0 ? 1 : time;

    return time;
}

void lv_anim_refr_now(void)
{
    anim_timer(NULL);
}

lv_value_precise_t lv_anim_path_linear(const lv_anim_t * a)
{
    /*Calculate the current step*/
    int32_t step = lv_map(a->act_time, 0, a->duration, 0, LV_ANIM_RESOLUTION);

    /*Get the new value which will be proportional to `step`
     *and the `start` and `end` values*/
    lv_value_precise_t new_value;
    new_value = step * (a->end_value - a->start_value);

    /* Use division to ensure truncation toward zero for negative values too.
     * Right shift of negative signed values is implementation-defined and typically rounds toward -inf.
     */
    new_value = lv_anim_shift_divide(new_value, LV_ANIM_RES_SHIFT);
    new_value += a->start_value;

    return new_value;
}

lv_value_precise_t lv_anim_path_ease_in(const lv_anim_t * a)
{
    return lv_anim_path_cubic_bezier(a, LV_BEZIER_VAL_FLOAT(0.42), LV_BEZIER_VAL_FLOAT(0),
                                     LV_BEZIER_VAL_FLOAT(1), LV_BEZIER_VAL_FLOAT(1));
}

lv_value_precise_t lv_anim_path_ease_out(const lv_anim_t * a)
{
    return lv_anim_path_cubic_bezier(a, LV_BEZIER_VAL_FLOAT(0), LV_BEZIER_VAL_FLOAT(0),
                                     LV_BEZIER_VAL_FLOAT(0.58), LV_BEZIER_VAL_FLOAT(1));
}

lv_value_precise_t lv_anim_path_ease_in_out(const lv_anim_t * a)
{
    return lv_anim_path_cubic_bezier(a, LV_BEZIER_VAL_FLOAT(0.42), LV_BEZIER_VAL_FLOAT(0),
                                     LV_BEZIER_VAL_FLOAT(0.58), LV_BEZIER_VAL_FLOAT(1));
}

lv_value_precise_t lv_anim_path_overshoot(const lv_anim_t * a)
{
    return lv_anim_path_cubic_bezier(a, 341, 0, 683, 1300);
}

lv_value_precise_t lv_anim_path_bounce(const lv_anim_t * a)
{
    /*Calculate the current step*/
    int32_t t = lv_map(a->act_time, 0, a->duration, 0, LV_BEZIER_VAL_MAX);
    lv_value_precise_t diff = (a->end_value - a->start_value);

    /*3 bounces has 5 parts: 3 down and 2 up. One part is t / 5 long*/

    if(t < 408) {
        /*Go down*/
        t = (t * 2500) >> LV_BEZIER_VAL_SHIFT; /*[0..1024] range*/
        t = LV_BEZIER_VAL_MAX - t;
    }
    else if(t >= 408 && t < 614) {
        /*First bounce back*/
        t -= 408;
        t    = t * 5; /*to [0..1024] range*/
        diff = diff / 20;
    }
    else if(t >= 614 && t < 819) {
        /*Fall back*/
        t -= 614;
        t    = t * 5; /*to [0..1024] range*/
        t    = LV_BEZIER_VAL_MAX - t;
        diff = diff / 20;
    }
    else if(t >= 819 && t < 921) {
        /*Second bounce back*/
        t -= 819;
        t    = t * 10; /*to [0..1024] range*/
        diff = diff / 40;
    }
    else if(t >= 921 && t <= LV_BEZIER_VAL_MAX) {
        /*Fall back*/
        t -= 921;
        t    = t * 10; /*to [0..1024] range*/
        t    = LV_BEZIER_VAL_MAX - t;
        diff = diff / 40;
    }

    if(t > LV_BEZIER_VAL_MAX) t = LV_BEZIER_VAL_MAX;
    if(t < 0) t = 0;
    int32_t step = lv_bezier3(t, 0, 500, 800, LV_BEZIER_VAL_MAX);

    lv_value_precise_t new_value;
    new_value = step * diff;
    /* Use division to ensure truncation toward zero for negative values too.
     * Right shift of negative signed values is implementation-defined and typically rounds toward -inf.
     */
    new_value = lv_anim_shift_divide(new_value, LV_BEZIER_VAL_SHIFT);
    new_value = a->end_value - new_value;

    return new_value;
}

lv_value_precise_t lv_anim_path_step(const lv_anim_t * a)
{
    if(a->act_time >= a->duration)
        return a->end_value;
    else
        return a->start_value;
}

lv_value_precise_t lv_anim_path_custom_bezier3(const lv_anim_t * a)
{
    const lv_anim_bezier3_para_t * para = &a->parameter.bezier3;
    return lv_anim_path_cubic_bezier(a, para->x1, para->y1, para->x2, para->y2);
}

lv_value_precise_t lv_anim_path_expo_decay(const lv_anim_t * a)
{
    /*
     * True exponential decay matching original LVGL scroll throw: v *= 0.9 per frame.
     *
     * Original LVGL applies v *= scroll_throw/256 each timer tick (default scroll_throw=10%,
     * so decay_factor = (256-10)/256 ≈ 0.961 per tick at ~30fps).
     * The position integral: s(t) = S_total * (1 - decay^(t/dt))
     * where S_total = v0 * dt / (1 - decay).
     *
     * We use act_time (ms) directly. With frame_period = 33ms (30fps) and
     * decay_per_frame = 0.9:
     *   decay(t) = 0.9^(t / 33)
     *   position(t) = S * (1 - 0.9^(t/33))
     *
     * Since start_value and end_value encode the full range (end_value = start + S_total),
     * we compute: result = start + (end - start) * (1 - 0.9^(t/33))
     *
     * For is_finished_cb termination: animation ends when remaining < 1 pixel.
     */
    if(a->act_time <= 0) return a->start_value;

    lv_value_precise_t t = (lv_value_precise_t)a->act_time;
    lv_value_precise_t frame_period = 33.0f;  /* ~30fps, matching LV_DEF_REFR_PERIOD */
    lv_value_precise_t decay_per_frame = 0.9f;

    /* Number of frames elapsed (fractional) */
    lv_value_precise_t n_frames = t / frame_period;

    /* remaining_frac = 0.9^n_frames using expf for smooth interpolation */
    lv_value_precise_t remaining_frac;
    /* ln(0.9) ≈ -0.10536 */
    remaining_frac = expf(-0.10536f * n_frames);

    LV_UNUSED(decay_per_frame);

    /* progress = 1 - remaining */
    lv_value_precise_t progress = 1.0f - remaining_frac;
    if(progress > 1.0f) progress = 1.0f;

    return a->start_value + (a->end_value - a->start_value) * progress;
}

bool lv_anim_path_expo_decay_is_finished(const lv_anim_t * a)
{
    /* Convergence check: finished when remaining distance < 1 pixel */
    lv_value_precise_t range = a->end_value - a->start_value;
    if(range < 0) range = -range;
    if(range < 1.0f) return true;

    lv_value_precise_t t = (lv_value_precise_t)a->act_time;
    lv_value_precise_t n_frames = t / 33.0f;
    lv_value_precise_t remaining_frac = expf(-0.10536f * n_frames);
    lv_value_precise_t remaining_dist = range * remaining_frac;

    return remaining_dist < 1.0f;
}

lv_value_precise_t lv_anim_path_scroll_throw(const lv_anim_t * a)
{
    /*
     * Unified scroll throw path: handles normal / overshoot / bounce-back in one function.
     *
     * Framework provides A, B, C via anim + config:
     *   A = a->start_value       (current position in anim coords)
     *   B = a->end_value         (predicted target, unclamped — may be beyond boundary)
     *   C = boundary_near / boundary_far  (content edges in anim coords)
     *
     * Behavior:
     *   - A past C: bounce-back to C (elastic drag release)
     *   - B past C: expo_decay A→C, then elastic overshoot + decay back to C
     *   - Normal:   expo_decay A→B
     *
     * The expo_decay formula matches original LVGL: v *= 0.9 per frame (33ms).
     */
    lv_anim_scroll_throw_config_t * cfg = (lv_anim_scroll_throw_config_t *)a->user_data;
    if(a->act_time <= 0) return a->start_value;

    lv_value_precise_t A = a->start_value;
    lv_value_precise_t B = a->end_value;

    lv_value_precise_t boundary = B;
    bool overshoot = false;
    bool a_past_boundary = false;

    if(cfg) {
        lv_value_precise_t near = (lv_value_precise_t)cfg->boundary_near;
        lv_value_precise_t far = (lv_value_precise_t)cfg->boundary_far;

        if(A > near) {
            a_past_boundary = true;
            boundary = near;
        }
        else if(A < far) {
            a_past_boundary = true;
            boundary = far;
        }
        else if(B > near) {
            overshoot = true;
            boundary = near;
        }
        else if(B < far) {
            overshoot = true;
            boundary = far;
        }
    }

    lv_value_precise_t t = (lv_value_precise_t)a->act_time;
    lv_value_precise_t n_frames = t / 33.0f;
    lv_value_precise_t remaining_frac = expf(-0.10536f * n_frames);
    lv_value_precise_t progress = 1.0f - remaining_frac;
    if(progress > 1.0f) progress = 1.0f;

    if(a_past_boundary) {
        /* A already past boundary (elastic drag release): bounce back to C */
        return A + (boundary - A) * progress;
    }
    else if(overshoot) {
        /* B past boundary: two-phase animation
         * Phase 1: expo_decay A → C (free travel)
         * Phase 2: elastic overshoot past C, decaying back to C */
        lv_value_precise_t free_dist = boundary - A;
        lv_value_precise_t total_dist = B - A;
        if(total_dist == 0) return boundary;

        lv_value_precise_t overshoot_amp = (B - boundary) / 3.0f;

        lv_value_precise_t progress_at_boundary = free_dist / total_dist;
        if(progress_at_boundary < 0) progress_at_boundary = -progress_at_boundary;
        if(progress_at_boundary > 1.0f) progress_at_boundary = 1.0f;

        if(progress < progress_at_boundary) {
            /* Phase 1: free travel */
            return A + total_dist * progress;
        }
        else {
            /* Phase 2: elastic overshoot.
             * sin(π*t)*exp(-2t): exactly 0 at t=0 and t=1, guarantees return to C */
            lv_value_precise_t denom = 1.0f - progress_at_boundary;
            if(denom < 0.001f) return boundary;
            lv_value_precise_t ep = (progress - progress_at_boundary) / denom;
            if(ep < 0) ep = 0;
            if(ep > 1.0f) ep = 1.0f;

            lv_value_precise_t elastic_frac = sinf(3.14159f * ep) * expf(-2.0f * ep);
            if(elastic_frac < 0) elastic_frac = 0;
            if(elastic_frac > 1.0f) elastic_frac = 1.0f;

            return boundary + overshoot_amp * elastic_frac;
        }
    }
    else {
        /* Normal: A and B both within bounds */
        return A + (B - A) * progress;
    }
}

bool lv_anim_path_scroll_throw_is_finished(const lv_anim_t * a)
{
    lv_anim_scroll_throw_config_t * cfg = (lv_anim_scroll_throw_config_t *)a->user_data;

    /* Check convergence: current value close to final resting position */
    lv_value_precise_t current = a->current_value;
    lv_value_precise_t target;

    if(cfg) {
        lv_value_precise_t near = (lv_value_precise_t)cfg->boundary_near;
        lv_value_precise_t far = (lv_value_precise_t)cfg->boundary_far;
        lv_value_precise_t B = a->end_value;

        /* Final resting position: B clamped to boundaries */
        if(B > near) target = near;
        else if(B < far) target = far;
        else target = B;
    }
    else {
        target = a->end_value;
    }

    lv_value_precise_t remaining = current - target;
    if(remaining < 0) remaining = -remaining;

    return remaining < 1.0f;
}

lv_value_precise_t lv_anim_path_scroll_throw_spring(const lv_anim_t * a)
{
    /*
     * Apple-style friction→spring scroll throw path.
     *
     * Same A/B/C contract:
     *   A = a->start_value, B = a->end_value (unclamped), C = boundary from config.
     *
     * Modeled after iOS UIScrollView / Flutter BouncingScrollSimulation:
     *   - Normal (B within bounds): expo_decay A→B (friction phase only)
     *   - B past boundary: expo_decay A→C, then seamlessly hand off remaining
     *     velocity to an underdamped spring that bounces around C
     *   - A already past boundary: spring directly back to C
     *
     * The key insight from Apple's implementation: the spring phase starts at
     * the boundary with the velocity the friction phase had at that moment,
     * creating a physically continuous transition.
     */
    lv_anim_scroll_throw_config_t * cfg = (lv_anim_scroll_throw_config_t *)a->user_data;
    if(a->act_time <= 0) return a->start_value;

    lv_value_precise_t A = a->start_value;
    lv_value_precise_t B = a->end_value;

    /* expo_decay constants: v *= 0.9 per 33ms frame → decay_rate = ln(0.9)/33 ≈ -0.003193 per ms */
    const lv_value_precise_t decay_rate = -0.10536f / 33.0f;  /* per ms */

    /* Spring parameters (underdamped) */
    const lv_value_precise_t omega = 10.0f;   /* natural frequency */
    const lv_value_precise_t zeta  = 0.65f;   /* damping ratio (<1 = underdamped) */
    const lv_value_precise_t omega_d = omega * sqrtf(1.0f - zeta * zeta);

    /* Classify scenario */
    enum { CASE_NORMAL, CASE_OVERSHOOT, CASE_A_PAST } scenario = CASE_NORMAL;
    lv_value_precise_t boundary = B;

    if(cfg) {
        lv_value_precise_t near = (lv_value_precise_t)cfg->boundary_near;
        lv_value_precise_t far  = (lv_value_precise_t)cfg->boundary_far;

        if(A > near) {
            scenario = CASE_A_PAST;
            boundary = near;
        }
        else if(A < far) {
            scenario = CASE_A_PAST;
            boundary = far;
        }
        else if(B > near) {
            scenario = CASE_OVERSHOOT;
            boundary = near;
        }
        else if(B < far) {
            scenario = CASE_OVERSHOOT;
            boundary = far;
        }
    }

    lv_value_precise_t t_ms = (lv_value_precise_t)a->act_time;

    if(scenario == CASE_NORMAL) {
        /* Pure friction: expo_decay A→B */
        lv_value_precise_t progress = 1.0f - expf(decay_rate * t_ms);
        if(progress > 1.0f) progress = 1.0f;
        return A + (B - A) * progress;
    }

    if(scenario == CASE_A_PAST) {
        /* A already past boundary: spring directly back to C.
         * Initial velocity = 0 (drag release, finger just lifted). */
        lv_value_precise_t t_sec = t_ms / 1000.0f;
        lv_value_precise_t d0 = A - boundary;
        lv_value_precise_t decay = expf(-zeta * omega * t_sec);
        lv_value_precise_t osc = cosf(omega_d * t_sec)
                                 + (zeta * omega / omega_d) * sinf(omega_d * t_sec);
        return boundary + d0 * decay * osc;
    }

    /* CASE_OVERSHOOT: two-phase — friction A→C, then spring at C.
     *
     * Phase 1 (friction): position(t) = A + (B-A) * (1 - exp(decay_rate * t))
     *   reaches boundary C when: A + (B-A)*(1-exp(decay_rate*t)) = C
     *   → t_boundary = ln(1 - (C-A)/(B-A)) / decay_rate
     *
     * Phase 2 (spring): starts at C with the velocity friction had at t_boundary.
     *   Friction velocity = (B-A) * (-decay_rate) * exp(decay_rate * t_boundary)
     *   Spring: x(t) = C + exp(-ζωt) * [d0*cos(ωd*t) + ((ζω*d0+v0)/ωd)*sin(ωd*t)]
     *   where d0 = 0 (starts exactly at boundary), v0 = handoff velocity.
     */
    lv_value_precise_t total_dist = B - A;
    if(total_dist == 0) return boundary;

    lv_value_precise_t frac_to_boundary = (boundary - A) / total_dist;
    if(frac_to_boundary < 0) frac_to_boundary = -frac_to_boundary;
    if(frac_to_boundary > 1.0f) frac_to_boundary = 1.0f;

    /* Time (ms) when friction reaches boundary */
    lv_value_precise_t one_minus_frac = 1.0f - frac_to_boundary;
    if(one_minus_frac < 0.001f) one_minus_frac = 0.001f;
    lv_value_precise_t t_boundary_ms = logf(one_minus_frac) / decay_rate;
    if(t_boundary_ms < 0) t_boundary_ms = 0;

    if(t_ms <= t_boundary_ms) {
        /* Still in friction phase */
        lv_value_precise_t progress = 1.0f - expf(decay_rate * t_ms);
        if(progress > 1.0f) progress = 1.0f;
        return A + total_dist * progress;
    }

    /* Spring phase: time since reaching boundary */
    lv_value_precise_t t_spring_sec = (t_ms - t_boundary_ms) / 1000.0f;

    /* Handoff velocity from friction (in position units per second) */
    lv_value_precise_t v_at_boundary = total_dist * (-decay_rate) * expf(decay_rate * t_boundary_ms);
    /* Convert from per-ms to per-sec */
    v_at_boundary *= 1000.0f;

    /* Cap handoff velocity (like Flutter's maxSpringTransferVelocity) */
    const lv_value_precise_t max_transfer_v = 5000.0f;
    if(v_at_boundary > max_transfer_v) v_at_boundary = max_transfer_v;
    else if(v_at_boundary < -max_transfer_v) v_at_boundary = -max_transfer_v;

    /* Spring from boundary: d0=0, v0=v_at_boundary
     * x(t) = C + exp(-ζωt) * (v0/ωd) * sin(ωd*t) */
    lv_value_precise_t spring_decay = expf(-zeta * omega * t_spring_sec);
    lv_value_precise_t spring_pos = boundary
                                    + spring_decay * (v_at_boundary / omega_d) * sinf(omega_d * t_spring_sec);

    return spring_pos;
}

bool lv_anim_path_scroll_throw_spring_is_finished(const lv_anim_t * a)
{
    lv_anim_scroll_throw_config_t * cfg = (lv_anim_scroll_throw_config_t *)a->user_data;

    /* Final resting position is always the boundary (clamped B) */
    lv_value_precise_t B = a->end_value;
    lv_value_precise_t target = B;
    if(cfg) {
        lv_value_precise_t near = (lv_value_precise_t)cfg->boundary_near;
        lv_value_precise_t far  = (lv_value_precise_t)cfg->boundary_far;

        if(B > near) target = near;
        else if(B < far) target = far;

        /* A past boundary: target is the boundary A was past */
        lv_value_precise_t A = a->start_value;
        if(A > near) target = near;
        else if(A < far) target = far;
    }

    lv_value_precise_t err = a->current_value - target;
    if(err < 0) err = -err;
    return err < 1.0f;
}

/**
 * Parameterized friction→spring scroll throw path.
 * Same as lv_anim_path_scroll_throw_spring but reads spring parameters from
 * a->parameter.ease: p1 = omega (natural frequency), p2 = zeta (damping ratio).
 * This allows creating multiple spring variants (stiff, overdamped, bouncy, etc.)
 * without writing separate path functions.
 */
lv_value_precise_t lv_anim_path_scroll_throw_spring_param(const lv_anim_t * a)
{
    lv_anim_scroll_throw_config_t * cfg = (lv_anim_scroll_throw_config_t *)a->user_data;
    if(a->act_time <= 0) return a->start_value;

    lv_value_precise_t A = a->start_value;
    lv_value_precise_t B = a->end_value;

    /* Read spring parameters from ease para */
    lv_value_precise_t omega = a->parameter.ease.p1;  /* natural frequency */
    lv_value_precise_t zeta  = a->parameter.ease.p2;  /* damping ratio */
    if(omega <= 0) omega = 10.0f;
    if(zeta <= 0) zeta = 0.65f;

    const lv_value_precise_t decay_rate = -0.10536f / 33.0f;

    /* Classify scenario */
    enum { CASE_NORMAL, CASE_OVERSHOOT, CASE_A_PAST } scenario = CASE_NORMAL;
    lv_value_precise_t boundary = B;

    if(cfg) {
        lv_value_precise_t near = (lv_value_precise_t)cfg->boundary_near;
        lv_value_precise_t far  = (lv_value_precise_t)cfg->boundary_far;

        if(A > near) {
            scenario = CASE_A_PAST;
            boundary = near;
        }
        else if(A < far) {
            scenario = CASE_A_PAST;
            boundary = far;
        }
        else if(B > near) {
            scenario = CASE_OVERSHOOT;
            boundary = near;
        }
        else if(B < far) {
            scenario = CASE_OVERSHOOT;
            boundary = far;
        }
    }

    lv_value_precise_t t_ms = (lv_value_precise_t)a->act_time;

    if(scenario == CASE_NORMAL) {
        lv_value_precise_t progress = 1.0f - expf(decay_rate * t_ms);
        if(progress > 1.0f) progress = 1.0f;
        return A + (B - A) * progress;
    }

    /* Spring helper: handles both underdamped (zeta<1) and overdamped (zeta>=1) */
#define SPRING_POS(target, d0, v0, t_sec) do { \
        if(zeta < 1.0f) { \
            lv_value_precise_t omega_d = omega * sqrtf(1.0f - zeta * zeta); \
            lv_value_precise_t _decay = expf(-zeta * omega * (t_sec)); \
            if(d0 == 0) \
                result = (target) + _decay * ((v0) / omega_d) * sinf(omega_d * (t_sec)); \
            else \
                result = (target) + _decay * ((d0) * cosf(omega_d * (t_sec)) \
                                              + ((zeta * omega * (d0) + (v0)) / omega_d) * sinf(omega_d * (t_sec))); \
        } else { \
            /* Overdamped or critically damped */ \
            lv_value_precise_t s = omega * sqrtf(zeta * zeta - 1.0f + 0.0001f); \
            lv_value_precise_t r1 = -zeta * omega + s; \
            lv_value_precise_t r2 = -zeta * omega - s; \
            lv_value_precise_t c2 = ((v0) - r1 * (d0)) / (r2 - r1 + 0.0001f); \
            lv_value_precise_t c1 = (d0) - c2; \
            result = (target) + c1 * expf(r1 * (t_sec)) + c2 * expf(r2 * (t_sec)); \
        } \
    } while(0)

    lv_value_precise_t result;

    if(scenario == CASE_A_PAST) {
        lv_value_precise_t t_sec = t_ms / 1000.0f;
        SPRING_POS(boundary, A - boundary, 0, t_sec);
        return result;
    }

    /* CASE_OVERSHOOT: friction → spring handoff */
    lv_value_precise_t total_dist = B - A;
    if(total_dist == 0) return boundary;

    lv_value_precise_t frac = (boundary - A) / total_dist;
    if(frac < 0) frac = -frac;
    if(frac > 1.0f) frac = 1.0f;

    lv_value_precise_t one_minus_frac = 1.0f - frac;
    if(one_minus_frac < 0.001f) one_minus_frac = 0.001f;
    lv_value_precise_t t_boundary_ms = logf(one_minus_frac) / decay_rate;
    if(t_boundary_ms < 0) t_boundary_ms = 0;

    if(t_ms <= t_boundary_ms) {
        lv_value_precise_t progress = 1.0f - expf(decay_rate * t_ms);
        if(progress > 1.0f) progress = 1.0f;
        return A + total_dist * progress;
    }

    lv_value_precise_t t_spring_sec = (t_ms - t_boundary_ms) / 1000.0f;
    lv_value_precise_t v_at_boundary = total_dist * (-decay_rate) * expf(decay_rate * t_boundary_ms) * 1000.0f;
    const lv_value_precise_t max_v = 5000.0f;
    if(v_at_boundary > max_v) v_at_boundary = max_v;
    else if(v_at_boundary < -max_v) v_at_boundary = -max_v;

    SPRING_POS(boundary, 0, v_at_boundary, t_spring_sec);
    return result;

#undef SPRING_POS
}

bool lv_anim_path_scroll_throw_spring_param_is_finished(const lv_anim_t * a)
{
    lv_anim_scroll_throw_config_t * cfg = (lv_anim_scroll_throw_config_t *)a->user_data;

    lv_value_precise_t B = a->end_value;
    lv_value_precise_t target = B;
    if(cfg) {
        lv_value_precise_t near = (lv_value_precise_t)cfg->boundary_near;
        lv_value_precise_t far  = (lv_value_precise_t)cfg->boundary_far;
        if(B > near) target = near;
        else if(B < far) target = far;
        lv_value_precise_t A = a->start_value;
        if(A > near) target = near;
        else if(A < far) target = far;
    }

    lv_value_precise_t err = a->current_value - target;
    if(err < 0) err = -err;
    return err < 1.0f;
}

void lv_anim_set_var(lv_anim_t * a, void * var)
{
    a->var = var;
}

void lv_anim_set_exec_cb(lv_anim_t * a, lv_anim_exec_xcb_t exec_cb)
{
    a->exec_cb = exec_cb;
}

void lv_anim_set_duration(lv_anim_t * a, uint32_t duration)
{
    a->duration = duration;
}

void lv_anim_set_delay(lv_anim_t * a, uint32_t delay)
{
    a->act_time = -(int32_t)(delay);
}

void lv_anim_set_values(lv_anim_t * a, lv_value_precise_t start, lv_value_precise_t end)
{
    a->start_value = start;
    a->current_value = (lv_value_precise_t)INT32_MIN;
    a->end_value = end;
}

void lv_anim_set_custom_exec_cb(lv_anim_t * a, lv_anim_custom_exec_cb_t exec_cb)
{
    a->custom_exec_cb = exec_cb;
}

void lv_anim_set_path_cb(lv_anim_t * a, lv_anim_path_cb_t path_cb)
{
    a->path_cb = path_cb;
}

void lv_anim_set_start_cb(lv_anim_t * a, lv_anim_start_cb_t start_cb)
{
    a->start_cb = start_cb;
}

void lv_anim_set_get_value_cb(lv_anim_t * a, lv_anim_get_value_cb_t get_value_cb)
{
    a->get_value_cb = get_value_cb;
}

void lv_anim_set_completed_cb(lv_anim_t * a, lv_anim_completed_cb_t completed_cb)
{
    a->completed_cb = completed_cb;
}

void lv_anim_set_deleted_cb(lv_anim_t * a, lv_anim_deleted_cb_t deleted_cb)
{
    a->deleted_cb = deleted_cb;
}

void lv_anim_set_reverse_duration(lv_anim_t * a, uint32_t duration)
{
    a->reverse_duration = duration;
}

void lv_anim_set_reverse_time(lv_anim_t * a, uint32_t duration)
{
    lv_anim_set_reverse_duration(a, duration);
}

void lv_anim_set_reverse_delay(lv_anim_t * a, uint32_t delay)
{
    a->reverse_delay = delay;
}

void lv_anim_set_repeat_count(lv_anim_t * a, uint32_t cnt)
{
    a->repeat_cnt = cnt;
}

void lv_anim_set_repeat_delay(lv_anim_t * a, uint32_t delay)
{
    a->repeat_delay = delay;
}

void lv_anim_set_early_apply(lv_anim_t * a, bool en)
{
    a->early_apply = en;
}

void lv_anim_set_user_data(lv_anim_t * a, void * user_data)
{
    a->user_data = user_data;
}

void lv_anim_set_bezier3_param(lv_anim_t * a, int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
    lv_anim_bezier3_para_t * para = &a->parameter.bezier3;

    para->x1 = x1;
    para->x2 = x2;
    para->y1 = y1;
    para->y2 = y2;
}

uint32_t lv_anim_get_delay(const lv_anim_t * a)
{
    return -a->act_time;
}

uint32_t lv_anim_get_time(const lv_anim_t * a)
{
    return a->duration;
}

uint32_t lv_anim_get_repeat_count(const lv_anim_t * a)
{
    return a->repeat_cnt;
}

void * lv_anim_get_user_data(const lv_anim_t * a)
{
    return a->user_data;
}

bool lv_anim_custom_delete(lv_anim_t * a, lv_anim_custom_exec_cb_t exec_cb)
{
    return lv_anim_delete(a ? a->var : NULL, (lv_anim_exec_xcb_t)exec_cb);
}

lv_anim_t * lv_anim_custom_get(lv_anim_t * a, lv_anim_custom_exec_cb_t exec_cb)
{
    return lv_anim_get(a ? a->var : NULL, (lv_anim_exec_xcb_t)exec_cb);
}

uint32_t lv_anim_resolve_speed(uint32_t speed_or_time, int32_t start, int32_t end)
{
    /*It was a simple time*/
    if((speed_or_time & LV_ANIM_SPEED_MASK) == 0) return speed_or_time;

    uint32_t d    = LV_ABS(start - end);
    uint32_t speed = speed_or_time & 0x3FF;
    if(speed == 0) speed = 1;
    uint32_t time = (d * 100) / speed; /*Speed is in 10 units per sec*/
    uint32_t max_time = (speed_or_time >> 20) & 0x3FF;
    uint32_t min_time = (speed_or_time >> 10) & 0x3FF;

    return LV_CLAMP(min_time * 10, time, max_time * 10);
}

bool lv_anim_is_paused(lv_anim_t * a)
{
    LV_ASSERT_NULL(a);
    return a->is_paused;
}

void lv_anim_pause(lv_anim_t * a)
{
    LV_ASSERT_NULL(a);
    lv_anim_pause_for_internal(a, LV_ANIM_PAUSE_FOREVER);
}

void lv_anim_pause_for(lv_anim_t * a, uint32_t ms)
{
    LV_ASSERT_NULL(a);
    lv_anim_pause_for_internal(a, ms);
}

void lv_anim_resume(lv_anim_t * a)
{
    LV_ASSERT_NULL(a);
    a->is_paused = false;
    a->pause_duration = 0;
    a->run_round = state.anim_run_round;
}

#if LV_USE_EXT_DATA
void lv_anim_set_external_data(lv_anim_t * anim, void * data, void (* free_cb)(void * data))
{
    if(!a) {
        LV_LOG_WARN("Can't attach external user data and destructor callback to a NULL animation");
        return;
    }

    anim->ext_data.data = data;
    anim->ext_data.free_cb = free_cb;
}
#endif

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Periodically handle the animations.
 * @param param unused
 */
static void anim_timer(lv_timer_t * param)
{
    LV_UNUSED(param);

    /*Flip the run round*/
    state.anim_run_round = state.anim_run_round ? false : true;

    lv_anim_t * a = lv_ll_get_head(anim_ll_p);
    while(a != NULL) {
        uint32_t elaps = lv_tick_elaps(a->last_timer_run);

        if(a->is_paused) {
            const uint32_t time_paused = lv_tick_elaps(a->pause_time);
            const bool is_pause_over = a->pause_duration != LV_ANIM_PAUSE_FOREVER && time_paused >= a->pause_duration;

            if(is_pause_over) {
                const uint32_t pause_overrun = time_paused - a->pause_duration;
                a->is_paused = false;
                a->act_time += pause_overrun;
                a->run_round = !state.anim_run_round;
            }
        }
        else {
            a->act_time += elaps;
        }
        a->last_timer_run = lv_tick_get();

        /*It can be set by `lv_anim_delete()` typically in `end_cb`. If set then an animation delete
         * happened in `anim_completed_handler` which could make this linked list reading corrupt
         * because the list is changed meanwhile
         */
        state.anim_list_changed = false;

        if(!a->is_paused && a->run_round != state.anim_run_round) {
            a->run_round = state.anim_run_round; /*The list readying might be reset so need to know which anim has run already*/
            /*The animation will run now for the first time. Call `start_cb`*/
            if(!a->start_cb_called && a->act_time >= 0) {

                if(a->early_apply == 0) {
                    if(a->get_value_precise_cb) {
                        lv_value_precise_t v_ofs = a->get_value_precise_cb(a);
                        a->start_value += v_ofs;
                        a->end_value += v_ofs;
                    }
                    else if(a->get_value_cb) {
                        int32_t v_ofs = a->get_value_cb(a);
                        a->start_value += (lv_value_precise_t)v_ofs;
                        a->end_value += (lv_value_precise_t)v_ofs;
                    }
                }

                resolve_time(a);

                if(a->start_cb) a->start_cb(a);
                a->start_cb_called = 1;

                /*Do not let two animations for the same 'var' with the same 'exec_cb'*/
                remove_concurrent_anims(a);
            }

            if(a->act_time >= 0) {
                int32_t act_time_original = a->act_time; /*The unclipped version is used later to correctly repeat the animation*/
                /*For time-finished animations clamp act_time to duration.
                 *For convergence-finished paths (e.g. spring) act_time can run past duration;
                 *clamping would freeze dt (= act_time - last_act_time) and break integration.
                 */
                if(a->is_finished_cb == anim_finished_time_cb) {
                    if(a->act_time > a->duration) a->act_time = a->duration;
                }

                int32_t act_time_before_exec = a->act_time;

                lv_value_precise_t new_value;
                new_value = a->path_cb(a);

                if(new_value != a->current_value) {
                    a->current_value = new_value;
                    /*Apply the calculated value*/
                    if(a->exec_cb) a->exec_cb(a->var, (int32_t)new_value);
                    if(a->exec_precise_cb) a->exec_precise_cb(a->var, new_value);
                    if(!state.anim_list_changed && a->custom_exec_cb) a->custom_exec_cb(a, (int32_t)new_value);
                    if(!state.anim_list_changed && a->custom_exec_precise_cb) a->custom_exec_precise_cb(a, new_value);
                }

                if(!state.anim_list_changed) {
                    /*Restore the original time to see if there is over time, ignoring silly values.
                     *Restore only if it wasn't changed in the `exec_cb` for some special reasons.*/
                    if(a->act_time == act_time_before_exec && act_time_original < a->duration * 2 &&
                       a->is_finished_cb == anim_finished_time_cb) {
                        a->act_time = act_time_original;
                    }

                    /*If the time is elapsed the animation is ready*/
                    if(a->is_finished_cb(a)) {
                        anim_completed_handler(a);
                    }
                }
            }
        }

        /*If the linked list changed due to anim. delete then it's not safe to continue
         *the reading of the list from here -> start from the head*/
        if(state.anim_list_changed)
            a = lv_ll_get_head(anim_ll_p);
        else
            a = lv_ll_get_next(anim_ll_p, a);
    }

}

/**
 * Called when an animation is completed to do the necessary things
 * e.g. repeat, play in reverse, delete etc.
 * @param a pointer to an animation descriptor
 */
static void anim_completed_handler(lv_anim_t * a)
{
    /*In the end of a forward anim decrement repeat cnt.*/
    if(a->reverse_play_in_progress == 0 && a->repeat_cnt > 0 && a->repeat_cnt != LV_ANIM_REPEAT_INFINITE) {
        a->repeat_cnt--;
    }

    /*Delete animation if
     * - no repeat left and no reverse play scheduled (simple one shot animation); or
     * - no repeat, reverse play enabled (reverse_duration != 0) and reverse play is completed. */
    if(a->repeat_cnt == 0 && (a->reverse_duration == 0 || a->reverse_play_in_progress == 1)) {

        /*Delete the animation from the list.
         * This way the `completed_cb` will see the animations like it's animation is already deleted*/
        lv_ll_remove(anim_ll_p, a);
        /*Flag that the list has changed*/
        anim_mark_list_change();

        /*Call the callback function at the end*/
        if(a->completed_cb != NULL) a->completed_cb(a);
        if(a->deleted_cb != NULL) a->deleted_cb(a);
#if LV_USE_EXT_DATA
        if(a->ext_data.free_cb) {
            a->ext_data.free_cb(a->ext_data.data);
            a->ext_data.data = NULL;
        }
#endif
        lv_free(a);
    }
    /*If the animation is not deleted then restart it*/
    else {
        /*Restart the animation. If the time is over a little compensate it.*/
        int32_t over_time = 0;
        a->start_cb_called = 0;
        if(a->act_time > a->duration) over_time = a->act_time - a->duration;
        a->act_time = over_time - (int32_t)(a->repeat_delay);
        /*Swap start and end values in reverse-play mode*/
        if(a->reverse_duration != 0) {
            /*If now now playing in reverse, use the 'reverse_delay'.*/
            if(a->reverse_play_in_progress == 0) a->act_time = -(int32_t)(a->reverse_delay);

            /*Toggle reverse-play state*/
            a->reverse_play_in_progress = a->reverse_play_in_progress == 0 ? 1 : 0;
            /*Swap the start and end values*/
            lv_value_precise_t tmp = a->start_value;
            a->start_value = a->end_value;
            a->end_value   = tmp;
            /*Swap the time and reverse_duration*/
            uint32_t dt = a->duration;
            a->duration = a->reverse_duration;
            a->reverse_duration = dt;
        }
    }
}

static void anim_vsync_event(lv_event_t * e)
{
    LV_UNUSED(e);
    anim_timer(NULL);
}

static void anim_mark_list_change(void)
{
    state.anim_list_changed = true;
    if(lv_ll_get_head(anim_ll_p) == NULL) {
        if(state.timer) {
            lv_timer_pause(state.timer);
            return;
        }

        if(state.anim_vsync_registered) {
            lv_display_unregister_vsync_event(NULL, anim_vsync_event, NULL);
            state.anim_vsync_registered = false;
        }

        return;
    }

    if(state.timer) {
        lv_timer_resume(state.timer);
        return;
    }

    if(!state.anim_vsync_registered) {
        lv_display_register_vsync_event(NULL, anim_vsync_event, NULL);
        state.anim_vsync_registered = true;
    }
}

static lv_value_precise_t lv_anim_path_cubic_bezier(const lv_anim_t * a, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    /*Calculate the current step*/
    uint32_t t = lv_map(a->act_time, 0, a->duration, 0, LV_BEZIER_VAL_MAX);
    int32_t step = lv_cubic_bezier(t, x1, y1, x2, y2);

    lv_value_precise_t new_value;
    new_value = step * (a->end_value - a->start_value);

    /* Use division to ensure truncation toward zero for negative values too.
    * Right shift of negative signed values is implementation-defined and typically rounds toward -inf.
    */
    new_value = lv_anim_shift_divide(new_value, LV_BEZIER_VAL_SHIFT);
    new_value += a->start_value;

    return new_value;
}

static void lv_anim_pause_for_internal(lv_anim_t * a, uint32_t ms)
{

    a->is_paused = true;
    a->pause_time = lv_tick_get();
    a->pause_duration = ms;
}

static void resolve_time(lv_anim_t * a)
{
    a->duration = lv_anim_resolve_speed(a->duration, (int32_t)a->start_value, (int32_t)a->end_value);
    a->reverse_duration = lv_anim_resolve_speed(a->reverse_duration, (int32_t)a->start_value, (int32_t)a->end_value);
    a->reverse_delay = lv_anim_resolve_speed(a->reverse_delay, (int32_t)a->start_value, (int32_t)a->end_value);
    a->repeat_delay = lv_anim_resolve_speed(a->repeat_delay, (int32_t)a->start_value, (int32_t)a->end_value);
}

/**
 * Remove animations which are animating the same var with the same exec_cb
 * and they are already running or they have early_apply
 * @param a_current     the current animation, use its var and exec_cb as reference to know what to remove
 * @return              true: at least one animation was delete
 */
static bool remove_concurrent_anims(const lv_anim_t * a_current)
{
    if(a_current->exec_cb == NULL && a_current->custom_exec_cb == NULL) return false;

    lv_anim_t * a;
    bool del_any = false;
    a = lv_ll_get_head(anim_ll_p);
    while(a != NULL) {
        bool del = false;
        /*We can't test for custom_exec_cb equality because in the MicroPython binding
         *a wrapper callback is used here an the real callback data is stored in the `user_data`.
         *Therefore equality check would remove all animations.*/
        if(a != a_current &&
           (a->act_time >= 0 || a->early_apply) &&
           (a->var == a_current->var) &&
           ((a->exec_cb && a->exec_cb == a_current->exec_cb)
            /*|| (a->custom_exec_cb && a->custom_exec_cb == a_current->custom_exec_cb)*/)) {
            lv_ll_remove(anim_ll_p, a);
            if(a->deleted_cb != NULL) a->deleted_cb(a);
#if LV_USE_EXT_DATA
            if(a->ext_data.free_cb) {
                a->ext_data.free_cb(a->ext_data.data);
                a->ext_data.data = NULL;
            }
#endif
            lv_free(a);
            /*Read by `anim_timer`. It need to know if a delete occurred in the linked list*/
            anim_mark_list_change();

            del_any = true;
            del = true;
        }

        /*Always start from the head on delete, because we don't know
         *how `anim_ll_p` was changes in `a->deleted_cb` */
        a = del ? lv_ll_get_head(anim_ll_p) : lv_ll_get_next(anim_ll_p, a);
    }

    return del_any;
}

static void remove_anim(void * a)
{
    lv_anim_t * anim = a;
    lv_ll_remove(anim_ll_p, a);
    if(anim->deleted_cb != NULL) anim->deleted_cb(anim);
#if LV_USE_EXT_DATA
    if(anim->ext_data.free_cb) {
        anim->ext_data.free_cb(anim->ext_data.data);
        anim->ext_data.data = NULL;
    }
#endif
    lv_free(a);
}

static bool anim_finished_time_cb(const lv_anim_t * a)
{
    return (a->act_time >= a->duration);
}
