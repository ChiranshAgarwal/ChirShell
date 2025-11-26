# ChirShell Feature Guide

This document explains each major ChirShell feature, how it is implemented, and where to look in the codebase for reference.

## Interactive Shell Loop & Prompt
The entry point configures the shell process group, installs minimal signal handling, and runs a classic read–eval–print loop (REPL). Each iteration builds the colorful prompt, reads a command line, parses it, and executes the resulting pipeline.

```43:65:src/main.cpp
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

        executor::execute(plan, ctx);
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

## Parser: Quotes, Escapes, Pipes
`parser::parse_line` tokenizes the raw line into a sequence of `Command` objects, respecting quoting, escaping, and special characters (`|`). Pipes are detected by the `|` character.

```26:94:src/parser.cpp
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

## Built-in Commands
Built-ins run inside the shell process and are necessary for terminal-specific operations. Currently, only `exit` is implemented as a builtin, which sets the `should_exit` flag to terminate the REPL loop.

```29:50:src/builtins.cpp
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
```

## Pipeline Execution & Fork/Exec
`executor::execute` handles both built-ins and external command pipelines. It allocates `std::vector<std::array<int,2>>` pipes, forks each stage into the same process group, wires stdin/stdout, and waits for all processes to complete.

```75:188:src/executor.cpp
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
```

External commands are executed via `execvp()`, which searches the `PATH` environment variable to locate executables. All commands run in the foreground, and the shell waits for them to complete before showing the next prompt.

## Signal Handling
Minimal signal handling is implemented for interactive use. The shell ignores `SIGTSTP` (Ctrl+Z) and `SIGTTOU` to prevent itself from being stopped during terminal operations. `SIGINT` (Ctrl+C) is handled to print a newline and continue the REPL loop.

```32:36:src/main.cpp
    // Minimal signal handling for interactive use
    // Ignore SIGTSTP (Ctrl+Z) so shell doesn't get stopped
    ::signal(SIGTSTP, SIG_IGN);
    // Ignore SIGTTOU (sent when changing terminal foreground process group)
    ::signal(SIGTTOU, SIG_IGN);
    // Handle SIGINT (Ctrl+C) - just print newline and continue
    ::signal(SIGINT, sigint_handler);
```

## Build Options
A POSIX-style `Makefile` is provided that compiles the source files with C++20 standard and places object files in `build/obj/` and the binary in `bin/`.

```1:37:Makefile
CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Iinclude
LDFLAGS :=

# Directories
BUILD_DIR := build/obj
BIN_DIR := bin

SRCS := \
	src/main.cpp \
	src/parser.cpp \
	src/executor.cpp \
	src/builtins.cpp \
	src/prompt.cpp

# Object files go to build/obj/
OBJS := $(SRCS:src/%.cpp=$(BUILD_DIR)/%.o)

TARGET := $(BIN_DIR)/chirshell

.PHONY: all clean directories

all: directories $(TARGET)

directories:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

$(TARGET): $(OBJS) | directories
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: src/%.cpp | directories
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
```

## Current Limitations
- No background job execution
- No job control (fg, bg, jobs commands)
- No history tracking
- No variable expansion
- No redirection operators (>, <, >>)
- Only `exit` builtin command
- All commands run in foreground only
