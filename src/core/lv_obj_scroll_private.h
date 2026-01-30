/**
 * @file lv_obj_scroll_private.h
 *
 */

#ifndef LV_OBJ_SCROLL_PRIVATE_H
#define LV_OBJ_SCROLL_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lv_obj_scroll.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Low level function to scroll by given x and y coordinates.
 * `LV_EVENT_SCROLL` is sent.
 * @param obj       pointer to an object to scroll
 * @param x         pixels to scroll horizontally
 * @param y         pixels to scroll vertically
 * @return          `LV_RESULT_INVALID`: to object was deleted in `LV_EVENT_SCROLL`;
 *                  `LV_RESULT_OK`: if the object is still valid
 */
lv_result_t lv_obj_scroll_by_raw(lv_obj_t * obj, int32_t x, int32_t y);

/**
 * Scroll by with custom animation curve (used by indev scroll)
 * @param obj pointer to an object
 * @param dx pixels to scroll horizontally
 * @param dy pixels to scroll vertically
 * @param path_cb animation path callback
 * @param bezier_param bezier parameters (can be NULL)
 * @param duration animation duration in ms
 */
void lv_obj_scroll_by_with_curve(lv_obj_t * obj, int32_t dx, int32_t dy,
                                 lv_anim_path_cb_t path_cb,
                                 lv_anim_bezier3_para_t * bezier_param,
                                 uint32_t duration);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_OBJ_SCROLL_PRIVATE_H*/
