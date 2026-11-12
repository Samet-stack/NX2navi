// runtime/vram_budget.h: the device-local allocation gate, and the session that crossed the OS budget.
// Built by tests/vram_budget_check.py. No Vulkan headers and no GPU.
#include <stdio.h>
#include <stdint.h>

#include "vram_budget.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// Device-local usage of one session, MiB. The last-but-one sample is the 60-frame window that passed the budget.
static const struct { uint32_t frame, used, budget; } SERIES[] = {
    {136800, 7694, 11229}, {142200, 8119, 10432}, {147600, 8643, 10031}, {153600, 8979, 10916},
    {159600, 8986, 10870}, {161400, 9557, 10872}, {162000, 9807, 10965}, {165600, 10137, 10957},
    {166200, 9956, 10959}, {166440, 10982, 10957}, {166800, 10252, 10872},
};
enum { NSERIES = sizeof SERIES / sizeof SERIES[0] };

// Growth is what a new VkDeviceMemory would add. The gate clamps it at the line; a fall in the log still happens
// (a block that emptied). Returns the peak raw usage.
static uint64_t simulate(int gate) {
    uint64_t sim = (uint64_t)SERIES[0].used << 20, peak = sim;
    for (int i = 0; i < NSERIES - 1; i++) {
        uint32_t f0 = SERIES[i].frame, f1 = SERIES[i + 1].frame;
        for (uint32_t f = f0 + 1; f <= f1; f++) {
            double t = (double)(f - f0) / (double)(f1 - f0);
            uint64_t want = (uint64_t)(SERIES[i].used + ((int)SERIES[i + 1].used - (int)SERIES[i].used) * t) << 20;
            uint64_t budget = (uint64_t)(SERIES[i].budget + ((int)SERIES[i + 1].budget - (int)SERIES[i].budget) * t) << 20;
            if (!budget) budget = (uint64_t)SERIES[i].budget << 20;
            if (want <= sim) sim = want;   // usage fell: blocks went back
            else if (!gate) sim = want;
            else {
                uint64_t margin = vram_margin_auto(budget);
                while (sim + (64ull << 20) <= want) {
                    if (!vram_allow_block(sim, budget, 64ull << 20, margin)) break;
                    sim += 64ull << 20;
                }
                if (sim < want) {
                    uint64_t rest = want - sim;
                    if (vram_allow_block(sim, budget, rest, margin)) sim = want;
                }
            }
            if (sim > peak) peak = sim;
        }
    }
    return peak;
}

int main(void) {
    CHECK(vram_margin_auto(0) == 0, "no budget, no margin");
    uint64_t big = vram_margin_auto(11ull << 30);
    CHECK(big == (11ull << 30) / 32, "11 GiB margin is a thirty-second (%llu MiB)", (unsigned long long)(big >> 20));
    CHECK(vram_margin_auto(32ull << 30) == VRAM_MARGIN_MAX, "a 32 GiB budget clamps at the max");
    uint64_t mid = vram_margin_auto(8ull << 30);
    CHECK(mid == (8ull << 30) / 32, "8 GiB margin is a thirty-second (%llu)", (unsigned long long)mid);
    CHECK(vram_margin_auto(1ull << 30) <= (1ull << 30) / 4, "a 1 GiB budget never keeps more than a quarter");
    CHECK(vram_margin_auto(1ull << 30) > 0, "a 1 GiB budget still keeps a margin");

    uint64_t budget = 11000ull << 20, margin = vram_margin_auto(budget);
    uint64_t line = vram_budget_line(budget, margin);
    CHECK(line == budget - margin, "line is budget minus margin");
    CHECK(vram_allow_block(line - (32ull << 20), budget, 64ull << 20, margin) == 0, "a block that would pass the line is refused");
    CHECK(vram_allow_block(line - (96ull << 20), budget, 64ull << 20, margin) == 1, "a block that lands short of the line is allowed");
    CHECK(vram_allow_block(line, budget, 1, margin) == 0, "already on the line: nothing new");
    CHECK(vram_allow_block(line, budget, 64ull << 20, 0) == 1, "margin 0: the gate is off");
    CHECK(vram_allow_block(0, 0, 64ull << 20, margin) == 1, "unknown budget: allow");
    CHECK(vram_budget_line(budget, 0) == 0, "no line when the gate is off");

    // A burst charged against one sample of `used`, which is what one submission does.
    uint64_t used = 10000ull << 20, got = 0;
    for (int i = 0; i < 40; i++) {
        if (!vram_allow_block(used + got, budget, 64ull << 20, margin)) break;
        got += 64ull << 20;
    }
    CHECK(used + got <= line, "a burst stops at the line (%.0f of %.0f)", (used + got) / 1048576.0, line / 1048576.0);
    CHECK(got >= (64ull << 20), "the burst was allowed to start (%.0f MiB)", got / 1048576.0);
    CHECK(!vram_allow_block(used + got, budget, 64ull << 20, margin), "the next block is refused");

    // Free space inside blocks is not an argument of the gate: the same `used` is refused either way.
    CHECK(vram_allow_block(line, budget, 64ull << 20, margin) == 0, "on the line is refused however much of `used` is free inside blocks");

    uint64_t bare = simulate(0);
    CHECK(bare > (10957ull << 20), "without the gate the series crosses the budget (peak %.0f MiB)", bare / 1048576.0);
    uint64_t held = simulate(1);
    CHECK(held < (10957ull << 20) - (64ull << 20),
          "with the gate the series stays at least 64 MiB under the budget (peak %.0f MiB)", held / 1048576.0);
    CHECK(held <= bare, "the gate does not raise the peak");

    if (!fails) printf("vram_budget: %d checks passed; simulated peak %.0f MiB with the gate, %.0f MiB without\n",
                       checks, held / 1048576.0, bare / 1048576.0);
    return fails ? 1 : 0;
}
