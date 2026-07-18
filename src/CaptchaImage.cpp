/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#include "CaptchaImage.h"

#include <QRandomGenerator>

#include <array>
#include <cmath>

namespace ircabot {

namespace {

// 5x7 glyphs for '0'-'9' then 'A'-'Z', row-major, low 5 bits (bit4 = leftmost).
constexpr unsigned char FONT5X7[36][7] = {
    {0x0e,0x11,0x13,0x15,0x19,0x11,0x0e}, // 0
    {0x04,0x0c,0x04,0x04,0x04,0x04,0x0e}, // 1
    {0x0e,0x11,0x01,0x02,0x04,0x08,0x1f}, // 2
    {0x1f,0x02,0x04,0x02,0x01,0x11,0x0e}, // 3
    {0x02,0x06,0x0a,0x12,0x1f,0x02,0x02}, // 4
    {0x1f,0x10,0x1e,0x01,0x01,0x11,0x0e}, // 5
    {0x06,0x08,0x10,0x1e,0x11,0x11,0x0e}, // 6
    {0x1f,0x01,0x02,0x04,0x08,0x08,0x08}, // 7
    {0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e}, // 8
    {0x0e,0x11,0x11,0x0f,0x01,0x02,0x0c}, // 9
    {0x0e,0x11,0x11,0x1f,0x11,0x11,0x11}, // A
    {0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e}, // B
    {0x0e,0x11,0x10,0x10,0x10,0x11,0x0e}, // C
    {0x1c,0x12,0x11,0x11,0x11,0x12,0x1c}, // D
    {0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f}, // E
    {0x1f,0x10,0x10,0x1e,0x10,0x10,0x10}, // F
    {0x0e,0x11,0x10,0x17,0x11,0x11,0x0e}, // G
    {0x11,0x11,0x11,0x1f,0x11,0x11,0x11}, // H
    {0x0e,0x04,0x04,0x04,0x04,0x04,0x0e}, // I
    {0x07,0x02,0x02,0x02,0x12,0x12,0x0c}, // J
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // K
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1f}, // L
    {0x11,0x1b,0x15,0x15,0x11,0x11,0x11}, // M
    {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, // N
    {0x0e,0x11,0x11,0x11,0x11,0x11,0x0e}, // O
    {0x1e,0x11,0x11,0x1e,0x10,0x10,0x10}, // P
    {0x0e,0x11,0x11,0x11,0x15,0x12,0x0d}, // Q
    {0x1e,0x11,0x11,0x1e,0x14,0x12,0x11}, // R
    {0x0e,0x11,0x10,0x0e,0x01,0x11,0x0e}, // S
    {0x1f,0x04,0x04,0x04,0x04,0x04,0x04}, // T
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0e}, // U
    {0x11,0x11,0x11,0x11,0x11,0x0a,0x04}, // V
    {0x11,0x11,0x11,0x15,0x15,0x1b,0x11}, // W
    {0x11,0x11,0x0a,0x04,0x0a,0x11,0x11}, // X
    {0x11,0x11,0x0a,0x04,0x04,0x04,0x04}, // Y
    {0x1f,0x01,0x02,0x04,0x08,0x10,0x1f}, // Z
};

int glyphIndex(QChar c)
{
    const char16_t u = c.unicode();
    if (u >= u'0' && u <= u'9') {
        return u - u'0';
    }
    const char16_t v = c.toUpper().unicode();
    if (v >= u'A' && v <= u'Z') {
        return 10 + (v - u'A');
    }
    return -1;
}

const std::array<quint32, 256>& crcTable()
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t{};
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[n] = c;
        }
        return t;
    }();
    return table;
}

