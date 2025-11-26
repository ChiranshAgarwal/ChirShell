# ChirShell Feature Guide

This document explains each major ChirShell feature, how it is implemented, and where to look in the codebase for reference.

## Interactive Shell Loop & Prompt
The entry point configures the shell process group, installs subsystems, and runs a classic read–eval–print loop (REPL). Each iteration builds the colorful prompt, reads a command line, records it in history, parses it, and executes the resulting pipeline.

```33:56:src/main.cpp
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
```

The vibrant `user@host:cwd` prompt is assembled in `prompt::build_prompt()`, which resolves user/host info and colors each segment with ANSI escape codes.

```49:57:src/prompt.cpp
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
```

## Parser: Quotes, Escapes, Pipes, Background Jobs
`parser::parse_line` tokenizes the raw line into a sequence of `Command` objects, respecting quoting, escaping, and special characters (`|`, `&`). A background pipeline is detected by a trailing `&`.

```26:93:src/parser.cpp
ParseResult parse_line(const std::string& line) {
    ParseResult result;
    Command current;
    std::string token;
    bool in_single = false;
    bool in_double = false;
    bool escape = false;

    for (const char ch : line) {
        if (escape) {
            token.push_back(ch);
            escape = false;
            continue;
        }

        if (ch == '\\') {
            escape = true;
            continue;
        }

        if (ch == '"' && !in_single) {
            in_double = !in_double;
            continue;
        }

        if (ch == '\'' && !in_double) {
            in_single = !in_single;
            continue;
        }

        if (!in_single && !in_double) {
            if (ch == '|') {
                flush_token(token, current);
                flush_command(current, result);
                continue;
            }

            if (ch == '&') {
                flush_token(token, current);
                flush_command(current, result);
                result.background = true;
                continue;
            }

            if (std::isspace(static_cast<unsigned char>(ch))) {
                flush_token(token, current);
                continue;
            }
        }

        token.push_back(ch);
    }

    if (escape) {
        token.push_back('\\');
    }

    flush_token(token, current);
    flush_command(current, result);

    // Remove any accidentally empty commands.
    result.pipeline.erase(
        std::remove_if(result.pipeline.begin(),
                       result.pipeline.end(),
                       [](const Command& cmd) { return cmd.args.empty(); }),
        result.pipeline.end());

    return result;
}
```

## Built-in Commands & History
Built-ins run inside the shell process and touch OS state directly (e.g., `cd`, environment manipulation, job control helpers). `builtins::run` dispatches to concrete handlers and toggles the REPL exit flag when `exit` is invoked.

```202:258:src/builtins.cpp
bool is_builtin(const parser::Command& cmd) {
    const auto name = command_name(cmd);
    if (name.empty()) {
        return false;
    }
    static const std::vector<std::string_view> builtins = {
        "cd",    "pwd",  "echo",  "env",  "export",
        "unset", "exit", "history", "jobs", "fg",
        "bg",    "kill",
    };
    return std::find(builtins.begin(), builtins.end(), name) != builtins.end();
}

int run(const parser::Command& cmd, bool& should_exit, History& history) {
    const auto name = command_name(cmd);
    if (name.empty()) {
        return 0;
    }

    if (name == "cd") {
        return builtin_cd(cmd);
    }
    if (name == "pwd") {
        return builtin_pwd();
    }
    if (name == "echo") {
        return builtin_echo(cmd);
    }
    if (name == "env") {
        return builtin_env();
    }
    if (name == "export") {
        return builtin_export(cmd);
    }
    if (name == "unset") {
        return builtin_unset(cmd);
    }
    if (name == "exit") {
        should_exit = true;
        return 0;
    }
    if (name == "history") {
        return builtin_history(history);
    }
    if (name == "jobs") {
        return builtin_jobs();
    }
    if (name == "fg") {
        return builtin_fg(cmd);
    }
    if (name == "bg") {
        return builtin_bg(cmd);
    }
    if (name == "kill") {
        return builtin_kill(cmd);
    }
    return 0;
}
```

## Pipeline Execution, Fork/Exec, Foreground Control
`executor::execute` handles both pure built-ins and external command pipelines. It allocates `std::vector<std::array<int,2>>` pipes, forks each stage into the same process group, wires stdin/stdout, and either waits (foreground) or registers a background job.

```73:179:src/executor.cpp
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
```

## Background Jobs, Reaper Thread, and Job Control
The job subsystem tracks every background pipeline, provides `jobs`, `fg`, `bg`, and `kill` helpers, and owns a dedicated reaper thread that periodically calls `waitpid(-1, WNOHANG)` or reacts immediately when the `SIGCHLD` handler toggles a flag.

