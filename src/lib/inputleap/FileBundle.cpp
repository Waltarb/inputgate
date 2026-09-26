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

#include "inputleap/FileBundle.h"

#include "base/Log.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <userenv.h>
#include <wtsapi32.h>
#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "userenv.lib")
#endif

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>

namespace inputleap {

namespace {

const char kMagic[4] = { 'I', 'G', 'F', 'B' };
const std::uint32_t kVersion = 1;

enum : std::uint8_t {
    kEntryDirectory = 0,
    kEntryFile = 1,
};

void write_u32(std::string& out, std::uint32_t v)
{
    for (int shift = 24; shift >= 0; shift -= 8) {
        out += static_cast<char>((v >> shift) & 0xff);
    }
}

void write_u64(std::string& out, std::uint64_t v)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        out += static_cast<char>((v >> shift) & 0xff);
    }
}

// Sequential reader over the bundle that fails instead of reading past the end
class Reader {
public:
    explicit Reader(const std::string& data) : data_(data) {}

    bool read_bytes(std::size_t n, const char** out)
    {
        if (n > data_.size() - pos_) {
            return false;
        }
        *out = data_.data() + pos_;
        pos_ += n;
        return true;
    }

    bool read_u8(std::uint8_t& v)
    {
        const char* p;
        if (!read_bytes(1, &p)) {
            return false;
        }
        v = static_cast<std::uint8_t>(*p);
        return true;
    }

    bool read_u32(std::uint32_t& v)
    {
        const char* p;
        if (!read_bytes(4, &p)) {
            return false;
        }
        v = 0;
        for (int i = 0; i < 4; ++i) {
            v = (v << 8) | static_cast<std::uint8_t>(p[i]);
        }
        return true;
    }

    bool read_u64(std::uint64_t& v)
    {
        const char* p;
        if (!read_bytes(8, &p)) {
            return false;
        }
        v = 0;
        for (int i = 0; i < 8; ++i) {
            v = (v << 8) | static_cast<std::uint8_t>(p[i]);
        }
        return true;
    }

private:
    const std::string& data_;
    std::size_t pos_ = 0;
};

std::string generic_utf8(const fs::path& path)
{
    return path.generic_u8string();
}

struct PackState {
    std::string& out;
    std::uint32_t count = 0;
    std::uint64_t total = 0;
    std::uint64_t limit = 0;
};

bool add_file(PackState& state, const fs::path& path, const std::string& rel)
{
    std::error_code ec;
    auto size = fs::file_size(path, ec);
    if (ec) {
        LOG_WARN("file transfer: can't read size of %s: %s",
                 path.u8string().c_str(), ec.message().c_str());
        return false;
    }
    state.total += size;
    if (state.total > state.limit) {
        LOG_NOTE("file transfer: copied files exceed %llu MiB, not sending them",
                 static_cast<unsigned long long>(state.limit / (1024 * 1024)));
        return false;
    }

    std::ifstream in;
    open_utf8_path(in, path, std::ios::in | std::ios::binary);
    if (!in) {
        LOG_WARN("file transfer: can't open %s", path.u8string().c_str());
        return false;
    }

    state.out += static_cast<char>(kEntryFile);
    write_u32(state.out, static_cast<std::uint32_t>(rel.size()));
    state.out += rel;
    write_u64(state.out, size);

    auto start = state.out.size();
    state.out.resize(start + size);
    in.read(&state.out[start], static_cast<std::streamsize>(size));
    if (static_cast<std::uint64_t>(in.gcount()) != size) {
        LOG_WARN("file transfer: short read on %s", path.u8string().c_str());
        return false;
    }
    ++state.count;
    return true;
}

