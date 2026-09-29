#ifndef SPARROW_HMI_NOTIFICATION_LIST_H
#define SPARROW_HMI_NOTIFICATION_LIST_H

#include "lvgl.h"
#include "sp_severity.h"

/* A fixed number of rows, each with a severity dot, a message and a time line. */
lv_obj_t *sp_notification_list_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t width,
                                      int32_t height, int rows);

/* text == NULL hides the row. */
void sp_notification_list_set_row(lv_obj_t *list, int row, const char *text, const char *time_text,
                                  SpSeverity severity);

#endif
