#include <stdint.h>
static inline void dcache_wback_range(uintptr_t address, uint32_t size) {
    (void)address; (void)size;
}
