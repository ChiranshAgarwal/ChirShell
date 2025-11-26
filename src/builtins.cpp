#include "builtins.hpp"

#include <algorithm>
#include <iostream>
#include <string_view>
#include <vector>

namespace builtins {
namespace {

std::string_view command_name(const parser::Command& cmd) {
    if (cmd.args.empty()) {
        return {};
    }
    return cmd.args.front();
}

} // namespace

bool is_builtin(const parser::Command& cmd) {
    const auto name = command_name(cmd);
    if (name.empty()) {
        return false;
    }
    static const std::vector<std::string_view> builtins = {
        "exit",
    };
    return std::find(builtins.begin(), builtins.end(), name) != builtins.end();
}

int run(const parser::Command& cmd, bool& should_exit) {
    const auto name = command_name(cmd);
    if (name.empty()) {
        return 0;
    }

    if (name == "exit") {
        should_exit = true;
        return 0;
    }
    
    return 0;
}

} // namespace builtins

