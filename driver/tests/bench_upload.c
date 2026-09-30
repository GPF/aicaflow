/* Run with `make -C driver upload-bench` in the KOS environment, then
 * kos-tool -f -t "$DCTOOL_HOST" -x driver/build/bench_upload.elf.
 * Only SH-4 RAM -> AICA RAM is timed: no file I/O, linking or frame waits.
 * DMA includes cache writeback and completion; readback is outside timing.
 * Firmware runs idle throughout. This does not measure concurrent playback. */
#include <kos.h>
#include <aicaflow/host.h>
#include <dc/g2bus.h>
#include <dc/spu.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

static const uint8_t firmware[] = {
#embed "../../firmware/aicaflow.drv"
};

int main(void) {
    const char *methods[] = {"pio", "dma", "sq", "sq64k"};
    unsigned failures = 0;
    const unsigned sizes[] = {4096, 65536, 542896, 1048576, 1928802};
    const unsigned capacity = (sizes[4] + 31u) & ~31u;
    uint8_t *source = memalign(32, capacity);
    uint8_t *readback = memalign(32, capacity);
    int result = afx_init(firmware, sizeof(firmware));
    uint32_t address = result ? 0 : afx_mem_alloc(capacity, 32);
    if (result || !address || !source || !readback) {
        printf("UPLOAD_BENCH FAIL init=%d address=%lu\n", result, (unsigned long)address);
        afx_shutdown(); free(source); free(readback); return 1;
    }
    printf("UPLOAD_BENCH BEGIN address=%lu repeats=7 warmups=1 firmware=idle\n",
           (unsigned long)address);
    for (unsigned s = 0; s < sizeof(sizes) / sizeof(*sizes); ++s) {
        unsigned bytes = sizes[s], padded = (bytes + 31u) & ~31u;
        for (unsigned repeat = 0; repeat < 8; ++repeat) {
            /* Rotate order to avoid favouring one method's cache state. */
            for (unsigned order = 0; order < 4; ++order) {
                unsigned method = (order + repeat) % 4;
                for (unsigned i = 0; i < padded; ++i)
                    source[i] = (uint8_t)((i * 37u) ^ (i >> 8) ^ (repeat * 53u) ^ (method * 117u));
                uint64_t begin = timer_us_gettime64();
                uint64_t prepared = begin;
                if (method == 1) {
                    dcache_wback_range((uintptr_t)source, padded);
                    prepared = timer_us_gettime64();
                    result = spu_dma_transfer(source, address, padded, 1, NULL, NULL);
                } else if (method == 2) {
                    spu_memload_sq(address, source, padded);
                } else if (method == 3) {
                    for (unsigned offset = 0; offset < padded; offset += 65536) {
                        unsigned count = padded - offset;
                        if (count > 65536) count = 65536;
                        spu_memload_sq(address + offset, source + offset, count);
                    }
                } else {
                    spu_memload(address, source, padded);
                }
                g2_fifo_wait();
                uint64_t done = timer_us_gettime64();
                spu_memread(readback, address, padded);
                int verified = !result && !memcmp(source, readback, padded);
                printf("UPLOAD_BENCH method=%s bytes=%u padded=%u repeat=%u total_us=%llu cache_us=%llu transfer_us=%llu verified=%d\n",
                       methods[method], bytes, padded, repeat,
                       (unsigned long long)(done - begin),
                       (unsigned long long)(prepared - begin),
                       (unsigned long long)(done - prepared), verified);
                if (!verified) {
                    unsigned first = padded, last = 0, mismatches = 0;
                    for (unsigned i = 0; i < padded; ++i) {
                        if (source[i] == readback[i]) continue;
                        if (!mismatches) first = i;
                        last = i; ++mismatches;
                    }
                    printf("UPLOAD_BENCH MISMATCH method=%s repeat=%u bytes=%u count=%u first=%u last=%u transfer=%d\n",
                           methods[method], repeat, bytes, mismatches, first, last, result);
                    ++failures;
                }
            }
        }
    }
    afx_mem_free(address);
    afx_shutdown(); free(source); free(readback);
    printf("UPLOAD_BENCH DONE failures=%u\n", failures);
    return failures ? 1 : 0;
}
