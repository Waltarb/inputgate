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

#include "platform/MSWindowsClipboard.h"

namespace inputleap {

//! Convert to/from the registered "PNG" clipboard format used by browsers and Office
class MSWindowsClipboardPNGConverter : public IMSWindowsClipboardConverter {
public:
    MSWindowsClipboardPNGConverter();
    ~MSWindowsClipboardPNGConverter() override = default;

    // IMSWindowsClipboardConverter overrides
    IClipboard::EFormat getFormat() const override;
    UINT getWin32Format() const override;
    HANDLE fromIClipboard(const std::string&) const override;
    std::string toIClipboard(HANDLE) const override;

private:
    UINT format_;
};

} // namespace inputleap
