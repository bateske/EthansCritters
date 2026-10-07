// Fat: read-only FAT16/FAT32 on top of sd::read (CHSd; clean-room, MIT,
// from HypeRunner: see NOTICE).
//
// Written from the Microsoft FAT specification (BPB fields, FAT type from
// the cluster count, 8.3 directory entries, end-of-chain marks) and the MBR
// partition table layout. It only turns a file name into LBA runs: after
// that a file is read as raw card blocks through its run list, so there is
// no FAT write code, no cache and no file object. Every call borrows the
// caller's 512 B buffer (4-byte aligned) and clobbers it; the volume state
// is 24 B. What a game does not call is left out of its image by the linker.
//
// Names are 8.3 short names as the directory stores them: 11 characters,
// capitals, the name and the extension padded with spaces ("WORDS   DIC"),
// and '?' stands for any character. Long-name entries, volume labels and
// hidden entries are skipped; a file never matches a folder name, nor a
// folder a file name.
#pragma once
#include <stdint.h>
#include "SdSpi.h"

namespace fat {

enum Err : int8_t {
    OK = 0,
    E_READ = -1,             // the card did not deliver a block
    E_NOFS = -10,            // no FAT16/FAT32 volume: no partition, bad BPB, FAT12
    E_EXFAT = -11,           // exFAT (or NTFS) volume: reformat the card as FAT32
    E_NOTFOUND = -12,        // no such file or directory (or not mounted)
    E_FRAG = -13,            // more runs than the caller has room for
    E_CHAIN = -14,           // cluster chain broken, looped, or not the file's length
};

struct Run { uint32_t lba, blocks; };
struct File { uint32_t cluster, size; };      // first cluster, size in bytes

// Finds the volume: a superfloppy boot sector at LBA 0, else the first MBR
// partition of type 01/04/06/0B/0C/0E. An exFAT boot sector at LBA 0, or an
// MBR with a type 07 partition and no FAT one, is E_EXFAT.
int8_t mount(uint8_t *buf);

// A file in the root directory.
int8_t find(const char *name, File &f, uint8_t *buf);

// A folder in the root directory.
int8_t folder(const char *name, File &dir, uint8_t *buf);

// The (skip + 1)th file in a folder whose name fits pattern ("????????CWD").
// Its 11-character name is copied to nameOut (no NUL) unless that is null.
// E_NOTFOUND: there are no more.
int8_t match(const File &dir, const char *pattern, uint8_t skip, File &f, char *nameOut, uint8_t *buf);

// The root directory as a folder, for match() and list(). E_NOTFOUND: not
// mounted.
int8_t root(File &dir);

// Every file and folder in dir, in directory order: fn gets each one's
// 11-character short name (no NUL), its first cluster and size, and whether
// it is a folder; returning false stops the walk. ".", "..", deleted
// entries, volume labels, long-name parts and hidden entries are left out.
// fn must not touch buf (the directory sector is in it) nor read the card.
// Returns OK or an error. (For a browser: CHStlView.)
typedef bool (*ListFn)(const char *name, const File &f, bool isDir, void *ctx);
int8_t list(const File &dir, ListFn fn, void *ctx, uint8_t *buf);

// Walks f's cluster chain once and returns its extents: up to maxRuns runs
// of consecutive blocks, the last one trimmed to the file's size (a file of
// 0 bytes has 0 runs). Returns the run count or an error. The walk is bounded
// by the file's size and the chain must end right there, so a looped or
// truncated FAT gives E_CHAIN instead of a hang.
int8_t runs(const File &f, Run *out, uint8_t maxRuns, uint8_t *buf);

// The usual way in: sd::init(), mount(), find(name) and runs(), in one go.
// Returns the run count; 0 if any step failed or the file is empty. (A game
// that tells the player why there is no card calls the steps itself.)
uint8_t open(const char *name, Run *out, uint8_t maxRuns, uint8_t *buf);

// Block k of a file (0 = its first 512 bytes) through its runs, into dst.
// False: past the end, or the card did not deliver it.
bool read(const Run *run, uint32_t nRuns, uint32_t k, uint8_t *dst);

// Blocks [k, k + n) of a file through its runs, by sd::stream(): one
// multi-block read per run the range touches (so a range across a run
// boundary costs a second command), each block handed to fn(block, ctx)
// once, in order, while the next arrives by DMA into the other buffer.
// buf0/buf1 are 512 B each, 4-byte aligned (gfx_chunkScratch() and the
// half after it, between gfx_wait() and the next flush); a block is only
// valid during its fn call. False: the range runs past the end (refused
// before anything is read), or the card failed part way (fn has seen a
// first part of the range, not always every block that arrived: fn gets a
// block once the next one's token is in, so a timeout loses the block
// before it too; see sd::stream() for what to do next).
// (In its own file, FatStream.cpp, so it is in no image that does not
// call it.)
bool stream(const Run *run, uint8_t nRuns, uint32_t k, uint32_t n, uint8_t *buf0, uint8_t *buf1,
            sd::BlockFn fn, void *ctx);

}  // namespace fat