bool add_entry(PackState& state, const fs::path& path, const std::string& rel)
{
    std::error_code ec;
    auto status = fs::symlink_status(path, ec);
    if (ec) {
        LOG_WARN("file transfer: can't stat %s", path.u8string().c_str());
        return false;
    }

    if (fs::is_symlink(status)) {
        // Follow links to regular files, but don't walk linked folders to
        // avoid loops and accidentally sending half the disk
        if (fs::is_regular_file(path, ec)) {
            return add_file(state, path, rel);
        }
        LOG_DEBUG("file transfer: skipping link %s", path.u8string().c_str());
        return true;
    }

    if (fs::is_regular_file(status)) {
        return add_file(state, path, rel);
    }

    if (fs::is_directory(status)) {
        state.out += static_cast<char>(kEntryDirectory);
        write_u32(state.out, static_cast<std::uint32_t>(rel.size()));
        state.out += rel;
        ++state.count;

        for (const auto& child : fs::directory_iterator(path, ec)) {
            auto child_rel = rel + "/" + generic_utf8(child.path().filename());
            if (!add_entry(state, child.path(), child_rel)) {
                return false;
            }
        }
        if (ec) {
            LOG_WARN("file transfer: can't list %s", path.u8string().c_str());
            return false;
        }
        return true;
    }

    LOG_DEBUG("file transfer: skipping special file %s", path.u8string().c_str());
    return true;
}

std::string timestamp_folder_name()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &tm);
    return buf;
}

#ifdef _WIN32
/*  The server is normally started by the Inputgate service and then runs as
    SYSTEM inside the user's session, so USERPROFILE points at the system
    profile. Look up the profile of the user logged into our session. */
fs::path session_user_profile()
{
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) {
        return {};
    }
    HANDLE token = nullptr;
    if (!WTSQueryUserToken(session, &token)) {
        return {}; // not running as SYSTEM, USERPROFILE is right
    }
    wchar_t buf[MAX_PATH];
    DWORD size = MAX_PATH;
    fs::path result;
    if (GetUserProfileDirectoryW(token, buf, &size)) {
        result = fs::path(buf);
    }
    CloseHandle(token);
    return result;
}
#endif

#ifndef _WIN32
// Reads XDG_DOWNLOAD_DIR from ~/.config/user-dirs.dirs
fs::path xdg_download_dir(const fs::path& home)
{
    const char* config_home = std::getenv("XDG_CONFIG_HOME");
    fs::path config = (config_home && *config_home) ? fs::path(config_home) : home / ".config";

    std::ifstream in;
    open_utf8_path(in, config / "user-dirs.dirs");
    std::string line;
    while (std::getline(in, line)) {
        const std::string key = "XDG_DOWNLOAD_DIR=";
        if (line.compare(0, key.size(), key) != 0) {
            continue;
        }
        std::string value = line.substr(key.size());
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        const std::string home_var = "$HOME";
        if (value.compare(0, home_var.size(), home_var) == 0) {
            value = home.u8string() + value.substr(home_var.size());
        }
        if (!value.empty()) {
            return fs::u8path(value);
        }
    }
    return home / "Downloads";
}
#endif

int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

bool FileBundle::pack(const std::vector<fs::path>& paths, std::string& out)
{
    out.clear();
    out.append(kMagic, sizeof(kMagic));
    write_u32(out, kVersion);
    write_u32(out, 0); // entry count, patched below

    PackState state{out};
    state.limit = max_bytes();

    for (const auto& path : paths) {
        auto name = generic_utf8(path.filename());
        if (name.empty()) {
            // e.g. a drive root such as C:\ -- refuse rather than guessing
            LOG_NOTE("file transfer: can't send %s", path.u8string().c_str());
            out.clear();
            return false;
        }
        if (!add_entry(state, path, name)) {
            out.clear();
            return false;
        }
    }

    std::string count;
    write_u32(count, state.count);
    out.replace(8, 4, count);

    LOG_INFO("file transfer: packed %u items, %llu bytes", state.count,
             static_cast<unsigned long long>(state.total));
    return true;
}

