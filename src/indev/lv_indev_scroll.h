/**
 * @file lv_indev_scroll.h
 *
 */

#ifndef LV_INDEV_SCROLL_H
#define LV_INDEV_SCROLL_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "../core/lv_obj.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * Scroll curve configuration using LVGL animation path callbacks
 */
typedef struct {
    /* Throw scroll curve (momentum scrolling) */
    lv_anim_path_cb_t throw_path_cb;      /**< Animation path callback for throw */
    lv_anim_bezier3_para_t throw_bezier;  /**< Bezier parameters for throw (if using bezier) */

    /* Snap/Bounce curve (snap points and boundary bounce) */
    lv_anim_path_cb_t snap_path_cb;       /**< Animation path callback for snap */
    lv_anim_bezier3_para_t snap_bezier;   /**< Bezier parameters for snap (if using bezier) */

    uint32_t throw_max_duration;          /**< Maximum throw animation duration (ms) */
    int32_t throw_min_velocity_threshold; /**< Throw stop threshold */

    uint32_t snap_duration;               /**< Snap animation duration (ms) */
} lv_scroll_curve_config_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Handle scrolling. Called by LVGL during input device processing
 * @param indev      pointer to an input device
 */
void lv_indev_scroll_handler(lv_indev_t * indev);

/**
 * Handle throwing after scrolling. Called by LVGL during input device processing
 * @param indev      pointer to an input device
 */
void lv_indev_scroll_throw_handler(lv_indev_t * indev);

/**
 * Predict where would a scroll throw end
 * @param indev     pointer to an input device
 * @param dir `     LV_DIR_VER` or `LV_DIR_HOR`
 * @return          the difference compared to the current position when the throw would be finished
 */
int32_t lv_indev_scroll_throw_predict(lv_indev_t * indev, lv_dir_t dir);

/**
 * Get the distance of the nearest snap point
 * @param obj       the object on which snap points should be found
 * @param p         save the distance of the found snap point there
 */
void lv_indev_scroll_get_snap_dist(lv_obj_t * obj, lv_point_t * p);

/**
 * Set custom scroll curve for an input device
 * @param indev     pointer to an input device
 * @param config    pointer to scroll curve configuration (NULL to use default)
 */
void lv_indev_set_scroll_curve(lv_indev_t * indev, const lv_scroll_curve_config_t * config);

/**
 * Get scroll curve configuration for an input device
 * @param indev     pointer to an input device
 * @return          pointer to scroll curve configuration or NULL if using default
 */
lv_scroll_curve_config_t * lv_indev_get_scroll_curve(lv_indev_t * indev);

/**
 * Create a default scroll curve configuration using LVGL animation paths
 * @param throw_path_cb     animation path callback for throw (e.g., lv_anim_path_linear)
 * @param snap_path_cb      animation path callback for snap (e.g., lv_anim_path_ease_out)
 * @return                  default configuration with specified path callbacks
 */
lv_scroll_curve_config_t lv_scroll_curve_create_default(lv_anim_path_cb_t throw_path_cb,
                                                        lv_anim_path_cb_t snap_path_cb);

/**
 * Create a scroll curve configuration with bezier curves
 * @param throw_x1, throw_y1, throw_x2, throw_y2    bezier control points for throw
 * @param snap_x1, snap_y1, snap_x2, snap_y2        bezier control points for snap
 * @return                                           configuration with bezier curves
 */
lv_scroll_curve_config_t lv_scroll_curve_create_bezier(int16_t throw_x1, int16_t throw_y1, int16_t throw_x2,
                                                       int16_t throw_y2,
                                                       int16_t snap_x1, int16_t snap_y1, int16_t snap_x2, int16_t snap_y2);

/**
 * Create a scroll curve configuration with custom parameters
 * @param throw_path_cb      animation path callback for throw
 * @param snap_path_cb       animation path callback for snap
 * @param throw_duration     throw animation duration (ms)
 * @param snap_duration      snap animation duration (ms)
 * @return                   configuration with specified parameters
 */
lv_scroll_curve_config_t lv_scroll_curve_create_custom(lv_anim_path_cb_t throw_path_cb,
                                                       lv_anim_path_cb_t snap_path_cb,
                                                       uint32_t throw_duration,
                                                       uint32_t snap_duration);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_INDEV_SCROLL_H*/
