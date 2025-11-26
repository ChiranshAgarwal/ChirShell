/**
 * @file builtins.hpp
 * @brief Shell built-in command declarations.
 */
#pragma once

#include "parser.hpp"
#include <string>

namespace builtins {

bool is_builtin(const parser::Command& cmd);
int run(const parser::Command& cmd, bool& should_exit);

} // namespace builtins

