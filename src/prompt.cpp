#include "prompt.hpp"

#include <pwd.h>
#include <unistd.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace {

constexpr const char* GREEN = "\033[32m";
constexpr const char* BLUE = "\033[34m";
constexpr const char* YELLOW = "\033[33m";
constexpr const char* WHITE = "\033[0m";

std::string current_user() {
    if (const char* user = std::getenv("USER")) {
        return user;
    }
    if (passwd* pwd = getpwuid(getuid())) {
        return pwd->pw_name;
    }
    return "unknown";
}

std::string current_host() {
    std::array<char, 256> buffer{};
    if (gethostname(buffer.data(), buffer.size()) == 0) {
        return buffer.data();
    }
    return "host";
}

std::string current_dir() {
    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    if (ec) {
        return "?";
    }
    return cwd.string();
}

} // namespace

namespace prompt {

std::string build_prompt() {
    std::ostringstream oss;
    oss << GREEN << current_user()
        << WHITE << '@'
        << BLUE << current_host()
        << WHITE << ':'
        << YELLOW << current_dir()
        << WHITE << "$ ";
    return oss.str();
}

} // namespace prompt



