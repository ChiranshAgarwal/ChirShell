#include "executor.hpp"

#include "builtins.hpp"
#include "jobs.hpp"

#include <array>
#include <csignal>
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
        tcsetpgrp(shell_terminal, pgid);
    }
}

void reclaim_terminal() {
    if (terminal_attached) {
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
            const std::string& raw_command,
            builtins::History& history,
            ExecutionContext& ctx) {
    if (plan.pipeline.empty()) {
        return 0;
    }

    if (plan.pipeline.size() == 1 && builtins::is_builtin(plan.pipeline[0])) {
        ctx.last_status = builtins::run(plan.pipeline[0], ctx.should_exit, history);
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
            if (!plan.background) {
                struct sigaction action {};
                action.sa_handler = SIG_DFL;
                sigemptyset(&action.sa_mask);
                action.sa_flags = 0;
                sigaction(SIGINT, &action, nullptr);
            }

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

    if (plan.background) {
        const int job_id = jobs::add_job(pgid, raw_command, true, static_cast<int>(stages));
        std::cout << '[' << job_id << "] " << pgid << " " << raw_command << '\n';
        ctx.last_status = 0;
        return 0;
    }

    attach_terminal(pgid);
    int status = 0;
    pid_t waited = 0;
    do {
        waited = ::waitpid(-pgid, &status, WUNTRACED);
    } while (waited > 0 && !WIFEXITED(status) && !WIFSIGNALED(status));

    reclaim_terminal();
    jobs::mark_job_finished(pgid, status);
    ctx.last_status = status;
    return status;
}

} // namespace executor