bool FileBundle::is_safe_relative_path(const std::string& rel)
{
    if (rel.empty() || rel.front() == '/') {
        return false;
    }

    std::size_t start = 0;
    while (start <= rel.size()) {
        auto end = rel.find('/', start);
        if (end == std::string::npos) {
            end = rel.size();
        }
        auto part = rel.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        for (char c : part) {
            // backslash and colon would let a Windows receiver escape the
            // folder (C:\..., \\server\...) or write to alternate data streams
            if (c == '\\' || c == ':' || static_cast<unsigned char>(c) < 0x20) {
                return false;
            }
        }
        start = end + 1;
    }
    return true;
}

std::vector<fs::path> FileBundle::unpack_to(const std::string& data, const fs::path& dest_dir)
{
    std::vector<fs::path> top_level;

    Reader reader(data);
    const char* magic;
    std::uint32_t version = 0;
    std::uint32_t count = 0;
    if (!reader.read_bytes(4, &magic) || std::string(magic, 4) != std::string(kMagic, 4) ||
        !reader.read_u32(version) || version != kVersion || !reader.read_u32(count))
    {
        LOG_WARN("file transfer: received data is not a file bundle");
        return {};
    }

    std::error_code ec;
    fs::create_directories(dest_dir, ec);
    if (ec) {
        LOG_ERR("file transfer: can't create %s: %s", dest_dir.u8string().c_str(),
                ec.message().c_str());
        return {};
    }

    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint8_t type = 0;
        std::uint32_t rel_size = 0;
        const char* rel_data;
        if (!reader.read_u8(type) || !reader.read_u32(rel_size) ||
            !reader.read_bytes(rel_size, &rel_data))
        {
            LOG_WARN("file transfer: truncated file bundle");
            return {};
        }
        std::string rel(rel_data, rel_size);
        if (!is_safe_relative_path(rel)) {
            LOG_WARN("file transfer: refusing unsafe path \"%s\"", rel.c_str());
            return {};
        }

        fs::path target = dest_dir / fs::u8path(rel);
        bool is_top_level = rel.find('/') == std::string::npos;

        if (type == kEntryDirectory) {
            fs::create_directories(target, ec);
            if (ec) {
                LOG_ERR("file transfer: can't create %s", target.u8string().c_str());
                return {};
            }
        } else if (type == kEntryFile) {
            std::uint64_t size = 0;
            const char* contents;
            if (!reader.read_u64(size) || size > data.size() ||
                !reader.read_bytes(static_cast<std::size_t>(size), &contents))
            {
                LOG_WARN("file transfer: truncated file bundle");
                return {};
            }
            fs::create_directories(target.parent_path(), ec);
            std::ofstream out;
            open_utf8_path(out, target, std::ios::out | std::ios::binary | std::ios::trunc);
            out.write(contents, static_cast<std::streamsize>(size));
            if (!out) {
                LOG_ERR("file transfer: can't write %s", target.u8string().c_str());
                return {};
            }
        } else {
            LOG_WARN("file transfer: unknown entry type %d", type);
            return {};
        }

        if (is_top_level) {
            top_level.push_back(target);
        }
    }

    LOG_NOTE("file transfer: received %u items into %s", count, dest_dir.u8string().c_str());
    return top_level;
}

std::vector<fs::path> FileBundle::unpack(const std::string& data)
{
    // The same clipboard contents may be set more than once (e.g. every
    // time the cursor enters this screen); don't write the files again.
    static std::mutex mutex;
    static std::size_t last_hash = 0;
    static std::size_t last_size = 0;
    static std::vector<fs::path> last_result;

    std::lock_guard<std::mutex> lock(mutex);
    auto hash = std::hash<std::string>{}(data);
    if (!last_result.empty() && hash == last_hash && data.size() == last_size) {
        bool all_exist = true;
        for (const auto& path : last_result) {
            std::error_code ec;
            all_exist = all_exist && fs::exists(path, ec);
        }
        if (all_exist) {
            return last_result;
        }
    }

    fs::path dest = receive_dir() / fs::u8path(timestamp_folder_name());
    for (int i = 2; fs::exists(dest); ++i) {
        dest = receive_dir() / fs::u8path(timestamp_folder_name() + "_" + std::to_string(i));
    }

    auto result = unpack_to(data, dest);
    if (!result.empty()) {
        last_hash = hash;
        last_size = data.size();
        last_result = result;
    }
    return result;
}

