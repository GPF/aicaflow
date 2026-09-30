#include <stdint.h>
#include <stddef.h>
void spu_disable(void);
void spu_enable(void);
void spu_memload(uintptr_t address, const void *source, size_t size);
void spu_memset(uintptr_t address, uint32_t value, size_t size);
typedef void (*spu_dma_callback_t)(void *data);
int spu_dma_transfer(const void *source, uintptr_t address, size_t size, int block,
                     spu_dma_callback_t callback, void *data);
