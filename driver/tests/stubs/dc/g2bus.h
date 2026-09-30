#include <stdint.h>
uint32_t g2_read_32(uintptr_t address);
void g2_write_32(uintptr_t address, uint32_t value);
#define g2_read_32_raw(address) g2_read_32(address)
#define g2_write_32_raw(address, value) g2_write_32((address), (value))
#define g2_lock_scoped() ((void)0)
static inline void g2_fifo_wait(void) {}
