/*
 * Inputgate -- mouse, keyboard, clipboard and file sharing utility
 * Copyright (C) 2026 The Inputgate Developers
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "platform/WaylandImageConverter.h"

#include "base/Log.h"

#include <zlib.h>

#include <cstdint>
#include <cstdlib>
#include <vector>

namespace inputleap {

namespace {

const char kPngSignature[8] = { '\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n' };

// Refuse absurd sizes before allocating
const std::uint64_t kMaxPixels = 16384ULL * 16384ULL;

std::uint32_t read_le32(const std::string& d, std::size_t o)
{
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(d[o])) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d[o + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d[o + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d[o + 3])) << 24);
}

std::uint16_t read_le16(const std::string& d, std::size_t o)
{
    return static_cast<std::uint16_t>(static_cast<std::uint8_t>(d[o]) |
                                      (static_cast<std::uint8_t>(d[o + 1]) << 8));
}

std::uint32_t read_be32(const std::string& d, std::size_t o)
{
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d[o])) << 24) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d[o + 1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d[o + 2])) << 8) |
           static_cast<std::uint32_t>(static_cast<std::uint8_t>(d[o + 3]));
}

void write_le32(std::string& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) out += static_cast<char>((v >> (8 * i)) & 0xff);
}

void write_le16(std::string& out, std::uint16_t v)
{
    out += static_cast<char>(v & 0xff);
    out += static_cast<char>(v >> 8);
}

void write_be32(std::string& out, std::uint32_t v)
{
    for (int shift = 24; shift >= 0; shift -= 8) out += static_cast<char>((v >> shift) & 0xff);
}

void write_chunk(std::string& out, const char* type, const std::string& data)
{
    write_be32(out, static_cast<std::uint32_t>(data.size()));
    std::string body(type, 4);
    body += data;
    out += body;
    write_be32(out, static_cast<std::uint32_t>(
        crc32(0, reinterpret_cast<const Bytef*>(body.data()), static_cast<uInt>(body.size()))));
}

int paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

} // namespace

std::string WaylandImageConverter::dib_to_png(const std::string& dib)
{
    if (dib.size() < 40) {
        return {};
    }
    std::uint32_t header_size = read_le32(dib, 0);
    auto width = static_cast<std::int32_t>(read_le32(dib, 4));
    auto height = static_cast<std::int32_t>(read_le32(dib, 8));
    std::uint16_t bit_count = read_le16(dib, 14);
    std::uint32_t compression = read_le32(dib, 16);

    bool bitfields = compression == 3 && header_size == 40;
    if ((compression != 0 && compression != 3) || (bit_count != 24 && bit_count != 32) ||
        (compression == 3 && bit_count != 32) || width <= 0 || height == 0)
    {
        LOG_DEBUG("image: unsupported bitmap (%d bpp, compression %u)", bit_count, compression);
        return {};
    }

    bool bottom_up = height > 0;
    std::uint32_t h = static_cast<std::uint32_t>(bottom_up ? height : -height);
    std::uint32_t w = static_cast<std::uint32_t>(width);
    if (static_cast<std::uint64_t>(w) * h > kMaxPixels) {
        return {};
    }

    std::size_t offset = header_size + (bitfields ? 12 : 0);
    std::size_t bytes_pp = bit_count / 8;
    std::size_t stride = (w * bytes_pp + 3) & ~static_cast<std::size_t>(3);
    if (offset + stride * h > dib.size()) {
        return {};
    }

    // CF_DIB alpha is usually all zero, meaning "no alpha"
    bool has_alpha = false;
    if (bit_count == 32) {
        for (std::uint32_t y = 0; y < h && !has_alpha; ++y) {
            const char* row = dib.data() + offset + y * stride;
            for (std::uint32_t x = 0; x < w; ++x) {
                if (row[x * 4 + 3] != 0) {
                    has_alpha = true;
                    break;
                }
            }
        }
    }

    std::string raw;
    raw.reserve((w * 4 + 1) * h);
    for (std::uint32_t y = 0; y < h; ++y) {
        std::uint32_t src_y = bottom_up ? h - 1 - y : y;
        const char* row = dib.data() + offset + src_y * stride;
        raw += '\0'; // filter: none
        for (std::uint32_t x = 0; x < w; ++x) {
            const char* px = row + x * bytes_pp;
            raw += px[2];
            raw += px[1];
            raw += px[0];
            raw += has_alpha ? px[3] : '\xff';
        }
    }

    uLongf compressed_size = compressBound(static_cast<uLong>(raw.size()));
    std::string compressed(compressed_size, '\0');
    if (compress2(reinterpret_cast<Bytef*>(&compressed[0]), &compressed_size,
                  reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()),
                  Z_DEFAULT_COMPRESSION) != Z_OK)
    {
        return {};
    }
    compressed.resize(compressed_size);

    std::string ihdr;
    write_be32(ihdr, w);
    write_be32(ihdr, h);
    ihdr += '\x08'; // bit depth
    ihdr += '\x06'; // RGBA
    ihdr += std::string(3, '\0'); // compression, filter, interlace

    std::string png(kPngSignature, sizeof(kPngSignature));
    write_chunk(png, "IHDR", ihdr);
    write_chunk(png, "IDAT", compressed);
    write_chunk(png, "IEND", {});
    return png;
}

std::string WaylandImageConverter::png_to_dib(const std::string& png)
{
    if (png.size() < 8 + 25 || png.compare(0, 8, std::string(kPngSignature, 8)) != 0) {
        return {};
    }

    std::uint32_t w = 0, h = 0;
    int bit_depth = 0, color_type = -1, interlace = 0;
    std::string idat, palette, trns;

    std::size_t pos = 8;
    while (pos + 12 <= png.size()) {
        std::uint32_t len = read_be32(png, pos);
        if (len > png.size() - pos - 12) {
            return {};
        }
        std::string type = png.substr(pos + 4, 4);
        const std::size_t data = pos + 8;
        if (type == "IHDR" && len >= 13) {
            w = read_be32(png, data);
            h = read_be32(png, data + 4);
            bit_depth = static_cast<std::uint8_t>(png[data + 8]);
            color_type = static_cast<std::uint8_t>(png[data + 9]);
            interlace = static_cast<std::uint8_t>(png[data + 12]);
        } else if (type == "PLTE") {
            palette = png.substr(data, len);
        } else if (type == "tRNS") {
            trns = png.substr(data, len);
        } else if (type == "IDAT") {
            idat.append(png, data, len);
        } else if (type == "IEND") {
            break;
        }
        pos += 12 + len;
    }

    int channels = 0;
    switch (color_type) {
        case 0: channels = 1; break; // grey
        case 2: channels = 3; break; // RGB
        case 3: channels = 1; break; // palette
        case 4: channels = 2; break; // grey + alpha
        case 6: channels = 4; break; // RGBA
        default: return {};
    }
    if (bit_depth != 8 || interlace != 0 || w == 0 || h == 0 ||
        static_cast<std::uint64_t>(w) * h > kMaxPixels || (color_type == 3 && palette.empty()))
    {
        LOG_DEBUG("image: unsupported png (depth %d, color type %d, interlace %d)",
                  bit_depth, color_type, interlace);
        return {};
    }

    const std::size_t bpp = static_cast<std::size_t>(channels);
    const std::size_t row_bytes = w * bpp;
    std::vector<unsigned char> raw((row_bytes + 1) * h);
    uLongf raw_size = static_cast<uLongf>(raw.size());
    if (uncompress(raw.data(), &raw_size, reinterpret_cast<const Bytef*>(idat.data()),
                   static_cast<uLong>(idat.size())) != Z_OK || raw_size != raw.size())
    {
        return {};
    }

    // Undo the per row filters in place
    std::vector<unsigned char> prev(row_bytes, 0);
    for (std::uint32_t y = 0; y < h; ++y) {
        unsigned char* line = &raw[y * (row_bytes + 1)];
        unsigned char filter = line[0];
        unsigned char* cur = line + 1;
        for (std::size_t i = 0; i < row_bytes; ++i) {
            int a = i >= bpp ? cur[i - bpp] : 0;
            int b = prev[i];
            int c = i >= bpp ? prev[i - bpp] : 0;
            switch (filter) {
                case 0: break;
                case 1: cur[i] = static_cast<unsigned char>(cur[i] + a); break;
                case 2: cur[i] = static_cast<unsigned char>(cur[i] + b); break;
                case 3: cur[i] = static_cast<unsigned char>(cur[i] + ((a + b) >> 1)); break;
                case 4: cur[i] = static_cast<unsigned char>(cur[i] + paeth(a, b, c)); break;
                default: return {};
            }
        }
        std::copy(cur, cur + row_bytes, prev.begin());
    }

    // 32 bpp bottom-up BI_RGB DIB, BGRA
    std::string dib;
    dib.reserve(40 + static_cast<std::size_t>(w) * h * 4);
    write_le32(dib, 40);
    write_le32(dib, w);
    write_le32(dib, h);
    write_le16(dib, 1);
    write_le16(dib, 32);
    write_le32(dib, 0); // BI_RGB
    write_le32(dib, w * h * 4);
    write_le32(dib, 2835); // 72 dpi
    write_le32(dib, 2835);
    write_le32(dib, 0);
    write_le32(dib, 0);

    for (std::uint32_t y = h; y-- > 0;) {
        const unsigned char* src = &raw[y * (row_bytes + 1) + 1];
        for (std::uint32_t x = 0; x < w; ++x) {
            unsigned char r, g, b, a = 255;
            const unsigned char* p = src + x * bpp;
            switch (color_type) {
                case 0: r = g = b = p[0]; break;
                case 2: r = p[0]; g = p[1]; b = p[2]; break;
                case 3: {
                    std::size_t idx = p[0];
                    if (idx * 3 + 2 >= palette.size()) {
                        return {};
                    }
                    r = static_cast<unsigned char>(palette[idx * 3]);
                    g = static_cast<unsigned char>(palette[idx * 3 + 1]);
                    b = static_cast<unsigned char>(palette[idx * 3 + 2]);
                    if (idx < trns.size()) {
                        a = static_cast<unsigned char>(trns[idx]);
                    }
                    break;
                }
                case 4: r = g = b = p[0]; a = p[1]; break;
                default: r = p[0]; g = p[1]; b = p[2]; a = p[3]; break;
            }
            dib += static_cast<char>(b);
            dib += static_cast<char>(g);
            dib += static_cast<char>(r);
            dib += static_cast<char>(a);
        }
    }
    return dib;
}

} // namespace inputleap
