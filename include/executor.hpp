/**
 * @file executor.hpp
 * @brief Execution logic for pipelines and process control.
 */
#pragma once

#include "builtins.hpp"
#include "parser.hpp"
#include <string>
#include <vector>

namespace executor {

struct ExecutionContext {
    bool should_exit = false;
    int last_status = 0;
};

int execute(const parser::ParseResult& plan,
            const std::string& raw_command,
            builtins::History& history,
            ExecutionContext& ctx);

void set_shell_pgid(pid_t pgid);
pid_t get_shell_pgid();

} // namespace executor

