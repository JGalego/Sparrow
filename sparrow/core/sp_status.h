#ifndef SPARROW_CORE_STATUS_H
#define SPARROW_CORE_STATUS_H

typedef enum {
    SP_OK = 0,
    SP_ERR_ARGUMENT,
    SP_ERR_RANGE,
    SP_ERR_SIZE,
    SP_ERR_MAGIC,
    SP_ERR_VERSION,
    SP_ERR_IO
} SpStatus;

#endif