quint32 crc32Of(const QByteArray& data)
{
    quint32 c = 0xFFFFFFFFu;
    for (const char ch : data) {
        c = crcTable()[(c ^ static_cast<unsigned char>(ch)) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

quint32 adler32Of(const QByteArray& data)
{
    quint32 a = 1;
    quint32 b = 0;
    constexpr quint32 MOD = 65521;
    for (const char ch : data) {
        a = (a + static_cast<unsigned char>(ch)) % MOD;
        b = (b + a) % MOD;
    }
    return (b << 16) | a;
}

void appendBE32(QByteArray& out, quint32 v)
{
    out.append(static_cast<char>((v >> 24) & 0xFF));
    out.append(static_cast<char>((v >> 16) & 0xFF));
    out.append(static_cast<char>((v >> 8) & 0xFF));
    out.append(static_cast<char>(v & 0xFF));
}

void appendChunk(QByteArray& png, const char* type, const QByteArray& data)
{
    appendBE32(png, static_cast<quint32>(data.size()));
    QByteArray typeAndData;
    typeAndData.append(type, 4);
    typeAndData.append(data);
    png.append(typeAndData);
    appendBE32(png, crc32Of(typeAndData));
}

// Wrap raw bytes in a zlib stream that uses only stored (uncompressed) DEFLATE
// blocks. Valid, and avoids linking zlib for a few-KB captcha image.
QByteArray zlibStored(const QByteArray& raw)
{
    QByteArray z;
    z.append(static_cast<char>(0x78)); // CMF
    z.append(static_cast<char>(0x01)); // FLG (no preset dict, fastest)
    qsizetype pos = 0;
    const qsizetype n = raw.size();
    do {
        const int block = static_cast<int>(qMin<qsizetype>(65535, n - pos));
        const bool last = (pos + block >= n);
        z.append(static_cast<char>(last ? 1 : 0)); // BFINAL, BTYPE=00 (stored)
        z.append(static_cast<char>(block & 0xFF));
        z.append(static_cast<char>((block >> 8) & 0xFF));
        const int nlen = (~block) & 0xFFFF;
        z.append(static_cast<char>(nlen & 0xFF));
        z.append(static_cast<char>((nlen >> 8) & 0xFF));
        z.append(raw.constData() + pos, block);
        pos += block;
    } while (pos < n);
    appendBE32(z, adler32Of(raw)); // Adler-32 is stored big-endian
    return z;
}

} // namespace

QByteArray renderCaptchaPng(const QString& text)
{
    QRandomGenerator* const rng = QRandomGenerator::global();

    constexpr int S = 6;          // pixel scale of the 5x7 font
    constexpr int GLYPH_W = 5;
    constexpr int GLYPH_H = 7;
    constexpr int cell = GLYPH_W * S + 12;
    constexpr int padX = 18;
    constexpr int H = 74;

    const int n = static_cast<int>(text.size());
    const int W = padX * 2 + qMax(1, n) * cell;

    QByteArray img(W * H, char(0)); // palette index 0 = background everywhere
    const auto setPx = [&](int x, int y, unsigned char idx) {
        if (x >= 0 && x < W && y >= 0 && y < H) {
            img[y * W + x] = static_cast<char>(idx);
        }
    };

    const double freq = 0.05 + rng->bounded(30) / 1000.0;
    const double phase = rng->bounded(628) / 100.0;
    const double amp = 3.0;

    for (int i = 0; i < n; ++i) {
        const int gi = glyphIndex(text.at(i));
        if (gi < 0) {
            continue;
        }
        const int baseX = padX + i * cell + 4 + rng->bounded(-2, 3);
        const int baseY = (H - GLYPH_H * S) / 2 + rng->bounded(-4, 5);
        const double shear = rng->bounded(-30, 31) / 100.0; // slant -0.3..0.3
        const double cy = baseY + GLYPH_H * S / 2.0;
        for (int row = 0; row < GLYPH_H; ++row) {
            const unsigned char bits = FONT5X7[gi][row];
            for (int col = 0; col < GLYPH_W; ++col) {
                if (!(bits & (1 << (4 - col)))) {
                    continue;
                }
                for (int sy = 0; sy < S; ++sy) {
                    for (int sx = 0; sx < S; ++sx) {
                        const int py0 = baseY + row * S + sy;
                        const int px = baseX + col * S + sx + static_cast<int>(shear * (py0 - cy));
                        const int py = py0 + static_cast<int>(amp * std::sin((baseX + col * S) * freq + phase));
                        setPx(px, py, 1); // ink
                    }
                }
            }
        }
    }

    // Noise: dim speckles and a few crossing lines over the glyphs.
    for (int k = 0; k < W * H / 90; ++k) {
        setPx(rng->bounded(W), rng->bounded(H), 2);
    }
    for (int l = 0; l < 3; ++l) {
        int x1 = rng->bounded(W);
        int y1 = rng->bounded(H);
        const int x2 = rng->bounded(W);
        const int y2 = rng->bounded(H);
        const int dx = std::abs(x2 - x1);
        const int dy = -std::abs(y2 - y1);
        const int sx = x1 < x2 ? 1 : -1;
        const int sy = y1 < y2 ? 1 : -1;
        int err = dx + dy;
        while (true) {
            setPx(x1, y1, 2);
            if (x1 == x2 && y1 == y2) {
                break;
            }
            const int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x1 += sx; }
            if (e2 <= dx) { err += dx; y1 += sy; }
        }
    }

    // Raw scanlines: filter byte 0 then W palette indices per row.
    QByteArray raw;
    raw.reserve(H * (1 + W));
    for (int y = 0; y < H; ++y) {
        raw.append(char(0));
        raw.append(img.constData() + y * W, W);
    }

    QByteArray png;
    static const unsigned char SIG[8] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    png.append(reinterpret_cast<const char*>(SIG), 8);

    QByteArray ihdr;
    appendBE32(ihdr, static_cast<quint32>(W));
    appendBE32(ihdr, static_cast<quint32>(H));
    ihdr.append(char(8)); // bit depth
    ihdr.append(char(3)); // color type: indexed
    ihdr.append(char(0)); // compression
    ihdr.append(char(0)); // filter
    ihdr.append(char(0)); // interlace
    appendChunk(png, "IHDR", ihdr);

    QByteArray plte;
    static const unsigned char PAL[3][3] = {
        {0x0d, 0x0c, 0x0b}, // 0 background (near-black)
        {0xe8, 0xa3, 0x3d}, // 1 ink (amber)
        {0x6a, 0x5f, 0x4a}, // 2 noise (dim)
    };
    for (const auto& c : PAL) {
        plte.append(static_cast<char>(c[0]));
        plte.append(static_cast<char>(c[1]));
        plte.append(static_cast<char>(c[2]));
    }
    appendChunk(png, "PLTE", plte);

    appendChunk(png, "IDAT", zlibStored(raw));
    appendChunk(png, "IEND", QByteArray());
    return png;
}

} // namespace ircabot
