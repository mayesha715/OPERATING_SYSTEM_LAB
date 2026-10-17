#!/bin/bash
set -e

echo "[1/3] Compiling..."
gcc -Wall -Wextra -std=c11 -o myshell main.c
echo "Compilation successful."

echo "[2/3] Running automated tests..."
rm -rf shell_test_tmp
mkdir shell_test_tmp

# Basic commands
printf 'pwd\nexit\n' | ./myshell > test_pwd.out
grep -q 'myshell:/\$ /' test_pwd.out && echo "PASS: pwd" || { echo "FAIL: pwd"; exit 1; }

printf 'echo Hello World\nexit\n' | ./myshell > test_echo.out
grep -q 'myshell:/\$ Hello World' test_echo.out && echo "PASS: echo" || { echo "FAIL: echo"; exit 1; }

printf 'whoami\nuname -a\nexit\n' | ./myshell > test_info.out
grep -q . test_info.out && echo "PASS: whoami/uname" || { echo "FAIL: whoami/uname"; exit 1; }

# File operations and redirection
printf 'mkdir shell_test_tmp/sub\ncd shell_test_tmp\ntouch a.txt\necho Hello > a.txt\necho World >> a.txt\ncat a.txt\ncd ..\nrm shell_test_tmp/a.txt\nrmdir shell_test_tmp/sub\nrmdir shell_test_tmp\nexit\n' | ./myshell > test_files.out
grep -q 'Hello' test_files.out && grep -q 'World' test_files.out && echo "PASS: file operations/redirection" || { echo "FAIL: file operations/redirection"; exit 1; }

# Input redirection and pipe
printf 'echo one > pipe_input.txt\ncat < pipe_input.txt | wc -l\nrm pipe_input.txt\nexit\n' | ./myshell > test_pipe.out
grep -q ' 1$' test_pipe.out && echo "PASS: pipe and input redirection" || { echo "FAIL: pipe and input redirection"; exit 1; }

# History
printf 'pwd\necho test\nhistory\nexit\n' | ./myshell > test_history.out
grep -q '1  pwd' test_history.out && grep -q '2  echo test' test_history.out && echo "PASS: history" || { echo "FAIL: history"; exit 1; }

# Background execution
printf 'sleep 1 &\necho background\nexit\n' | ./myshell > test_bg.out
grep -q 'background' test_bg.out && grep -q '\[background pid' test_bg.out && echo "PASS: background execution" || { echo "FAIL: background execution"; exit 1; }

# Virtual root protection
mkdir -p shell_test_tmp/rootcheck
printf 'pwd\ncd ..\npwd\ncd shell_test_tmp/rootcheck\npwd\ncd ../..\npwd\nexit\n' | ./myshell > test_root.out
# Initial root is the directory containing myshell, represented as /
grep -q 'myshell:/\$ /' test_root.out && grep -q 'myshell:/shell_test_tmp/rootcheck\$ /shell_test_tmp/rootcheck' test_root.out && echo "PASS: virtual shell root and cd restriction" || { echo "FAIL: virtual shell root and cd restriction"; exit 1; }
rm -rf shell_test_tmp

rm -f test_*.out

echo "All automated tests passed."
