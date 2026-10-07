"""Ethan's Critters: Path F, the full-screen clip (a section of CRITTERS.DAT).

A clip is a run of whole 128x128 frames, each stored exactly as CHGfx's
framebuffer gfx_fb holds it, so the game streams a frame straight into
gfx_fb with one word copy per 512 B block and draws nothing itself:

    block 0         the clip header (little-endian):
                      0  char magic[4]   "CLIP"
                      4  u16  frames
                      6  u16  frame ms   (0: a still)
                      8  u16  palette[16]  RGB444 (pal::init()'s format)
                      40 zero to the end of the block
    block 1 + 16f + k   frame f, framebuffer rows 8k .. 8k+7: 64 B per row,
                        two pixels per byte, the even x in the low nibble

A frame is 16 blocks: one CMD18 of 16 blocks (about 3.3 ms on the board).
"""
import struct

W = H = 128
ROW_BYTES = W // 2                  # 64, CHGfx's GFX_FB_STRIDE
FRAME_BYTES = ROW_BYTES * H         # 8192
FRAME_BLOCKS = FRAME_BYTES // 512   # 16
MAGIC = b"CLIP"
HEAD = struct.Struct("<4sHH16H")


def fb_bytes(idx):
    """A frame (W*H palette indices, row-major) in gfx_fb's layout."""
    if len(idx) != W * H:
        raise ValueError("a frame is %d pixels, not %d" % (W * H, len(idx)))
    if max(idx) > 15:
        raise ValueError("a frame pixel is not a palette index (holes must be filled first)")
    out = bytearray(FRAME_BYTES)
    for n in range(0, W * H, 2):
        out[n >> 1] = idx[n] | (idx[n + 1] << 4)
    return bytes(out)


def fb_indices(data):
    """The other way: gfx_fb bytes -> W*H indices."""
    out = bytearray(W * H)
    for n, b in enumerate(data[:FRAME_BYTES]):
        out[2 * n] = b & 15
        out[2 * n + 1] = b >> 4
    return out


def pack(frames, palette, ms=0):
    """The section's bytes: the header block, then every frame."""
    if not frames or len(frames) > 0xFFFF:
        raise ValueError("a clip has 1 to 65535 frames")
    if len(palette) != 16 or any(not 0 <= c <= 0xFFF for c in palette):
        raise ValueError("the palette is sixteen RGB444 colours")
    head = bytearray(512)
    HEAD.pack_into(head, 0, MAGIC, len(frames), ms, *palette)
    return bytes(head) + b"".join(fb_bytes(f) for f in frames)


def unpack(data):
    """(frames as index arrays, palette, ms) from a section's bytes."""
    magic, n, ms, *palette = HEAD.unpack_from(data, 0)
    if magic != MAGIC:
        raise ValueError("not a clip: magic %r" % magic)
    if len(data) < 512 + n * FRAME_BYTES:
        raise ValueError("clip of %d frames cut short" % n)
    frames = [fb_indices(data[512 + f * FRAME_BYTES:512 + (f + 1) * FRAME_BYTES]) for f in range(n)]
    return frames, list(palette), ms
