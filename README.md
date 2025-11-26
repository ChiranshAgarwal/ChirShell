# ChirShell

ChirShell is a lightweight, Unix-inspired shell implemented in modern C++20. It focuses on safe piping, job control, signal handling, and a colorful prompt while keeping the codebase approachable for experimentation and learning.

## Features
- Interactive REPL loop with graceful exit via `exit` or `Ctrl+D`
- Parser supporting pipes, quoting, escapes, background jobs, and repeated whitespace
- Built-in commands: `cd`, `pwd`, `echo`, `env`, `export`, `unset`, `history`, `jobs`, `fg`, `bg`, `kill`, and `exit`
- Safe multi-stage pipelines built with `std::vector<std::array<int,2>>`
- Background job management with a dedicated reaper thread plus `SIGCHLD` handling
- Foreground control and signal propagation that ignores `Ctrl+C` inside the shell but forwards it to running jobs
- Colorized prompt in the form `<user>@<host>:<cwd>$`

## Building
- **POSIX/MinGW:** `make`
- **PowerShell (Windows):** `./build.ps1` (use `./build.ps1 -Clean` to remove artifacts)

Both methods place the `chirshell` binary in the project root.

## Usage
```
./chirshell
chiransh@archlinux:~/projects/ChirShell$ echo hello | tr a-z A-Z
HELLO
chiransh@archlinux:~/projects/ChirShell$ sleep 5 &
[1] 4242 sleep 5 &
chiransh@archlinux:~/projects/ChirShell$ jobs
[1] Running sleep 5 &
chiransh@archlinux:~/projects/ChirShell$ fg %1
```

## Future Improvements
- Advanced redirection (`>`, `<`, `2>`) and here-doc support
- Command completion and line editing via GNU Readline or linenoise
- Persistent history file with timestamp metadata
- Job notifications integrated with prompt redraw to avoid layout glitches
- Configurable prompt segments/themes

