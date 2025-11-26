#include "executor.hpp"

#include "builtins.hpp"

#include <array>
#include <cerrno>
#include <iostream>
#include <optional>
#include <string>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

namespace executor {
namespace {

pid_t shell_pgid = 0;
const int shell_terminal = STDIN_FILENO;
bool terminal_attached = false;

void close_pipe(std::array<int, 2>& pipefd) {
    for (int fd : pipefd) {
        if (fd >= 0) {
            ::close(fd);
        }
    }
    pipefd = {-1, -1};
}

void close_pipes(std::vector<std::array<int, 2>>& pipes) {
    for (auto& pipefd : pipes) {
        close_pipe(pipefd);
    }
}

std::vector<char*> build_argv(const parser::Command& cmd,
                              std::vector<std::string>& storage) {
    storage = cmd.args;
    std::vector<char*> argv;
    argv.reserve(storage.size() + 1);
    for (auto& arg : storage) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    return argv;
}

void attach_terminal(pid_t pgid) {
    if (terminal_attached) {
        if (tcsetpgrp(shell_terminal, pgid) < 0 && errno != ENOTTY) {
            // Ignore errors - process group might not exist yet or already gone
        }
    }
}

void reclaim_terminal() {
    if (terminal_attached) {
        // Always try to reclaim terminal, ignore errors
        tcsetpgrp(shell_terminal, shell_pgid);
    }
}

} // namespace

void set_shell_pgid(pid_t pgid) {
    shell_pgid = pgid;
    terminal_attached = ::isatty(shell_terminal);
}

pid_t get_shell_pgid() {
    return shell_pgid;
}

int execute(const parser::ParseResult& plan,
            ExecutionContext& ctx) {
    if (plan.pipeline.empty()) {
        return 0;
    }

    // Handle builtin commands (must be single command, no pipes)
    if (plan.pipeline.size() == 1 && builtins::is_builtin(plan.pipeline[0])) {
        ctx.last_status = builtins::run(plan.pipeline[0], ctx.should_exit);
        return ctx.last_status;
    }

    const std::size_t stages = plan.pipeline.size();
    std::vector<std::array<int, 2>> pipes(stages > 1 ? stages - 1 : 0, std::array<int, 2>{-1, -1});

    for (std::size_t i = 0; i + 1 < stages; ++i) {
        if (::pipe(pipes[i].data()) < 0) {
            perror("pipe");
            close_pipes(pipes);
            return 1;
        }
    }

    std::vector<pid_t> pids;
    pids.reserve(stages);
    pid_t pgid = 0;

    for (std::size_t i = 0; i < stages; ++i) {
        pid_t pid = ::fork();
        if (pid < 0) {
            perror("fork");
            close_pipes(pipes);
            return 1;
        }

        if (pid == 0) {
            // Child path.
            if (pgid == 0) {
                pgid = ::getpid();
            }
            ::setpgid(0, pgid);

            if (i > 0) {
                ::dup2(pipes[i - 1][0], STDIN_FILENO);
            }
            if (i + 1 < stages) {
                ::dup2(pipes[i][1], STDOUT_FILENO);
            }

            for (auto& pipefd : pipes) {
                close_pipe(pipefd);
            }

            std::vector<std::string> storage;
            auto argv = build_argv(plan.pipeline[i], storage);
            if (argv.empty() || !argv[0]) {
                std::cerr << "chirshell: empty command in pipeline\n";
                _exit(1);
            }
            ::execvp(argv[0], argv.data());
            perror("execvp");
            _exit(127);
        }

        // Parent path.
        if (pgid == 0) {
            pgid = pid;
        }
        ::setpgid(pid, pgid);

        if (i > 0) {
            close_pipe(pipes[i - 1]);
        }

        pids.push_back(pid);
    }

    if (!pipes.empty()) {
        close_pipe(pipes.back());
    }

    // Attach terminal to child process group
    attach_terminal(pgid);
    
    // Wait for all processes in the pipeline to complete
    int exit_code = 0;
    for (pid_t pid : pids) {
        int status = 0;
        pid_t waited = ::waitpid(pid, &status, 0);
        if (waited > 0) {
            if (WIFEXITED(status)) {
                exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                exit_code = 128 + WTERMSIG(status);  // Standard shell convention
            }
        } else if (waited < 0 && errno != ECHILD) {
            perror("waitpid");
        }
    }

    // Always reclaim terminal, even if there were errors
    reclaim_terminal();
    
    ctx.last_status = exit_code;
    return exit_code;
}

} // namespace executor

