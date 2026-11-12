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

// Reclaim starts a margin before the gate (early = line - margin), before a new block is refused.
// level 2 is on the gate line. urgent: usage is on that line, a block would be refused, or one was refused
// since the last sample — released images then go with no keep-age. drop: released images can be destroyed.
// pack: move images out of a sparse block. step: how many the mild path looks at.
typedef struct {
    int level, drop, pack, step, urgent;
    uint64_t early, line;
} VramReclaim;

static inline VramReclaim vram_reclaim(uint64_t used, uint64_t budget, uint64_t margin, uint64_t free_inside,
                                       int refused_now) {
    VramReclaim r;
    r.level = r.drop = r.pack = r.step = r.urgent = 0;
    r.early = r.line = 0;
    if (!budget || !margin) return r;
    if (margin > budget) margin = budget / 4 ? budget / 4 : budget;
    r.line = budget - margin;
    r.early = budget > margin * 2 ? budget - margin * 2 : 0;
    if (used >= r.line) r.level = 2;
    else if (used >= r.early) r.level = 1;
    else if (free_inside >= (512ull << 20) && r.early > (512ull << 20) && used >= r.early - (512ull << 20))
        r.level = 1;
    if (used >= r.line || refused_now || !vram_allow_block(used, budget, 64ull << 20, margin)) r.urgent = 1;
    if (r.urgent && !r.level) r.level = 2;
    if (r.level) {
        r.drop = 1;
        r.pack = free_inside >= (128ull << 20) || r.urgent;
        r.step = r.level >= 2 ? 64 : 32;
    }
    return r;
}

// Bytes of released images to destroy so raw usage falls under line - margin. 0 when it is already under.
static inline uint64_t vram_drop_goal(uint64_t used, uint64_t budget, uint64_t margin) {
    if (!budget || !margin) return 0;
    if (margin > budget) margin = budget / 4 ? budget / 4 : budget;
    uint64_t early = budget > margin * 2 ? budget - margin * 2 : 0;
    if (used <= early) return 0;
    return used - early;
}

// Own device memory released textures may hold: pct percent of the budget. 0 or a negative pct: no cap.
static inline uint64_t vram_released_cap(uint64_t budget, int pct) {
    if (!budget || pct <= 0) return ~(uint64_t)0;
    if (pct > 50) pct = 50;
    return budget / 100 * (uint64_t)pct;
}

// 1 when a released image may be destroyed. ignore_keep: the long keep does not apply (on the line, a refusal,
// or over the released-memory cap). A release younger than `stale` stays either way: the game takes those back
// within a frame.
static inline int vram_released_drop(uint64_t since_rel, uint64_t since_samp, uint64_t stale, uint64_t keep,
                                    int ignore_keep) {
    if (since_rel < stale) return 0;
    if (ignore_keep) return 1;
    if (!keep) return 0;
    return since_samp >= keep;
}

// A moved image must stay in device-local memory, and must not land in the block being emptied. That block is the
// one with the most free space, so a best-fit allocator puts the next image back into it and it never drains.
static inline int vram_pack_dest_ok(int device_local, uint64_t dest_block, uint64_t avoid_block) {
    if (!device_local) return 0;
    if (avoid_block && dest_block == avoid_block) return 0;
    return 1;
}

// An image that cannot leave its block: multisample, too many mips to copy, registered with DLSS or Streamline,
// or written in the last `recent` submissions (a render target). last_write 0: never written. last_write ahead of
// issued: the submission being recorded still writes it.
static inline int vram_pack_movable(int samples, int levels, int held, uint64_t last_write, uint64_t issued,
                                   uint64_t recent) {
    if (samples > 1 || levels > 16 || held) return 0;
    if (last_write > issued) return 0;
    if (last_write && recent && issued - last_write < recent) return 0;
    return 1;
}

// A block that holds one of those is not a pack target: it can never be emptied, and holding it out of new
// allocations pushes later images into system memory.
static inline int vram_pack_block_ok(int any_unmovable, int movable) {
    return !any_unmovable && movable > 0;
}

// Two full passes that move nothing: give the block back to the allocator. One pass can be a run of images the
// upload batch is still writing.
static inline int vram_pack_give_up(int zero_move_passes) {
    return zero_move_passes >= 2;
}

// Smallest-fit tries the emptiest block last. A normal image that still lands there is kept: that hole was the
// only one that fit, and the exclusion is released so later images are not pushed into system memory. Host memory
// is only when the device allocation itself failed. A move is kept only when it is device-local and in another block.
static inline int vram_place_keep_normal(int alloc_ok) { return alloc_ok; }
static inline int vram_place_host(int alloc_ok) { return !alloc_ok; }
static inline int vram_place_release(int alloc_ok, int in_excluded) { return alloc_ok && in_excluded; }
static inline int vram_place_keep_move(int device_local, int in_excluded) {
    return device_local && !in_excluded;
}

// How many released images to destroy so the count is at most `cap`. 0 when there is no cap or the count is under it.
// The images that cost driver memory outside VMA are the ones with no memory of their own; a byte cap does not see them.
static inline uint32_t vram_released_count_over(uint32_t count, uint32_t cap) {
    if (!cap || count <= cap) return 0;
    return count - cap;
}

// The count cap drops images whose content exists only in the image. Far from the budget line that brought back
// black frames, so it runs only once reclaim has started (usage at line - margin or closer).
static inline int vram_count_drop_now(int near_line, uint32_t count, uint32_t cap) {
    return near_line && vram_released_count_over(count, cap) > 0;
}

// Bytes of images to copy off a sparse block this frame. Urgent: a whole block, so it can be empty within a
// frame. Otherwise half a block, so a block empties within two frames, and at least 16 MiB.
static inline uint64_t vram_pack_budget(uint64_t block, int urgent) {
    if (!block) block = 64ull << 20;
    if (urgent) return block;
    uint64_t half = block / 2;
    return half < (16ull << 20) ? (16ull << 20) : half;
}

// A pack copy must not run before an upload that is still only recorded. `uploaded` is the batch that last
// recorded a write to the image (0 if none). `copy_batch` is the batch the copy is recorded into.
// `copy_after_upload` is 1 when that copy is later in the same batch than the upload. `submitted` is the highest
// batch already handed to the queue. A copy in an earlier batch, or in a later batch while the upload's batch is
// still open, reads the image before the upload and the upload then lands on the image about to be destroyed.
static inline int vram_pack_ordered(uint64_t uploaded, uint64_t copy_batch, int copy_after_upload, uint64_t submitted) {
    if (!uploaded) return 1;
    if (copy_batch < uploaded) return 0;
    if (copy_batch == uploaded) return copy_after_upload;
    return uploaded <= submitted;
}
