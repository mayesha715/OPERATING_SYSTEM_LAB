# Enhanced Custom Shell

## Assignment
Operating Systems Lab 1 — Building an Enhanced Custom Shell

## Files
- `main.c` — complete custom shell implementation.
- `build_and_test.sh` — compiles the shell with GCC and runs automated tests.
- `README.md` — compilation, system-call mapping, features, and tests.

## Compilation

```bash
gcc -Wall -o myshell main.c
```

The included test script uses the same required compilation form:

```bash
./build_and_test.sh
```

## Running

```bash
./myshell
```

The shell uses the directory containing the `myshell` executable as its **virtual shell root**. That directory is displayed as `/` by `pwd` and in the prompt. `cd ..`, `cd ../..`, and other path traversal attempts cannot leave this root. The normal `cd` builtin can move between directories inside the root.

Example, if the real directory is `/home/user/custom_shell`:

```text
Real path: /home/user/custom_shell        Shell path: /
Real path: /home/user/custom_shell/docs   Shell path: /docs
```

`cd` with no argument returns to the shell root.

## Required Commands and System Calls

| Command | Implementation / System Calls |
|---|---|
| `pwd` | `getcwd()`; output is converted to a path relative to the virtual shell root |
| `ls` | `opendir()`, `readdir()`, `closedir()`, `stat()` |
| `cd` | Builtin in parent process; `chdir()`, `getcwd()`, `realpath()`; root boundary enforced |
| `whoami` | `getuid()`, `getpwuid()` |
| `uname` | `uname()` |
| `mkdir` | `mkdir()` |
| `touch` | `open()` with `O_CREAT`; `utimensat()` for timestamp update |
| `rm` | `unlink()` for files and `rmdir()` for empty directories |
| `cp` | `open()`, `read()`, `write()`, `close()` |
| `mv` | `rename()` |
| `cat` | `open()`, `read()`, `write()`, `close()` |
| `echo` | Builtin; environment variables such as `$PATH` are expanded using `getenv()` |
| `uptime` | Reads `/proc/uptime` and `/proc/loadavg` with file APIs |
| `meminfo` | Reads and parses `/proc/meminfo` |
| `help` | Builtin; prints shell command documentation |
| `exit` | Builtin; exits the shell with a supplied status from 0–255 |

No `system()` call is used. Manual utilities are implemented directly rather than invoking standard shell binaries.

## Process Management

External commands use:
- `fork()` to create a child process.
- `execvp()` to execute the requested external program.
- `waitpid()` for foreground commands.
- `waitpid(..., WNOHANG)` to reap completed background processes without blocking.

The required builtins `cd`, `echo`, `help`, and `exit` are evaluated in the main parent shell process.

## Command Parsing

The parser supports multiple command-line arguments, for example:

```bash
ls -l /usr/bin
cp source.txt destination.txt
```

It recognizes:
- normal arguments
- quoted strings using single or double quotes
- `|`
- `<`
- `>`
- `>>`
- `&`

Invalid commands, missing arguments, invalid operators, and file-access errors produce clear diagnostics using `perror()`, `strerror()`, or explicit error messages.

## Advanced Features

### 1. Command Piping

One pipe is supported:

```bash
ls -l | wc -l
cat file.txt | grep hello
```

Implemented with `pipe()`, `dup2()`, `fork()`, `execvp()`, and `waitpid()`.

### 2. I/O Redirection

Overwrite output:

```bash
echo Hello > output.txt
```

Append output:

```bash
echo World >> output.txt
```

Redirect input:

```bash
cat < output.txt
```

Implemented using `open()` and `dup2()`.

### 3. Background Execution

Append `&` to execute without waiting:

```bash
sleep 5 &
```

The parent immediately displays the next prompt and later reports/reaps completed background processes.

### 4. Command History

The shell maintains up to 100 commands in memory:

```bash
history
```

## Useful Examples

```bash
pwd
ls
ls -l
cd docs
cd ..
cd
whoami
uname -a
mkdir test
touch test/a.txt
echo Hello > test/a.txt
cat test/a.txt
echo World >> test/a.txt
cat < test/a.txt
cp test/a.txt test/b.txt
mv test/b.txt test/c.txt
rm test/c.txt
rmdir test
uptime
meminfo
ls | wc -l
sleep 3 &
history
help
exit
```

## Automated Tests

Run:

```bash
./build_and_test.sh
```

The script tests:
- compilation
- `pwd`
- `echo`
- `whoami` and `uname`
- directory/file operations
- output and input redirection
- piping
- history
- background execution
- virtual shell root behavior and protection against `cd ..` escaping the root


