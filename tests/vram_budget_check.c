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

    // Reclaim starts a margin before a new block is refused.
    {
        uint64_t b = 10957ull << 20, m = vram_margin_auto(b);
        uint64_t used = 10137ull << 20, free_in = 1411ull << 20;   // frame 165600 of the session
        VramReclaim rc = vram_reclaim(used, b, m, free_in, 0);
        CHECK(rc.level == 1 && rc.drop && rc.pack && !rc.urgent, "frame 165600 reclaims before the gate (level %d)", rc.level);
        CHECK(vram_allow_block(used, b, 64ull << 20, m) == 1, "a block at frame 165600 is still allowed");
        VramReclaim quiet = vram_reclaim(6000ull << 20, b, m, 128ull << 20, 0);
        CHECK(quiet.level == 0 && !quiet.drop && !quiet.urgent, "6 GiB with little trapped space stays quiet");
        VramReclaim wall = vram_reclaim(vram_budget_line(b, m), b, m, 0, 0);
        CHECK(wall.level == 2 && wall.urgent, "on the line the keep-age is off (level %d)", wall.level);
        CHECK(vram_reclaim(used, b, 0, free_in, 0).level == 0, "margin 0: no reclaim");
    }

    // The 10-minute session: usage crossed line - margin with ~1 GiB of released textures still held, then sat
    // on the line while blocks were refused. On the line, or after a refusal, the long keep does not apply.
    {
        uint64_t b = 10829ull << 20, m = vram_margin_auto(b);
        uint64_t used = 10353ull << 20;   // frame 33240: under the line, above line - margin
        VramReclaim pre = vram_reclaim(used, b, m, 1608ull << 20, 0);
        CHECK(pre.level >= 1 && pre.pack && !pre.urgent, "line - margin starts reclaim before a refusal (level %d)", pre.level);
        CHECK(vram_reclaim(used, b, m, 1608ull << 20, 1).urgent, "a block refused this sample drops released images now");
        uint64_t bon = 10801ull << 20, mon = vram_margin_auto(bon), uon = 10486ull << 20;   // frame 33360
        VramReclaim on = vram_reclaim(uon, bon, mon, 1328ull << 20, 0);
        CHECK(on.urgent && vram_drop_goal(uon, bon, mon) > 0, "on the line there is a drop goal");
        uint64_t stale = 120, keep = 150ull * 60;
        CHECK(!vram_released_drop(5000, 5000, stale, keep, 0), "under the keep, a young released image stays");
        CHECK(vram_released_drop(5000, 5000, stale, keep, 1), "on the line the keep-age does not apply");
        CHECK(!vram_released_drop(10, 10, stale, keep, 1), "a release from this moment stays even on the line");
        CHECK(vram_released_cap(b, 4) < (991ull << 20), "the default cap is under the 991 MiB that session held");
        CHECK(vram_pack_budget(64ull << 20, 1) >= (64ull << 20), "urgent packing moves a whole block in a frame");
        CHECK(vram_pack_budget(64ull << 20, 0) >= (32ull << 20), "otherwise a block empties within two frames");
        CHECK(!vram_pack_dest_ok(0, 2, 1), "a move into system memory is not a pack");
        CHECK(!vram_pack_dest_ok(1, 7, 7), "a move back into the block being emptied is not a pack");
        CHECK(vram_pack_dest_ok(1, 3, 7), "a device-local move into another block is a pack");
        CHECK(vram_pack_dest_ok(1, 3, 0), "with no block held out, another device block is fine");
        CHECK(vram_released_count_over(9022, 2048) == 9022 - 2048, "9000 released images are over a 2048 cap");
        CHECK(vram_released_count_over(2048, 2048) == 0, "at the cap nothing is over");
        CHECK(vram_released_count_over(9000, 0) == 0, "a count cap of 0 does not drop");
        {
            // Oldest first, including images with no memory of their own: those are the outside-VMA cost.
            struct { uint64_t age, bytes; int live; } rels[] = {
                {10, 0, 1}, {4000, 0, 1}, {3000, 80ull << 20, 1}, {5000, 0, 1},
            };
            uint32_t count = 4, cap = 2, stale = 120;
            uint32_t over = vram_released_count_over(count, cap);
            int guard = 8;
            while (over && guard--) {
                int best = -1;
                for (int i = 0; i < 4; i++) {
                    if (!rels[i].live || rels[i].age < stale) continue;
                    if (best < 0 || rels[i].age > rels[best].age) best = i;   // older age value is released earlier
                }
                CHECK(best >= 0, "count cap still has an old released image");
                if (best < 0) break;
                rels[best].live = 0;
                count--;
                over = vram_released_count_over(count, cap);
            }
            CHECK(count == cap && !rels[3].live && !rels[1].live && rels[0].live,
                  "oldest images go first, a just-released one stays, zero-byte ones count");
        }
        struct { uint64_t age, bytes; int live; } tex[] = {
            {5000, 100ull << 20, 1}, {4000, 600ull << 20, 1}, {10, 900ull << 20, 1}, {8000, 50ull << 20, 1},
        };
        uint64_t goal = 500ull << 20, freed = 0;
        int guard = 8;
        while (freed < goal && guard--) {
            int best = -1;
            for (int i = 0; i < 4; i++) {
                if (!tex[i].live || !vram_released_drop(tex[i].age, tex[i].age, stale, keep, 1)) continue;
                if (best < 0 || tex[i].bytes > tex[best].bytes) best = i;
            }
            CHECK(best >= 0, "pressure still has a released image to drop");
            if (best < 0) break;
            freed += tex[best].bytes;
            tex[best].live = 0;
        }
        CHECK(freed >= goal && !tex[1].live && tex[2].live, "largest eligible first, a just-released image stays (freed %llu)",
              (unsigned long long)(freed >> 20));
        int aged = 0;
        for (int i = 0; i < 4; i++)
            if (vram_released_drop(tex[i].age, tex[i].age, stale, keep, 0)) aged++;
        CHECK(aged == 0, "the same images are kept while usage is under the line and under the cap");
    }

    // A pack copy submitted on its own runs before the upload batch is flushed. The upload then writes the image
    // the copy already read, and that image is destroyed. Appending the copy to the open batch puts it after the upload.
    CHECK(!vram_pack_ordered(3, 0, 0, 2), "a copy submitted before the open upload batch is unordered");
    CHECK(vram_pack_ordered(3, 3, 1, 2), "a copy appended after the upload in the open batch is ordered");
    CHECK(!vram_pack_ordered(3, 3, 0, 2), "a copy before the upload in the same batch is unordered");
    CHECK(vram_pack_ordered(3, 4, 1, 3), "once the upload batch is submitted, a later batch may copy");
    CHECK(!vram_pack_ordered(3, 4, 1, 2), "a later batch while the upload batch is still open is unordered");
    CHECK(vram_pack_ordered(0, 4, 1, 3), "an image with no upload may be copied");
    {
        // The runtime's choice: the copy batch is the open one, or the next one when nothing is open, and the copy
        // is recorded after whatever that batch already holds.
        uint64_t submitted = 2, open = 3, uploaded = 3;
        uint64_t copy_batch = open ? open : submitted + 1;
        CHECK(vram_pack_ordered(uploaded, copy_batch, 1, submitted), "the open batch takes the copy after its upload");
        submitted = 3, open = 0, uploaded = 3;
        copy_batch = open ? open : submitted + 1;
        CHECK(vram_pack_ordered(uploaded, copy_batch, 1, submitted), "a flushed upload may be copied by the next batch");
        submitted = 2, open = 3, uploaded = 3;
        CHECK(!vram_pack_ordered(uploaded, 0, 0, submitted), "submitting the copy on its own overtakes the open upload");
    }

    uint64_t bare = simulate(0);
    CHECK(bare > (10957ull << 20), "without the gate the series crosses the budget (peak %.0f MiB)", bare / 1048576.0);
    uint64_t held = simulate(1);
    CHECK(held < (10957ull << 20) - (64ull << 20),
          "with the gate the series stays at least 64 MiB under the budget (peak %.0f MiB)", held / 1048576.0);
    CHECK(held <= bare, "the gate does not raise the peak");

    // Packing trapped free space once reclaim is on, 32 MiB a frame, before the recorded climb.
    {
        uint64_t peak = 0, refused = 0, returned = 0;
        int on = 0;
        for (int i = 0; i < NSERIES - 1; i++) {
            uint32_t f0 = SERIES[i].frame, f1 = SERIES[i + 1].frame;
            for (uint32_t f = f0 + 1; f <= f1; f++) {
                double t = (double)(f - f0) / (double)(f1 - f0);
                uint64_t want = (uint64_t)(SERIES[i].used + ((int)SERIES[i + 1].used - (int)SERIES[i].used) * t) << 20;
                uint64_t budget = (uint64_t)(SERIES[i].budget + ((int)SERIES[i + 1].budget - (int)SERIES[i].budget) * t) << 20;
                if (!budget) budget = (uint64_t)SERIES[i].budget << 20;
                uint64_t margin = vram_margin_auto(budget);
                // Trapped free space in the log: a couple of hundred MiB, then about 1.4 GiB after frame 162000.
                uint64_t trapped = f < 162000 ? (200ull << 20) : (1400ull << 20);
                if (returned < trapped) trapped -= returned;
                else trapped = 0;
                uint64_t sim = want > returned ? want - returned : 0;
                VramReclaim rc = vram_reclaim(sim, budget, margin, trapped, 0);
                if (rc.pack && trapped > (32ull << 20)) {
                    returned += 32ull << 20;
                    if (returned > want) returned = want;
                    sim = want > returned ? want - returned : 0;
                    on = 1;
                }
                if (!vram_allow_block(sim, budget, 64ull << 20, margin)) refused++;
                if (sim > peak) peak = sim;
            }
        }
        CHECK(on, "packing ran on the series");
        CHECK(refused == 0, "with packing, a 64 MiB block is never refused (%llu frames would refuse)",
              (unsigned long long)refused);
        CHECK(peak < vram_budget_line((10957ull << 20), vram_margin_auto(10957ull << 20)) + (64ull << 20),
              "packed peak %.0f MiB stays at the line", peak / 1048576.0);
    }

    if (!fails) printf("vram_budget: %d checks passed; simulated peak %.0f MiB with the gate, %.0f MiB without\n",
                       checks, held / 1048576.0, bare / 1048576.0);
    return fails ? 1 : 0;
}
