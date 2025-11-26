#include "builtins.hpp"
#include "executor.hpp"
#include "parser.hpp"
#include "prompt.hpp"

#include <cerrno>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

namespace {
void sigint_handler(int) {
    std::cout << '\n';
    std::cout.flush();
}
} // namespace

int main() {
    const pid_t shell_pid = ::getpid();
    if (::setpgid(shell_pid, shell_pid) < 0 && errno != EPERM) {
        perror("setpgid");
        return 1;
    }
    if (::isatty(STDIN_FILENO)) {
        ::tcsetpgrp(STDIN_FILENO, shell_pid);
    }

    // Minimal signal handling for interactive use
    // Ignore SIGTSTP (Ctrl+Z) so shell doesn't get stopped
    ::signal(SIGTSTP, SIG_IGN);
    // Ignore SIGTTOU (sent when changing terminal foreground process group)
    ::signal(SIGTTOU, SIG_IGN);
    // Handle SIGINT (Ctrl+C) - just print newline and continue
    ::signal(SIGINT, sigint_handler);

    executor::set_shell_pgid(shell_pid);

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

        parser::ParseResult plan = parser::parse_line(line);

        if (plan.pipeline.empty()) {
            continue;
        }

        // Execute command - continue loop even if command fails
        try {
            executor::execute(plan, ctx);
        } catch (const std::exception& e) {
            std::cerr << "chirshell: error: " << e.what() << '\n';
        } catch (...) {
            std::cerr << "chirshell: unknown error occurred\n";
        }
    }

    return 0;
}

