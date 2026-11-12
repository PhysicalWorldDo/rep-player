#pragma once

#include "bc7_prep.hpp"
#include <algorithm>

#define BCDECDEF static inline
#define BCDEC_IMPLEMENTATION
#include "../vendor/bcdec/bcdec.h"
#undef BCDEC_IMPLEMENTATION
#undef BCDECDEF

namespace rep::bc7 {

inline std::optional<Bytes> restorePrep(std::span<const uint8_t> data, size_t blockCount) {
    constexpr size_t headerSize = 48;
    if (data.size() < headerSize || at<uint32_t>(data, 0) != 0x7bc)
        return std::nullopt;

    std::array<size_t, 10> counts{}, offsets{}, used{};
    constexpr std::array<size_t, 10> splitPoints{8, 8, 12, 12, 6, 8, 8, 12, 0, 0};
    size_t count = 0, end = headerSize;
    for (size_t mode = 0; mode < counts.size(); ++mode) {
        counts[mode] = at<uint32_t>(data, 8 + mode * 4);
        offsets[mode] = end;
        count += counts[mode];
        end += counts[mode] * (mode == 9 ? 4 : 16);
    }
    // A standard BC7 block can begin with the same bytes as the Prep magic.
    if (count != blockCount || end + (count + 1) / 2 != data.size())
        return std::nullopt;

    const auto flags = at<uint32_t>(data, 4);
    if (flags & ~uint32_t{0x100ff})
        throw Error("unsupported BC7Prep flags");
    const auto modes = data.subspan(end);
    auto modeAt = [&](size_t i) { return size_t((modes[i / 2] >> (4 * (i % 2))) & 15); };
    for (size_t i = 0; i < count; ++i) {
        const auto mode = modeAt(i);
        if (mode >= counts.size())
            throw Error("invalid BC7Prep mode");
        ++used[mode];
    }
    if (used != counts)
        throw Error("BC7Prep mode counts disagree with mode stream");

    Bytes blocks(count * 16);
    used.fill(0);
    for (size_t i = 0; i < count; ++i) {
        const auto mode = modeAt(i), row = used[mode]++;
        const size_t stride = mode == 9 ? 4 : 16;
        const auto* source = data.data() + offsets[mode];
        std::array<uint8_t, 16> record{};
        if (flags & (1u << mode)) {
            const auto first = splitPoints[mode], second = stride - first;
            std::copy_n(source + row * first, first, record.data());
            std::copy_n(source + counts[mode] * first + row * second, second, record.data() + first);
        } else {
            std::copy_n(source + row * stride, stride, record.data());
        }
        const auto [lo, hi] = prepDecodeBlock(int(mode), record, (flags & 0x10000) != 0);
        std::memcpy(blocks.data() + i * 16, &lo, sizeof(lo));
        std::memcpy(blocks.data() + i * 16 + sizeof(lo), &hi, sizeof(hi));
    }
    return blocks;
}

inline Bytes decodeDds(std::span<const uint8_t> raw, int width, int height) {
    if (raw.size() < 148 || std::memcmp(raw.data(), "DDS ", 4) || std::memcmp(raw.data() + 84, "DX10", 4))
        throw Error("missing BC7 DDS DX10 header");
    if (at<uint32_t>(raw, 4) != 124 || at<uint32_t>(raw, 76) != 32)
        throw Error("invalid BC7 DDS header sizes");
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        at<uint32_t>(raw, 12) != uint32_t(height) || at<uint32_t>(raw, 16) != uint32_t(width))
        throw Error("BC7 DDS dimensions disagree with IMG");
    const auto format = at<uint32_t>(raw, 128);
    if (format != 98 && format != 99)
        throw Error("unsupported BC7 DDS DXGI format");
    if (at<uint32_t>(raw, 132) != 3 || (at<uint32_t>(raw, 136) & 4) || at<uint32_t>(raw, 140) != 1)
        throw Error("BC7 DDS requires a single 2D texture");

    const int columns = (width + 3) / 4, rows = (height + 3) / 4;
    const size_t count = size_t(columns) * rows;
    auto blocks = raw.subspan(148);
    auto restored = restorePrep(blocks, count);
    if (restored)
        blocks = *restored;
    if (blocks.size() < count * 16)
        throw Error("truncated BC7 block data");

    Bytes rgba(size_t(width) * height * 4);
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < columns; ++x) {
            // bcdec reads two 64-bit words. DDS DX10 data can start unaligned.
            std::array<unsigned long long, 2> block;
            std::memcpy(block.data(), blocks.data() + (size_t(y) * columns + x) * 16, 16);
            std::array<uint8_t, 64> pixels;
            bcdec_bc7(block.data(), pixels.data(), 16);
            const int copyWidth = std::min(4, width - 4 * x), copyHeight = std::min(4, height - 4 * y);
            for (int row = 0; row < copyHeight; ++row) {
                const auto offset = (size_t(y * 4 + row) * width + x * 4) * 4;
                std::copy_n(pixels.data() + row * 16, copyWidth * 4, rgba.data() + offset);
            }
        }
    }
    return rgba;
}

} // namespace rep::bc7
