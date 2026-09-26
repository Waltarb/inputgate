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

#include "platform/MSWindowsClipboardFileConverter.h"

#include "inputleap/FileBundle.h"
#include "base/Log.h"

#include <shellapi.h>
#include <shlobj.h>

#include <cstring>

namespace inputleap {

IClipboard::EFormat MSWindowsClipboardFileConverter::getFormat() const
{
    return IClipboard::kFileList;
}

UINT MSWindowsClipboardFileConverter::getWin32Format() const
{
    return CF_HDROP;
}

HANDLE MSWindowsClipboardFileConverter::fromIClipboard(const std::string& data) const
{
    if (data.empty()) {
        return nullptr;
    }

    auto paths = FileBundle::unpack(data);
    if (paths.empty()) {
        return nullptr;
    }

    // DROPFILES header followed by a double NUL terminated list of wide paths
    std::wstring list;
    for (const auto& path : paths) {
        list += path.wstring();
        list += L'\0';
    }
    list += L'\0';

    SIZE_T size = sizeof(DROPFILES) + list.size() * sizeof(wchar_t);
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
    if (handle == nullptr) {
        return nullptr;
    }
    auto* drop = static_cast<DROPFILES*>(GlobalLock(handle));
    if (drop == nullptr) {
        GlobalFree(handle);
        return nullptr;
    }
    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;
    std::memcpy(reinterpret_cast<char*>(drop) + sizeof(DROPFILES), list.data(),
                list.size() * sizeof(wchar_t));
    GlobalUnlock(handle);
    return handle;
}

std::string MSWindowsClipboardFileConverter::toIClipboard(HANDLE data) const
{
    auto drop = static_cast<HDROP>(data);
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);

    std::vector<fs::path> paths;
    for (UINT i = 0; i < count; ++i) {
        UINT length = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring path(length + 1, L'\0');
        DragQueryFileW(drop, i, &path[0], length + 1);
        path.resize(length);
        paths.emplace_back(path);
    }

    std::string bundle;
    if (paths.empty() || !FileBundle::pack(paths, bundle)) {
        return {};
    }
    return bundle;
}

} // namespace inputleap