```17:245:src/jobs.cpp
std::vector<Job> job_table;
std::mutex job_mutex;
std::atomic<bool> running{false};
std::thread reaper;
pid_t shell_pgid = 0;
const int shell_terminal = STDIN_FILENO;
bool terminal_attached = false;
int next_job_id = 1;
volatile sig_atomic_t sigchld_flag = 0;
...
void reap_once() {
    int status = 0;
    pid_t pid = 0;
    while ((pid = ::waitpid(-1, &status, WNOHANG)) > 0) {
        handle_child_exit(pid, status);
    }
}

void reaper_loop() {
    while (running.load()) {
        if (sigchld_flag) {
            sigchld_flag = 0;
            reap_once();
        } else {
            reap_once();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    reap_once();
}
...
int add_job(pid_t pgid, const std::string& command, bool background, int process_count) {
    std::lock_guard<std::mutex> lock(job_mutex);
    Job job;
    job.id = next_job_id++;
    job.pgid = pgid;
    job.command = command;
    job.running = true;
    job.background = background;
    job.processes = process_count;
    job_table.push_back(job);
    return job.id;
}
...
bool bring_job_foreground(int id) {
    std::unique_lock<std::mutex> lock(job_mutex);
    Job* job = find_job_locked(id);
    if (!job) {
        return false;
    }
    job->background = false;
    job->running = true;
    if (job->processes <= 0) {
        job->processes = 1;
    }
    const pid_t pgid = job->pgid;
    lock.unlock();

    give_terminal_to(pgid);
    if (::kill(-pgid, SIGCONT) < 0) {
        perror("fg");
        reclaim_terminal();
        return false;
    }

    int status = 0;
    pid_t waited = 0;
    do {
        waited = ::waitpid(-pgid, &status, WUNTRACED);
    } while (waited > 0 && !WIFEXITED(status) && !WIFSIGNALED(status));

    reclaim_terminal();
    mark_job_finished(pgid, status);
    return true;
}
```

## Signal Handling
Signals are configured so the shell ignores Ctrl+C itself but still reports a newline to keep the prompt tidy. `SIGCHLD` simply flips a flag that wakes the reaper thread to clean up zombies.

```11:39:src/signals.cpp
void sigint_handler(int) {
    std::cout << '\n';
    std::cout.flush();
}

void sigchld_handler(int) {
    jobs::notify_sigchld();
}

void install_handler(int sig, void (*fn)(int)) {
    struct sigaction action {};
    action.sa_handler = fn;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sigaction(sig, &action, nullptr);
}
...
void install_handlers() {
    install_handler(SIGINT, sigint_handler);
    install_handler(SIGCHLD, sigchld_handler);
    install_handler(SIGTSTP, SIG_IGN);
}
```

## Build Options
Two portable build entry points are provided: a POSIX-style `Makefile` and a PowerShell script for Windows environments. Both compile the same source list with the required C++20 and pthread flags.

```1:32:Makefile
CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -pthread -Iinclude
LDFLAGS := -pthread

SRCS := \
	src/main.cpp \
	src/parser.cpp \
	src/executor.cpp \
	src/builtins.cpp \
	src/jobs.cpp \
	src/signals.cpp \
	src/prompt.cpp

OBJS := $(SRCS:.cpp=.o)

TARGET := chirshell

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJS) $(TARGET)
```

```1:41:build.ps1
param(
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$target = Join-Path $PSScriptRoot "chirshell"

if ($Clean) {
    Write-Host "Cleaning build artifacts..."
    if (Test-Path "$target.exe") { Remove-Item "$target.exe" -Force }
    if (Test-Path $target) { Remove-Item $target -Force }
    Get-ChildItem -Path $PSScriptRoot -Filter "*.o" | Remove-Item -Force -ErrorAction SilentlyContinue
    return
}

$sources = @(
    "src/main.cpp",
    "src/parser.cpp",
    "src/executor.cpp",
    "src/builtins.cpp",
    "src/jobs.cpp",
    "src/signals.cpp",
    "src/prompt.cpp"
) | ForEach-Object { Join-Path $PSScriptRoot $_ }

$includeDir = Join-Path $PSScriptRoot "include"
$argv = @(
    "g++",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-pthread",
    "-I", $includeDir
) + $sources + @("-o", $target, "-pthread")

Write-Host "Compiling chirshell..."
Write-Host ($argv -join " ")

$process = Start-Process -FilePath $argv[0] -ArgumentList $argv[1..($argv.Length - 1)] -NoNewWindow -PassThru -Wait
if ($process.ExitCode -ne 0) {
    throw "Compilation failed with exit code $($process.ExitCode)"
}

Write-Host "Build complete: $target"
```


