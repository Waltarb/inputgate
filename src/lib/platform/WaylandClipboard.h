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

#include "inputleap/clipboard_types.h"

#include <functional>
#include <memory>

namespace inputleap {

class IClipboard;

/*! Wayland clipboard access through the data-control protocol
    (ext-data-control-v1, falling back to wlr-data-control-unstable-v1).

    Data-control lets a client read and set the clipboard without having
    keyboard focus, which is what a network input sharing tool needs. It is
    supported by KWin (KDE Plasma 6) and wlroots based compositors; GNOME
    doesn't implement it.

    The Wayland connection lives on its own thread. All public methods may be
    called from the main thread.
*/
class WaylandClipboard {
public:
    //! Called (from the Wayland thread) when another application sets a clipboard
    using GrabbedCallback = std::function<void(ClipboardID)>;

    //! Throws std::runtime_error if the compositor doesn't support data-control
    explicit WaylandClipboard(GrabbedCallback on_grabbed);
    ~WaylandClipboard();

    WaylandClipboard(const WaylandClipboard&) = delete;
    WaylandClipboard& operator=(const WaylandClipboard&) = delete;

    //! Copies the current contents of clipboard \p id into \p clipboard
    bool get(ClipboardID id, IClipboard* clipboard) const;

    //! Makes the contents of \p clipboard the current clipboard \p id
    bool set(ClipboardID id, const IClipboard* clipboard);

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace inputleap
