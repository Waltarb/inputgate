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

#include <gtest/gtest.h>

#include <fstream>
#include <random>

namespace inputleap {

namespace {

class TempDir {
public:
    TempDir()
    {
        std::random_device rd;
        path_ = fs::temp_directory_path() / ("inputgate-test-" + std::to_string(rd()));
        fs::create_directories(path_);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

void write_file(const fs::path& path, const std::string& contents)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << contents;
}

std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write_u32(std::string& out, std::uint32_t v)
{
    for (int shift = 24; shift >= 0; shift -= 8) {
        out += static_cast<char>((v >> shift) & 0xff);
    }
}

} // namespace

TEST(FileBundleTests, pack_unpack_roundtrip)
{
    TempDir src;
    TempDir dst;
    std::string binary("a\0b\xff", 4);
    write_file(src.path() / "single.txt", "hello");
    write_file(src.path() / "folder" / "inner.bin", binary);
    write_file(src.path() / "folder" / "sub" / "deep.txt", "deep");
    fs::create_directories(src.path() / "folder" / "empty");

    std::string bundle;
    ASSERT_TRUE(FileBundle::pack({ src.path() / "single.txt", src.path() / "folder" }, bundle));

    auto top = FileBundle::unpack_to(bundle, dst.path());
    ASSERT_EQ(2u, top.size());
    EXPECT_EQ(dst.path() / "single.txt", top[0]);
    EXPECT_EQ(dst.path() / "folder", top[1]);

    EXPECT_EQ("hello", read_file(dst.path() / "single.txt"));
    EXPECT_EQ(binary, read_file(dst.path() / "folder" / "inner.bin"));
    EXPECT_EQ("deep", read_file(dst.path() / "folder" / "sub" / "deep.txt"));
    EXPECT_TRUE(fs::is_directory(dst.path() / "folder" / "empty"));
}

TEST(FileBundleTests, unpack_rejects_truncated_data)
{
    TempDir src;
    TempDir dst;
    write_file(src.path() / "file.txt", "some contents");

    std::string bundle;
    ASSERT_TRUE(FileBundle::pack({ src.path() / "file.txt" }, bundle));
    bundle.resize(bundle.size() - 3);

    EXPECT_TRUE(FileBundle::unpack_to(bundle, dst.path()).empty());
    EXPECT_TRUE(FileBundle::unpack_to("garbage", dst.path()).empty());
    EXPECT_TRUE(FileBundle::unpack_to("", dst.path()).empty());
}

TEST(FileBundleTests, unpack_rejects_path_traversal)
{
    TempDir dst;
    std::string rel = "../escaped.txt";
    std::string bundle = "IGFB";
    write_u32(bundle, 1);
    write_u32(bundle, 1);
    bundle += static_cast<char>(1);
    write_u32(bundle, static_cast<std::uint32_t>(rel.size()));
    bundle += rel;
    bundle += std::string(7, '\0');
    bundle += static_cast<char>(1);
    bundle += "x";

    EXPECT_TRUE(FileBundle::unpack_to(bundle, dst.path() / "inner").empty());
    EXPECT_FALSE(fs::exists(dst.path() / "escaped.txt"));
}

TEST(FileBundleTests, safe_relative_paths)
{
    EXPECT_TRUE(FileBundle::is_safe_relative_path("file.txt"));
    EXPECT_TRUE(FileBundle::is_safe_relative_path("folder/sub/file.txt"));
    EXPECT_TRUE(FileBundle::is_safe_relative_path("..hidden"));

    EXPECT_FALSE(FileBundle::is_safe_relative_path(""));
    EXPECT_FALSE(FileBundle::is_safe_relative_path("/etc/passwd"));
    EXPECT_FALSE(FileBundle::is_safe_relative_path(".."));
    EXPECT_FALSE(FileBundle::is_safe_relative_path("a/../../b"));
    EXPECT_FALSE(FileBundle::is_safe_relative_path("a//b"));
    EXPECT_FALSE(FileBundle::is_safe_relative_path("a/./b"));
    EXPECT_FALSE(FileBundle::is_safe_relative_path("C:/Windows"));
    EXPECT_FALSE(FileBundle::is_safe_relative_path("a\..\b"));
    EXPECT_FALSE(FileBundle::is_safe_relative_path("file.txt:stream"));
}

TEST(FileBundleTests, uri_list_roundtrip)
{
#ifdef _WIN32
    std::vector<fs::path> paths = { fs::u8path("C:/Users/me/My File.txt"),
                                    fs::u8path("C:/data/caf\xc3\xa9") };
    EXPECT_EQ("file:///C:/Users/me/My%20File.txt\r\nfile:///C:/data/caf%C3%A9\r\n",
              FileBundle::to_uri_list(paths));
#else
    std::vector<fs::path> paths = { fs::u8path("/home/me/My File.txt"),
                                    fs::u8path("/tmp/caf\xc3\xa9") };
    EXPECT_EQ("file:///home/me/My%20File.txt\r\nfile:///tmp/caf%C3%A9\r\n",
              FileBundle::to_uri_list(paths));
#endif
    EXPECT_EQ(paths, FileBundle::from_uri_list(FileBundle::to_uri_list(paths)));
}

TEST(FileBundleTests, uri_list_parsing)
{
    auto paths = FileBundle::from_uri_list(
        "# comment\nfile://localhost/tmp/a\nhttps://example.com/x\nfile:///tmp/b%2Fc\n");
    ASSERT_EQ(2u, paths.size());
    EXPECT_EQ(fs::u8path("/tmp/a"), paths[0]);
    EXPECT_EQ(fs::u8path("/tmp/b/c"), paths[1]);
}

} // namespace inputleap
