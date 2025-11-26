#include "builtins.hpp"
#include "executor.hpp"
#include "parser.hpp"
#include "prompt.hpp"
#include "signals.hpp"
#include "jobs.hpp"

#include <cerrno>
#include <csignal>
#include <iostream>
#include <string>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

int main() {
    const pid_t shell_pid = ::getpid();
    if (::setpgid(shell_pid, shell_pid) < 0 && errno != EPERM) {
        perror("setpgid");
        return 1;
    }
    if (::isatty(STDIN_FILENO)) {
        ::tcsetpgrp(STDIN_FILENO, shell_pid);
    }

    executor::set_shell_pgid(shell_pid);
    jobs::initialize(shell_pid);
    signals::install_handlers();

    builtins::History history;
    executor::ExecutionContext ctx;

    while (!ctx.should_exit) {
        const std::string ps1 = prompt::build_prompt();
        std::cout << ps1;
        std::cout.flush();

        std::string line;
        if (!std::getline(std::cin, line)) {
            std::cout << '\n';
            break;
        }

        if (line.empty()) {
            continue;
        }

        history.push_back(line);
        parser::ParseResult plan = parser::parse_line(line);

        if (plan.pipeline.empty()) {
            continue;
        }

        executor::execute(plan, line, history, ctx);
    }

    jobs::shutdown();
    return 0;
}