fs::path FileBundle::receive_dir()
{
#ifdef _WIN32
    // wide variants so non-ASCII user names work
    if (const wchar_t* dir = _wgetenv(L"INPUTGATE_RECEIVE_DIR")) {
        if (*dir) {
            return fs::path(dir);
        }
    }
    fs::path profile = session_user_profile();
    if (profile.empty()) {
        if (const wchar_t* env = _wgetenv(L"USERPROFILE")) {
            profile = fs::path(env);
        }
    }
    fs::path base = profile.empty() ? fs::temp_directory_path() : profile / "Downloads";
#else
    if (const char* dir = std::getenv("INPUTGATE_RECEIVE_DIR")) {
        if (*dir) {
            return fs::u8path(dir);
        }
    }
    const char* home = std::getenv("HOME");
    fs::path base = home ? xdg_download_dir(fs::u8path(home)) : fs::temp_directory_path();
#endif
    return base / "Inputgate";
}

std::uint64_t FileBundle::max_bytes()
{
    // stays below the default 100 MB clipboard limit of the server GUI
    std::uint64_t mib = 90;
    if (const char* value = std::getenv("INPUTGATE_MAX_FILE_MB")) {
        char* end = nullptr;
        auto parsed = std::strtoull(value, &end, 10);
        if (end != value && parsed > 0) {
            mib = parsed;
        }
    }
    // The clipboard protocol uses 32 bit sizes, leave room for the headers
    const std::uint64_t hard_limit = 0xF0000000ULL;
    std::uint64_t bytes = mib * 1024 * 1024;
    return bytes < hard_limit ? bytes : hard_limit;
}

std::string FileBundle::to_uri_list(const std::vector<fs::path>& paths)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string result;
    for (const auto& path : paths) {
        std::string p = path.generic_u8string();
        if (p.empty() || p.front() != '/') {
            p = "/" + p; // C:/foo -> /C:/foo
        }
        result += "file://";
        for (unsigned char c : p) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                c == '/' || c == '-' || c == '_' || c == '.' || c == '~' || c == ':')
            {
                result += static_cast<char>(c);
            } else {
                result += '%';
                result += hex[c >> 4];
                result += hex[c & 0xf];
            }
        }
        result += "\r\n";
    }
    return result;
}

std::vector<fs::path> FileBundle::from_uri_list(const std::string& uri_list)
{
    std::vector<fs::path> paths;
    std::istringstream in(uri_list);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\0')) {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::string scheme = "file://";
        if (line.compare(0, scheme.size(), scheme) != 0) {
            continue;
        }
        std::string rest = line.substr(scheme.size());
        // skip the host part (usually empty or "localhost")
        auto slash = rest.find('/');
        if (slash == std::string::npos) {
            continue;
        }
        rest = rest.substr(slash);

        std::string decoded;
        for (std::size_t i = 0; i < rest.size(); ++i) {
            if (rest[i] == '%' && i + 2 < rest.size()) {
                int hi = hex_value(rest[i + 1]);
                int lo = hex_value(rest[i + 2]);
                if (hi >= 0 && lo >= 0) {
                    decoded += static_cast<char>((hi << 4) | lo);
                    i += 2;
                    continue;
                }
            }
            decoded += rest[i];
        }
#ifdef _WIN32
        // /C:/foo -> C:/foo
        if (decoded.size() >= 3 && decoded[0] == '/' && decoded[2] == ':') {
            decoded.erase(0, 1);
        }
#endif
        paths.push_back(fs::u8path(decoded));
    }
    return paths;
}

} // namespace inputleap
