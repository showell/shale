// deeper.h: hunt_names' edge plants, a header below a folder.
#ifndef SHALE_DEEPER_H
#define SHALE_DEEPER_H

typedef struct {
    int c;
} DataEntry;

static inline int deeper_arr_get(int *data, int idx) {
    return data[idx];
}

#endif // SHALE_DEEPER_H
