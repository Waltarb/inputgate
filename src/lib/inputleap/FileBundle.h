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

#include "io/filesystem.h"

#include <cstdint>
#include <string>
#include <vector>

namespace inputleap {

/*! Packs copied files and folders into a single buffer so that they can be
    sent as the IClipboard::kFileList clipboard format, and unpacks them on
    the receiving side.

    Buffer layout (all integers big endian):

        4 bytes  magic "IGFB"
        4 bytes  version (1)
        4 bytes  number of entries
        per entry:
            1 byte   type (0 = directory, 1 = file)
            4 bytes  length of the relative path
            n bytes  relative path, UTF-8, '/' separated
            8 bytes  file size (files only)
            n bytes  file contents (files only)

    The top level entries are the items that were copied; everything inside
    a copied folder follows it with a path prefixed by the folder name.
*/
class FileBundle {
public:
    //! Pack \p paths. Returns false if the total exceeds max_bytes() or a file can't be read
    static bool pack(const std::vector<fs::path>& paths, std::string& out);

    /*! Unpack \p data into a fresh folder below receive_dir(). Returns the
        top level items that were written, or an empty list on failure.
        Unpacking the same data twice in a row returns the earlier result
        without writing the files again.
    */
    static std::vector<fs::path> unpack(const std::string& data);

    //! Unpack \p data into \p dest_dir. Returns the top level items written.
    static std::vector<fs::path> unpack_to(const std::string& data, const fs::path& dest_dir);

    /*! Folder received files are written to. INPUTGATE_RECEIVE_DIR overrides
        the default of <Downloads>/Inputgate.
    */
    static fs::path receive_dir();

    /*! Largest bundle that will be sent. INPUTGATE_MAX_FILE_MB overrides the
        default of 90 MiB.
    */
    static std::uint64_t max_bytes();

    //! Returns true if \p rel is a safe relative path (no "..", no absolute paths)
    static bool is_safe_relative_path(const std::string& rel);

    //! Converts a list of local paths to a text/uri-list (file:// URIs, CRLF separated)
    static std::string to_uri_list(const std::vector<fs::path>& paths);

    //! Parses a text/uri-list and returns the local paths it contains
    static std::vector<fs::path> from_uri_list(const std::string& uri_list);
};

} // namespace inputleap
