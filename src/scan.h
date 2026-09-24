#ifndef SCAN_H
#define SCAN_H

#include <stdint.h>

uint64_t scan_mask64(const char *p);
uint64_t scan_mask64_ref(const char *p);
char *scan_next(const char *p, const char *end, int *has_amp);

#endif
