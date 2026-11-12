#pragma once

#include "protocol.hpp"
#include <utility>

namespace rep::bc7 {

// Scalar BC7Prep inverse transform adapted from the DNF DB Reader port of
// CUE4Parse BC7PrepDecoder. Copyright (c) CUE4Parse contributors.
// CUE4Parse independently implemented the decoder using Epic Games' Oodle
// SDK 2.9.14 bc7prep_decode.cpp as a format reference under the Unreal Engine EULA.
// Apache-2.0, see licenses/BC7PREP_LICENSE.txt.

inline uint64_t prepGet64(std::span<const uint8_t> bytes, size_t offset) {
    return at<uint64_t>(bytes, offset);
}
inline uint64_t prepGet32(std::span<const uint8_t> bytes, size_t offset) {
    return at<uint32_t>(bytes, offset);
}
inline uint64_t prepGet16(std::span<const uint8_t> bytes, size_t offset) {
    return at<uint16_t>(bytes, offset);
}

inline uint64_t prepBitExtract(uint64_t value, unsigned start, unsigned width) {
    return (value >> start) & ((uint64_t{1} << width) - 1);
}
inline uint64_t prepPackedAdd(uint64_t a, uint64_t b, uint64_t msb, uint64_t low) {
    return ((a & low) + (b & low)) ^ ((a ^ b) & msb);
}
inline void prepDecorr(uint64_t& r, uint64_t& g, uint64_t& b, uint64_t msb, uint64_t low) {
    const auto y = r;
    r = prepPackedAdd(y, g, msb, low);
    g = y;
    b = prepPackedAdd(y, b, msb, low);
}
inline void prepDecorr(uint64_t& r, uint64_t& g, uint64_t& b, uint64_t msb) {
    prepDecorr(r, g, b, msb, ~msb);
}

inline uint64_t prepMode4DecorrFast(uint64_t rgba) {
    const auto y = (rgba & 0x3ff) * (1 + (1 << 10) + (1 << 20));
    const auto c = ((rgba >> 10) & 0x3ff) | (rgba & 0xfffff00000);
    return prepPackedAdd(y, c, 0x8421084210, ~uint64_t(0x8421084210));
}
inline uint64_t prepCompact32To7_2x(uint64_t x) { return (x & 0x7f) | ((x >> 25) & 0x3f80); }
inline uint64_t prepCompact24To7_3x(uint64_t x) {
    return (x & 0x7f) | ((x >> 17) & 0x3f80) | ((x >> 34) & 0x1fc000);
}
inline uint64_t prepCompact24To7_2x(uint64_t x) { return (x & 0x7f) | ((x >> 17) & 0x3f80); }
inline uint64_t prepCompact16To1_4x(uint64_t x) {
    return ((x & 0x0001000100010001) * 0x0001000200040008) >> 48;
}
inline uint64_t prepCompact16To1_2x(uint64_t x) { return (x & 1) | ((x >> 15) & 2); }
inline uint64_t prepCompact16To5_4x(uint64_t x) {
    x &= 0x001f001f001f001f;
    x = ((x >> 11) | x) & 0x000003ff000003ff;
    return ((x >> 22) | x) & 0xfffff;
}
inline uint64_t prepCompact16To5_2x(uint64_t x) { return (x & 0x1f) | ((x >> 11) & 0x3e0); }
inline uint64_t prepCompact16To6_4x(uint64_t x) {
    x = ((x >> 10) | x) & 0x00000fff00000fff;
    return ((x >> 20) | x) & 0xffffff;
}
inline uint64_t prepCompact8To7_8x(uint64_t x) {
    constexpr uint64_t stay1 = 0x007f007f007f007f;
    x = ((x & (stay1 << 8)) >> 1) | (x & stay1);
    constexpr uint64_t stay2 = 0x00003fff00003fff;
    x = ((x & ~stay2) >> 2) | (x & stay2);
    constexpr uint64_t stay4 = 0x000000000fffffff;
    return ((x & ~stay4) >> 4) | (x & stay4);
}
inline uint64_t prepCompact8To7_6x(uint64_t x) {
    constexpr uint64_t stay = 0x007f007f007f;
    x = ((x & (stay << 8)) >> 1) | (x & stay);
    return (x & 0x0000000ffff) | ((x >> 2) & 0x0000fffc000) | ((x >> 4) & 0x3fff0000000);
}
inline uint64_t prepCompact8To1_8x(uint64_t x) {
    return ((x & 0x0101010101010101) * 0x0102040810204080) >> 56;
}
inline uint64_t prepExpand4To5_12x(uint64_t x) {
    x = ((x & 0x0000ffff00000000) << 8) | ((x & 0x00000000ffff0000) << 4) | (x & 0xffff);
    constexpr uint64_t stay = 0x0000ff000ff000ff;
    x = ((x & ~stay) << 2) | (x & stay);
    return x + (x & 0x03c0f03c0f03c0f0);
}
inline uint64_t prepExpand4To5_4x(uint64_t x) {
    x = ((x & 0x0ff00) << 2) | (x & 0xff);
    return x + (x & 0x3c0f0);
}
inline uint64_t prepExpand2To6_4x(uint64_t x) {
    x = ((x << 8) | x) & 0x0f00f;
    return ((x << 4) | x) & 0xc30c3;
}
inline uint64_t prepExpand1To5_12x(uint64_t x) {
    x = ((x & 0xfff) * 0x100010001) & 0xf0000f0000f;
    return (x * 0x1111) & 0x084210842108421;
}
inline uint64_t prepExpand1To5_4x(uint64_t x) { return ((x & 0xf) * 0x1111) & 0x8421; }

// The input is a restored, unsplit Prep record.
inline std::pair<uint64_t, uint64_t> prepDecodeBlock(int mode, std::span<const uint8_t> b,
                                                     bool switchColorspace) {
    switch (mode) {
    case 0: {
        auto inLo = prepGet64(b, 0);
        auto inHi = prepGet64(b, 8);
        auto rbits = prepBitExtract(inLo, 0, 24);
        auto gbits = prepBitExtract(inLo, 24, 24);
        auto bbits = prepBitExtract(inLo, 48, 16) | (prepBitExtract(inHi, 0, 8) << 16);
        auto partbits = prepBitExtract(inHi, 8, 4);
        if (switchColorspace) {
            prepDecorr(rbits, gbits, bbits, 0x888888);
        }
        auto lo = uint64_t(1);
        lo |= partbits << 1;
        lo |= rbits << 5;
        lo |= gbits << 29;
        lo |= bbits << 53;
        auto hi = bbits >> 11;
        hi |= inHi & ~uint64_t(0x1fff);
        return {lo, hi};
    }
    case 1: {
        auto rgbs = prepGet64(b, 0);
        auto extra = prepGet64(b, 8);
        auto rbits = prepCompact16To6_4x((rgbs >> 0) & 0x003f003f003f003f);
        auto gbits = prepCompact16To6_4x((rgbs >> 5) & 0x003e003e003e003e);
        auto bbits = prepCompact16To6_4x((rgbs >> 10) & 0x003e003e003e003e);
        auto expandedGb = prepExpand2To6_4x((extra & 0xff));
        gbits |= (expandedGb >> 0) & 0x41041;
        bbits |= (expandedGb >> 1) & 0x41041;
        if (switchColorspace) {
            prepDecorr(rbits, gbits, bbits, 0x820820);
        }
        auto lo = uint64_t(0x2);
        lo |= (extra >> 6) & 0xfc;
        lo |= rbits << 8;
        lo |= gbits << 32;
        lo |= bbits << 56;
        auto hi = bbits >> 8;
        hi |= extra & ~uint64_t(0xffff);
        return {lo, hi};
    }
    case 2: {
        auto endpt0 = prepGet64(b, 0);
        auto endpt1 = prepGet32(b, 8);
        auto index = prepGet32(b, 12);
        auto rbits = prepCompact16To5_4x(endpt0 >> 1) | (prepCompact16To5_2x(endpt1 >> 1) << 20);
        auto gbits = prepCompact16To5_4x(endpt0 >> 6) | (prepCompact16To5_2x(endpt1 >> 6) << 20);
        auto bbits = prepCompact16To5_4x(endpt0 >> 11) | (prepCompact16To5_2x(endpt1 >> 11) << 20);
        auto partbits = prepCompact16To1_4x(endpt0) | (prepCompact16To1_2x(endpt1) << 4);
        if (switchColorspace) {
            prepDecorr(rbits, gbits, bbits, 0x21084210);
        }
        auto lo = uint64_t(0x4);
        lo |= partbits << 3;
        lo |= rbits << 9;
        lo |= gbits << 39;
        auto hi = gbits >> 25;
        hi |= bbits << 5;
        hi |= (index & ~uint64_t(7)) << 32;
        return {lo, hi};
    }
    case 3: {
        auto endpt0 = prepGet64(b, 0);
        auto endpt1 = prepGet32(b, 8);
        auto index = prepGet32(b, 12);
        auto rbits = prepCompact24To7_3x(endpt0 >> 0) | ((endpt1 & 0x007f00) << (21 - 8));
        auto gbits = prepCompact24To7_3x(endpt0 >> 8) | ((endpt1 & 0x7f0000) << (21 - 16));
        auto bbits = prepCompact24To7_2x(endpt0 >> 16) | (prepCompact24To7_2x(endpt1) << 14);
        auto partbits = prepCompact8To1_8x(endpt0 >> 7);
        if (switchColorspace) {
            prepDecorr(rbits, gbits, bbits, 0x8102040);
        }
        auto lo = uint64_t(0x8);
        lo |= (partbits & 0x3f) << 4;
        lo |= rbits << 10;
        lo |= gbits << 38;
        auto hi = gbits >> 26;
        hi |= bbits << 2;
        hi |= (partbits & 0xc0) << 24;
        hi |= index << 32;
        return {lo, hi};
    }
    case 4: {
        auto loPtr = 0;
        auto inLo = prepGet32(b, loPtr) + (prepGet16(b, loPtr + 4) << 32);
        auto secondPtr = 6;
        auto inHi0 = prepGet16(b, secondPtr);
        auto inHi1 = prepGet64(b, secondPtr + 2);
        auto rgba = prepBitExtract(inLo, 2, 40);
        if (switchColorspace) {
            rgba = prepMode4DecorrFast(rgba);
        }
        auto crot = prepBitExtract(inHi0, 0, 2);
        auto shiftAmount = int(((0 - crot) & 3) * 10);
        auto xorMask = (rgba ^ (rgba << shiftAmount)) & 0xffc0000000;
        rgba ^= xorMask;
        rgba ^= xorMask >> shiftAmount;
        rgba += rgba & 0x0ffc0000000;
        rgba += rgba & 0x1f000000000;
        rgba |= (((inLo & 3) * 0x21) & 0x41) << 30;
        auto lo = uint64_t(0x10);
        lo |= crot << 5;
        lo |= prepBitExtract(inLo, 42, 1) << 7;
        lo |= rgba << 8;
        lo |= prepBitExtract(inHi0, 2, 14) << 50;
        auto hi = inHi1;
        return {lo, hi};
    }
    case 5: {
        auto endpoints = prepGet64(b, 0);
        auto indicesBits = prepGet64(b, 8);
        auto lo = uint64_t(0x20);
        uint64_t hi = 0;
        if (switchColorspace) {
            auto rbits = (endpoints >> 0) & 0x000000fe000000fe;
            auto gbits = (endpoints >> 8) & 0x000000fe000000fe;
            auto bbits = (endpoints >> 16) & 0x000000fe000000fe;
            prepDecorr(rbits, gbits, bbits, 0x8000000080, 0x7e0000007e);
            endpoints = rbits | (gbits << 8) | (bbits << 16) | (endpoints & 0xff010101ff010101);
            auto crot = indicesBits & 3;
            auto shiftAmount = int(((0 - crot) & 3) << 3);
            auto xorMask = (endpoints ^ (endpoints << shiftAmount)) & 0xff000000ff000000;
            endpoints ^= xorMask;
            endpoints ^= xorMask >> shiftAmount;
            lo |= crot << 6;
            lo |= prepCompact32To7_2x(endpoints >> 1) << 8;
            lo |= prepCompact32To7_2x(endpoints >> 9) << 22;
            lo |= prepCompact32To7_2x(endpoints >> 17) << 36;
            lo |= ((endpoints >> 24) & 0xff) << 50;
            lo |= ((endpoints >> 56) & 0x3f) << 58;
        } else {
            auto crot = indicesBits & 3;
            auto shiftAmount = int(((0 - crot) & 3) << 4);
            auto xorMask = (endpoints ^ (endpoints << shiftAmount)) & 0xffff000000000000;
            endpoints ^= xorMask;
            endpoints ^= xorMask >> shiftAmount;
            lo |= crot << 6;
            lo |= prepCompact8To7_6x(endpoints >> 1) << 8;
            lo |= ((endpoints >> 48) & 0xff) << 50;
            lo |= ((endpoints >> 56) & 0x3f) << 58;
        }
        hi = (endpoints >> 62) & 3;
        hi |= indicesBits & ~uint64_t(3);
        return {lo, hi};
    }
    case 6: {
        auto endpoints = prepGet64(b, 0);
        auto indicesBits = prepGet64(b, 8);
        auto lo = uint64_t(0x40);
        if (switchColorspace) {
            auto rbits = prepCompact32To7_2x(endpoints >> 0);
            auto gbits = prepCompact32To7_2x(endpoints >> 8);
            auto bbits = prepCompact32To7_2x(endpoints >> 16);
            auto abits = prepCompact32To7_2x(endpoints >> 24);
            prepDecorr(rbits, gbits, bbits, 0x2040);
            lo |= rbits << 7;
            lo |= gbits << 21;
            lo |= bbits << 35;
            lo |= abits << 49;
        } else {
            auto deint = prepCompact8To7_8x(endpoints);
            lo |= deint << 7;
        }
        lo |= endpoints & (uint64_t{1} << 63);
        auto hi = indicesBits;
        return {lo, hi};
    }
    case 7: {
        auto prgbs = prepGet64(b, 0);
        auto rest = prepGet32(b, 8);
        rest |= prepGet32(b, 12) << 32;
        uint64_t lo = 0, hi = 0;
        if (switchColorspace) {
            auto rbits = prepCompact16To5_4x(prgbs >> 1);
            auto gbits = prepCompact16To5_4x(prgbs >> 6);
            auto bbits = prepCompact16To5_4x(prgbs >> 11);
            auto pbits = prepCompact16To1_4x(prgbs);
            auto abits = prepBitExtract(rest, 8, 20);
            prepDecorr(rbits, gbits, bbits, 0x84210);
            lo = 0x80;
            lo |= prepBitExtract(rest, 28, 6) << 8;
            lo |= rbits << 14;
            lo |= gbits << 34;
            lo |= bbits << 54;
            hi = bbits >> 10;
            hi |= abits << 10;
            hi |= pbits << 30;
        } else {
            auto pbits = prepBitExtract(rest, 8, 4);
            auto rgbbits = prepExpand4To5_12x(prgbs) << 1;
            rgbbits |= prepExpand1To5_12x(rest >> 12);
            auto abits = prepExpand4To5_4x(prgbs >> 48) << 1;
            abits |= prepExpand1To5_4x(rest >> 24);
            lo = 0x80;
            lo |= prepBitExtract(rest, 28, 6) << 8;
            lo |= rgbbits << 14;
            hi = rgbbits >> 50;
            hi |= abits << 10;
            hi |= pbits << 30;
        }
        hi |= rest & ~uint64_t(0x3FFFFffff);
        return {lo, hi};
    }
    case 8: {
        return {prepGet64(b, 0), prepGet64(b, 8)};
    }
    case 9: {
        auto r = uint64_t(b[0]);
        auto g = uint64_t(b[1]);
        auto blue = uint64_t(b[2]);
        if (switchColorspace) {
            const auto y = r;
            r = (y + g) & 255;
            g = y;
            blue = (y + blue) & 255;
        }
        constexpr uint64_t bit6 = (uint64_t{0x40} << 8) | (uint64_t{0x40} << 22) | (uint64_t{0x40} << 36);
        constexpr uint64_t lo7 = (uint64_t{0x7f} << 8) | (uint64_t{0x7f} << 22) | (uint64_t{0x7f} << 36);
        auto c = (r << 8) | (g << 22) | (blue << 36);
        auto t = c << 6;
        c = ((c >> 1) & lo7) - (c & ~lo7);
        c += t + (t & bit6);
        c |= uint64_t(b[3]) << 50;
        return {c | 0x20, 0xaaaaaaac};
    }
    }
    throw Error("invalid BC7Prep mode");
}

} // namespace rep::bc7
