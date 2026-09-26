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
#include <ole2.h>

#include <cstring>

#pragma comment(lib, "ole32.lib")

namespace inputleap {

namespace {

std::vector<std::wstring> files_from_hdrop(HDROP drop)
{
    std::vector<std::wstring> files;
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count; ++i) {
        UINT length = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring path(length + 1, L'\0');
        DragQueryFileW(drop, i, &path[0], length + 1);
        path.resize(length);
        files.push_back(path);
    }
    return files;
}

} // namespace

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
    return pack(files_from_hdrop(static_cast<HDROP>(data)));
}

std::string MSWindowsClipboardFileConverter::pack(const std::vector<std::wstring>& files)
{
    std::vector<fs::path> paths(files.begin(), files.end());
    std::string bundle;
    if (paths.empty() || !FileBundle::pack(paths, bundle)) {
        return {};
    }
    return bundle;
}

std::vector<std::wstring> MSWindowsClipboardFileConverter::read_ole_file_list()
{
    /*  Explorer puts copied files on the clipboard through OLE. A process
        running as SYSTEM (the Inputgate service starts the server that way)
        then only sees the "DataObject" marker, not CF_HDROP itself, so ask
        Explorer's data object for the file list directly. */
    std::vector<std::wstring> files;

    HRESULT init = OleInitialize(nullptr);
    if (FAILED(init)) {
        LOG_DEBUG("file transfer: OleInitialize failed: 0x%08lx", init);
        return files;
    }

    IDataObject* object = nullptr;
    HRESULT hr = OleGetClipboard(&object);
    if (SUCCEEDED(hr) && object != nullptr) {
        FORMATETC format = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM medium = {};
        hr = object->GetData(&format, &medium);
        if (SUCCEEDED(hr)) {
            files = files_from_hdrop(static_cast<HDROP>(medium.hGlobal));
            ReleaseStgMedium(&medium);
        } else {
            LOG_DEBUG("file transfer: no file list in the OLE clipboard: 0x%08lx", hr);
        }
        object->Release();
    } else {
        LOG_DEBUG("file transfer: OleGetClipboard failed: 0x%08lx", hr);
    }

    OleUninitialize();
    LOG_DEBUG("file transfer: %zu files on the OLE clipboard", files.size());
    return files;
}

} // namespace inputleap
