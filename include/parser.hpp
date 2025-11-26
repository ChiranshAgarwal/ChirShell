/**
 * @file parser.hpp
 * @brief Parsing utilities for turning user input into executable commands.
 */
#pragma once

#include <string>
#include <vector>

namespace parser {

struct Command {
    std::vector<std::string> args;
};

struct ParseResult {
    std::vector<Command> pipeline;
    bool background = false;
};

ParseResult parse_line(const std::string& line);

} // namespace parser



