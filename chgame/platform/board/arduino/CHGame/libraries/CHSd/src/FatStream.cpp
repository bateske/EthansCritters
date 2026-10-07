// FatStream.cpp - fat::stream(), see Fat.h: a file's blocks through
// sd::stream(), split where the file's runs split. Lifted from CHStlView's
// card::stream(), with the range checked before anything is read. A file
// of its own so that Fat.cpp, and with it every image that only reads
// blocks one at a time, stays byte for byte what it was. Like Fat.cpp it
// runs on the PC too: the simulator's card and the host tests supply
// sd::stream().
#pragma GCC optimize("Os", "no-ipa-sra")
#include "Fat.h"
#include "SdSpi.h"

namespace fat {

bool stream(const Run *r, uint8_t nRuns, uint32_t k, uint32_t n, uint8_t *buf0, uint8_t *buf1,
            sd::BlockFn fn, void *ctx) {
    const Run *end = r + nRuns;
    uint32_t len = 0;
    for (const Run *p = r; p < end; p++) len += p->blocks;
    if (k > len || n > len - k) return false;
    // In range, so the loop ends inside the runs.
    for (; n; r++) {
        if (k >= r->blocks) { k -= r->blocks; continue; }
        uint32_t take = r->blocks - k;
        if (take > n) take = n;
        if (!sd::stream(r->lba + k, take, buf0, buf1, fn, ctx)) return false;
        n -= take;
        k = 0;
    }
    return true;
}

}  // namespace fat
