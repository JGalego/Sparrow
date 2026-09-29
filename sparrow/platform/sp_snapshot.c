#include "sp_snapshot.h"

#include <stdio.h>

static void write_rows(FILE *file, const lv_draw_buf_t *buffer)
{
    const uint32_t width = buffer->header.w;
    const uint32_t height = buffer->header.h;

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *row = (const uint8_t *)buffer->data + (size_t)y * buffer->header.stride;
        for (uint32_t x = 0; x < width; x++) {
            const uint8_t rgb[3] = {row[x * 4 + 2], row[x * 4 + 1], row[x * 4]};
            fwrite(rgb, 1, sizeof rgb, file);
        }
    }
}

SpStatus sp_snapshot_write_ppm(lv_obj_t *obj, const char *path)
{
    lv_draw_buf_t *buffer = lv_snapshot_take(obj, LV_COLOR_FORMAT_ARGB8888);
    FILE *file;

    if (buffer == NULL) {
        return SP_ERR_IO;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        lv_draw_buf_destroy(buffer);
        return SP_ERR_IO;
    }
    fprintf(file, "P6\n%u %u\n255\n", (unsigned)buffer->header.w, (unsigned)buffer->header.h);
    write_rows(file, buffer);
    lv_draw_buf_destroy(buffer);
    return fclose(file) == 0 ? SP_OK : SP_ERR_IO;
}
