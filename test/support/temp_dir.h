// Portable replacement for mkdtemp() in the native tests: makes a new,
// uniquely named directory under the system temp directory ($TMPDIR on POSIX,
// %TEMP% on Windows) and returns its path, or "" on failure.
#pragma once

#include <filesystem>
#include <random>
#include <string>
#include <system_error>

inline std::string makeTestTempDir(const std::string &prefix)
{
    std::error_code ec;
    const std::filesystem::path base = std::filesystem::temp_directory_path(ec);
    if (ec)
        return {};
    std::random_device rd;
    std::mt19937_64 rng((uint64_t(rd()) << 32) ^ rd());
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const std::filesystem::path dir =
            base / (prefix + "_" + std::to_string(rng() % 1000000000ull));
        if (std::filesystem::create_directory(dir, ec) && !ec)
            return dir.string();
    }
    return {};
}

// A path for [name] in a directory of the system temp directory made once per
// process, or "" if it cannot be made. Use it instead of $TMPDIR or /tmp,
// which do not exist on Windows.
inline std::string testTempPath(const std::string &name)
{
    static const std::string dir = makeTestTempDir("soloud_test");
    return dir.empty() ? std::string() : dir + "/" + name;
}
