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

#pragma once

#include <string>

namespace inputleap {

/*! Converts between PNG, which Linux applications expect on the clipboard,
    and IClipboard::kBitmap (a BMP without file header), which every Windows
    application accepts.
*/
class WaylandImageConverter {
public:
    //! Encodes a 24 or 32 bpp BI_RGB DIB as PNG. Returns an empty string on failure.
    static std::string dib_to_png(const std::string& dib);

    /*! Decodes an 8 bit RGB, RGBA, grey or palette PNG into a 32 bpp DIB.
        Returns an empty string for anything else.
    */
    static std::string png_to_dib(const std::string& png);
};

} // namespace inputleap
