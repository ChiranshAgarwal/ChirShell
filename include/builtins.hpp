/**
 * @file builtins.hpp
 * @brief Shell built-in command declarations.
 */
#pragma once

#include "parser.hpp"
#include <string>
#include <vector>

namespace builtins {

using History = std::vector<std::string>;

bool is_builtin(const parser::Command& cmd);
int run(const parser::Command& cmd, bool& should_exit, History& history);

} // namespace builtins



