// Whether a new device-local VkDeviceMemory is still allowed.
//
// Windows counts the driver's heapUsage against heapBudget (VK_EXT_memory_budget). One session stayed near 8 ms
// on the GPU until usage passed the budget by 25 MiB; after that the GPU stayed at 30-70 ms even once usage had
// fallen back under the budget. Two things that look like free memory do not lower heapUsage: space left inside a
// VMA block, and a VkImage that has been destroyed (in that session several thousand images went and the memory
// outside VMA did not). A new VkDeviceMemory does. The gate refuses one that would enter the last slice of the
// budget. A suballocation into a block VMA already holds never asks for a new VkDeviceMemory, so it is not gated.
//
// Own header so tests/vram_budget_check.c can build it with no Vulkan headers.
#pragma once

#include <stdint.h>

#define VRAM_MARGIN_MIN (256ull << 20)
#define VRAM_MARGIN_MAX (512ull << 20)

// The auto margin: a thirty-second of the budget, clamped, and never more than a quarter of a small budget.
static inline uint64_t vram_margin_auto(uint64_t budget) {
    if (!budget) return 0;
    uint64_t m = budget / 32;
    if (m < VRAM_MARGIN_MIN) m = VRAM_MARGIN_MIN;
    if (m > VRAM_MARGIN_MAX) m = VRAM_MARGIN_MAX;
    uint64_t quarter = budget / 4;
    if (m > quarter) m = quarter;
    return m;
}

// margin 0: the gate is off (unknown budget, or the player set NX2NAVI_VRAM_MARGIN_MB=0).
// 1 when a new device-local allocation of `bytes` may go ahead. `used` is raw heapUsage, plus the bytes already
// granted since that figure was sampled.
static inline int vram_allow_block(uint64_t used, uint64_t budget, uint64_t bytes, uint64_t margin) {
    if (!margin || !budget || !bytes) return 1;
    if (margin > budget) margin = budget / 4 ? budget / 4 : budget;
    uint64_t line = budget - margin;
    if (used >= line) return 0;
    return bytes <= line - used;
}

// The line itself, for the log. 0 when the gate is off.
static inline uint64_t vram_budget_line(uint64_t budget, uint64_t margin) {
    if (!margin || !budget) return 0;
    if (margin > budget) margin = budget / 4 ? budget / 4 : budget;
    return budget - margin;
}

// Reclaim starts before the gate. level 2 is inside the gate's margin (a new block is about to be refused);
// level 1 is a margin earlier, or when a large amount of free space is trapped inside blocks and raw usage is
// within 512 MiB of that earlier line. drop: aged released images can be destroyed. pack: move images out of a
// sparse block so the block can be freed. step: released images one check may destroy.
typedef struct {
    int level, drop, pack, step;
    uint64_t early, line;
} VramReclaim;

static inline VramReclaim vram_reclaim(uint64_t used, uint64_t budget, uint64_t margin, uint64_t free_inside) {
    VramReclaim r;
    r.level = r.drop = r.pack = r.step = 0;
    r.early = r.line = 0;
    if (!budget || !margin) return r;
    if (margin > budget) margin = budget / 4 ? budget / 4 : budget;
    r.line = budget - margin;
    r.early = budget > margin * 2 ? budget - margin * 2 : 0;
    if (used >= r.line) r.level = 2;
    else if (used >= r.early) r.level = 1;
    else if (free_inside >= (512ull << 20) && r.early > (512ull << 20) && used >= r.early - (512ull << 20))
        r.level = 1;
    if (r.level) {
        r.drop = 1;
        r.pack = free_inside >= (128ull << 20);
        r.step = r.level >= 2 ? 64 : 32;
    }
    return r;
}
