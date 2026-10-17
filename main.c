#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <sys/utsname.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>

#define MAX_ARGS 128
#define BUFFER_SIZE 4096
#define MAX_HISTORY 100

static char *history[MAX_HISTORY];
static int history_count = 0;
static char shell_root[PATH_MAX];

static void print_errno(const char *command, const char *path);



static void print_help(void)
{
    printf("\n========== MAYESHA'S SHELL HELP ==========\n");
    printf("Standard file/directory commands:\n");
    printf("  pwd                     Print current working directory\n");
    printf("  ls [options] [dir]      List directory contents\n");
    printf("  cd [dir]                Change directory (builtin)\n");
    printf("  whoami                  Display current user\n");
    printf("  uname [-a]              Display system information\n");
    printf("\nFile/directory manipulation:\n");
    printf("  mkdir <dir> [mode]      Create directory (default mode 0755)\n");
    printf("  touch <file>...         Create/update file timestamp\n");
    printf("  rm <file/dir>...        Remove file or empty directory\n");
    printf("  cp <source> <dest>      Copy a file\n");
    printf("  mv <source> <dest>      Move/rename file or directory\n");
    printf("\nFile/process inspection:\n");
    printf("  cat <file>...            Display file contents\n");
    printf("  echo <text>...           Print text or environment variables\n");
    printf("  uptime                   Show uptime and load averages\n");
    printf("  meminfo                  Show memory statistics\n");
    printf("\nShell commands:\n");
    printf("  help                     Show this help menu\n");
    printf("  history                  Show command history\n");
    printf("  clear                    Clear terminal\n");
    printf("  exit [status]             Exit shell\n");
    printf("\nAdvanced features:\n");
    printf("  cmd1 | cmd2              Pipe stdout of cmd1 to stdin of cmd2\n");
    printf("  cmd > file               Redirect stdout (overwrite)\n");
    printf("  cmd >> file              Redirect stdout (append)\n");
    printf("  cmd < file               Redirect stdin from file\n");
    printf("  cmd &                    Run external command in background\n");
    printf("  history                  In-memory command history (up to 100)\n");
    printf("\nExamples:\n");
    printf("  ls -l | wc -l\n");
    printf("  ls > files.txt\n");
    printf("  cat < input.txt\n");
    printf("  echo hello >> log.txt\n");
    printf("  sleep 10 &\n");
    printf("===================================\n\n");
}

static void add_history(const char *command)
{
    if (command == NULL || command[0] == '\0')
        return;

    if (history_count == MAX_HISTORY)
    {
        free(history[0]);
        for (int i = 1; i < MAX_HISTORY; i++)
            history[i - 1] = history[i];
        history_count--;
    }

    history[history_count] = strdup(command);
    if (history[history_count] != NULL)
        history_count++;
}

static void print_history(void)
{
    for (int i = 0; i < history_count; i++)
        printf("%d  %s\n", i + 1, history[i]);
}

static void clear_history(void)
{
    for (int i = 0; i < history_count; i++)
        free(history[i]);
    history_count = 0;
}

static void print_errno(const char *command, const char *path)
{
    if (path != NULL)
        fprintf(stderr, "%s: %s: %s\n", command, path, strerror(errno));
    else
        fprintf(stderr, "%s: %s\n", command, strerror(errno));
}

static int parse_octal_mode(const char *text, mode_t *mode)
{
    char *end;
    long value;

    if (text == NULL || text[0] == '\0')
        return -1;

    errno = 0;
    value = strtol(text, &end, 8);

    if (errno != 0 || *end != '\0' || value < 0 || value > 07777)
        return -1;

    *mode = (mode_t)value;
    return 0;
}



static int get_virtual_path(char *out, size_t size)
{
    char cwd[PATH_MAX];

    if (getcwd(cwd, sizeof(cwd)) == NULL)
        return -1;

    size_t root_len = strlen(shell_root);

    if (strcmp(cwd, shell_root) == 0)
    {
        if (size < 2)
            return -1;
        strcpy(out, "/");
        return 0;
    }

    if (strncmp(cwd, shell_root, root_len) == 0 && cwd[root_len] == '/')
    {
        if (strlen(cwd + root_len) + 1 > size)
            return -1;
        strcpy(out, cwd + root_len);
        return 0;
    }

    if (size < 2)
        return -1;
    strcpy(out, "/");
    return 0;
}

static int path_is_inside_root(const char *path)
{
    size_t root_len = strlen(shell_root);
    return strcmp(path, shell_root) == 0 ||
           (strncmp(path, shell_root, root_len) == 0 && path[root_len] == '/');
}

static int build_cd_target(const char *argument, char *target, size_t size)
{
    char candidate[PATH_MAX];

    if (argument == NULL || argument[0] == '\0')
    {
        strncpy(target, shell_root, size - 1);
        target[size - 1] = '\0';
        return 0;
    }

    if (argument[0] == '/')
    {
       
        if (snprintf(candidate, sizeof(candidate), "%s%s", shell_root, argument) >=
            (int)sizeof(candidate))
        {
            fprintf(stderr, "cd: path too long\n");
            return -1;
        }
    }
    else
    {
        if (getcwd(candidate, sizeof(candidate)) == NULL)
        {
            perror("cd: getcwd");
            return -1;
        }

        size_t len = strlen(candidate);
        if (len + 1 + strlen(argument) + 1 > sizeof(candidate))
        {
            fprintf(stderr, "cd: path too long\n");
            return -1;
        }

        candidate[len] = '/';
        strcpy(candidate + len + 1, argument);
    }

    char resolved[PATH_MAX];
    if (realpath(candidate, resolved) == NULL)
    {
        print_errno("cd", argument);
        return -1;
    }

    if (!path_is_inside_root(resolved))
    {
        fprintf(stderr, "cd: cannot leave shell root directory\n");
        return -1;
    }

    strncpy(target, resolved, size - 1);
    target[size - 1] = '\0';
    return 0;
}

static int cmd_pwd(int argc, char **argv)
{
    (void)argv;

    if (argc != 1)
    {
        fprintf(stderr, "pwd: too many arguments\n");
        return 1;
    }

    char virtual_path[PATH_MAX];
    if (get_virtual_path(virtual_path, sizeof(virtual_path)) < 0)
    {
        perror("pwd");
        return 1;
    }

    printf("%s\n", virtual_path);
    return 0;
}

static void print_permissions(mode_t mode)
{
    char permissions[11] = "----------";

    if (S_ISDIR(mode)) permissions[0] = 'd';
    if (S_ISLNK(mode)) permissions[0] = 'l';
    if (mode & S_IRUSR) permissions[1] = 'r';
    if (mode & S_IWUSR) permissions[2] = 'w';
    if (mode & S_IXUSR) permissions[3] = 'x';
    if (mode & S_IRGRP) permissions[4] = 'r';
    if (mode & S_IWGRP) permissions[5] = 'w';
    if (mode & S_IXGRP) permissions[6] = 'x';
    if (mode & S_IROTH) permissions[7] = 'r';
    if (mode & S_IWOTH) permissions[8] = 'w';
    if (mode & S_IXOTH) permissions[9] = 'x';

    printf("%s", permissions);
}

static int list_directory(const char *path, int long_format)
{
    DIR *dir = opendir(path);
    if (dir == NULL)
    {
        print_errno("ls", path);
        return 1;
    }

    struct dirent *entry;
    int status = 0;

    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;

        if (!long_format)
        {
            printf("%s  ", entry->d_name);
            continue;
        }

        char fullpath[BUFFER_SIZE * 2];
        int n = snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(fullpath))
        {
            fprintf(stderr, "ls: path too long: %s\n", entry->d_name);
            status = 1;
            continue;
        }

        struct stat st;
        if (stat(fullpath, &st) < 0)
        {
            print_errno("ls", fullpath);
            status = 1;
            continue;
        }

        print_permissions(st.st_mode);
        printf(" %5lu %8ld %s\n",
               (unsigned long)st.st_nlink,
               (long)st.st_size,
               entry->d_name);
    }

    if (!long_format)
        printf("\n");

    if (closedir(dir) < 0)
    {
        perror("ls: closedir");
        status = 1;
    }

    return status;
}

static int cmd_ls(int argc, char **argv)
{
    int long_format = 0;
    const char *path = ".";

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-l") == 0)
            long_format = 1;
        else if (argv[i][0] == '-')
        {
            fprintf(stderr, "ls: unsupported option: %s\n", argv[i]);
            return 1;
        }
        else
        {
            if (path != NULL && strcmp(path, ".") != 0)
            {
                fprintf(stderr, "ls: multiple paths are not supported\n");
                return 1;
            }
            path = argv[i];
        }
    }

    struct stat st;
    if (stat(path, &st) < 0)
    {
        print_errno("ls", path);
        return 1;
    }

    if (!S_ISDIR(st.st_mode))
    {
        if (long_format)
        {
            print_permissions(st.st_mode);
            printf(" %5lu %8ld %s\n",
                   (unsigned long)st.st_nlink,
                   (long)st.st_size,
                   path);
        }
        else
            printf("%s\n", path);
        return 0;
    }

    return list_directory(path, long_format);
}

static int cmd_whoami(int argc, char **argv)
{
    (void)argv;

    if (argc != 1)
    {
        fprintf(stderr, "whoami: no arguments expected\n");
        return 1;
    }

    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);

    if (pw == NULL)
    {
        fprintf(stderr, "whoami: cannot determine username for UID %u\n",
                (unsigned)uid);
        return 1;
    }

    printf("%s\n", pw->pw_name);
    return 0;
}

static int cmd_uname(int argc, char **argv)
{
    int all = 0;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-a") == 0)
            all = 1;
        else
        {
            fprintf(stderr, "uname: unsupported option: %s\n", argv[i]);
            return 1;
        }
    }

    struct utsname info;
    if (uname(&info) < 0)
    {
        perror("uname");
        return 1;
    }

    if (all)
    {
        printf("%s %s %s %s %s\n",
               info.sysname, info.nodename, info.release,
               info.version, info.machine);
    }
    else
    {
        printf("%s\n", info.sysname);
    }

    return 0;
}

static int cmd_mkdir(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
    {
        fprintf(stderr, "Usage: mkdir <directory> [mode]\n");
        return 1;
    }

    mode_t mode = 0755;
    if (argc == 3 && parse_octal_mode(argv[2], &mode) != 0)
    {
        fprintf(stderr, "mkdir: invalid mode '%s' (use octal, e.g. 755)\n", argv[2]);
        return 1;
    }

    if (mkdir(argv[1], mode) < 0)
    {
        print_errno("mkdir", argv[1]);
        return 1;
    }

    return 0;
}

static int cmd_touch(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: touch <file>...\n");
        return 1;
    }

    int status = 0;

    for (int i = 1; i < argc; i++)
    {
        int fd = open(argv[i], O_WRONLY | O_CREAT, 0666);
        if (fd < 0)
        {
            print_errno("touch", argv[i]);
            status = 1;
            continue;
        }

        if (close(fd) < 0)
        {
            print_errno("touch", argv[i]);
            status = 1;
            continue;
        }

        /* Update access and modification time to the current time. */
        if (utimensat(AT_FDCWD, argv[i], NULL, 0) < 0)
        {
            print_errno("touch", argv[i]);
            status = 1;
        }
    }

    return status;
}

static int cmd_rm(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: rm <file-or-empty-directory>...\n");
        return 1;
    }

    int status = 0;

    for (int i = 1; i < argc; i++)
    {
        struct stat st;
        if (stat(argv[i], &st) < 0)
        {
            print_errno("rm", argv[i]);
            status = 1;
            continue;
        }

        int result;
        if (S_ISDIR(st.st_mode))
            result = rmdir(argv[i]);
        else
            result = unlink(argv[i]);

        if (result < 0)
        {
            print_errno("rm", argv[i]);
            status = 1;
        }
    }

    return status;
}

static int copy_file(const char *source, const char *destination)
{
    int src = open(source, O_RDONLY);
    if (src < 0)
    {
        print_errno("cp", source);
        return 1;
    }

    struct stat st;
    if (fstat(src, &st) < 0)
    {
        print_errno("cp", source);
        close(src);
        return 1;
    }

    int dst = open(destination, O_WRONLY | O_CREAT | O_TRUNC,
                   st.st_mode & 0777);
    if (dst < 0)
    {
        print_errno("cp", destination);
        close(src);
        return 1;
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    int status = 0;

    while ((bytes_read = read(src, buffer, sizeof(buffer))) > 0)
    {
        ssize_t total_written = 0;

        while (total_written < bytes_read)
        {
            ssize_t bytes_written = write(dst,
                                          buffer + total_written,
                                          (size_t)(bytes_read - total_written));
            if (bytes_written < 0)
            {
                print_errno("cp", destination);
                status = 1;
                break;
            }
            total_written += bytes_written;
        }

        if (status != 0)
            break;
    }

    if (bytes_read < 0)
    {
        print_errno("cp", source);
        status = 1;
    }

    if (close(src) < 0)
    {
        print_errno("cp", source);
        status = 1;
    }

    if (close(dst) < 0)
    {
        print_errno("cp", destination);
        status = 1;
    }

    return status;
}

static int cmd_cp(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: cp <source> <destination>\n");
        return 1;
    }

    struct stat st;
    if (stat(argv[1], &st) < 0)
    {
        print_errno("cp", argv[1]);
        return 1;
    }

    if (S_ISDIR(st.st_mode))
    {
        fprintf(stderr, "cp: directory copying is not supported: %s\n", argv[1]);
        return 1;
    }

    if (strcmp(argv[1], argv[2]) == 0)
    {
        fprintf(stderr, "cp: source and destination are the same file\n");
        return 1;
    }

    return copy_file(argv[1], argv[2]);
}

static int cmd_mv(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: mv <source> <destination>\n");
        return 1;
    }

    if (rename(argv[1], argv[2]) < 0)
    {
        print_errno("mv", argv[1]);
        return 1;
    }

    return 0;
}

static int cmd_cat(int argc, char **argv)
{
    int status = 0;
    char buffer[BUFFER_SIZE];

    if (argc == 1)
    {
        ssize_t bytes_read;
        while ((bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0)
        {
            ssize_t total_written = 0;
            while (total_written < bytes_read)
            {
                ssize_t bytes_written = write(STDOUT_FILENO,
                                              buffer + total_written,
                                              (size_t)(bytes_read - total_written));
                if (bytes_written < 0)
                {
                    perror("cat: write");
                    return 1;
                }
                total_written += bytes_written;
            }
        }
        if (bytes_read < 0)
        {
            perror("cat: read");
            return 1;
        }
        return 0;
    }

    for (int i = 1; i < argc; i++)
    {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0)
        {
            print_errno("cat", argv[i]);
            status = 1;
            continue;
        }

        ssize_t bytes_read;
        while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0)
        {
            ssize_t total_written = 0;

            while (total_written < bytes_read)
            {
                ssize_t bytes_written = write(STDOUT_FILENO,
                                              buffer + total_written,
                                              (size_t)(bytes_read - total_written));
                if (bytes_written < 0)
                {
                    perror("cat: write");
                    status = 1;
                    break;
                }
                total_written += bytes_written;
            }

            if (status != 0)
                break;
        }

        if (bytes_read < 0)
        {
            print_errno("cat", argv[i]);
            status = 1;
        }

        close(fd);
    }

    return status;
}

static int cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        if (i > 1)
            putchar(' ');

        const char *text = argv[i];

        if (text[0] == '$' && text[1] != '\0')
        {
            const char *value = getenv(text + 1);
            if (value != NULL)
                fputs(value, stdout);
        }
        else
        {
            fputs(text, stdout);
        }
    }

    putchar('\n');
    return 0;
}

static int cmd_uptime(int argc, char **argv)
{
    (void)argv;

    if (argc != 1)
    {
        fprintf(stderr, "Usage: uptime\n");
        return 1;
    }

    FILE *file = fopen("/proc/uptime", "r");
    if (file == NULL)
    {
        print_errno("uptime", "/proc/uptime");
        return 1;
    }

    double uptime_seconds;
    double idle_seconds;

    if (fscanf(file, "%lf %lf", &uptime_seconds, &idle_seconds) != 2)
    {
        fprintf(stderr, "uptime: invalid /proc/uptime format\n");
        fclose(file);
        return 1;
    }
    fclose(file);

    file = fopen("/proc/loadavg", "r");
    if (file == NULL)
    {
        print_errno("uptime", "/proc/loadavg");
        return 1;
    }

    char load1[32], load5[32], load15[32];
    if (fscanf(file, "%31s %31s %31s", load1, load5, load15) != 3)
    {
        fprintf(stderr, "uptime: invalid /proc/loadavg format\n");
        fclose(file);
        return 1;
    }
    fclose(file);

    long total_seconds = (long)uptime_seconds;
    long days = total_seconds / 86400;
    total_seconds %= 86400;
    long hours = total_seconds / 3600;
    total_seconds %= 3600;
    long minutes = total_seconds / 60;
    long seconds = total_seconds % 60;

    printf("up ");
    if (days > 0)
        printf("%ld day%s, ", days, days == 1 ? "" : "s");
    printf("%02ld:%02ld:%02ld, load average: %s, %s, %s\n",
           hours, minutes, seconds, load1, load5, load15);

    return 0;
}

static int cmd_meminfo(int argc, char **argv)
{
    (void)argv;

    if (argc != 1)
    {
        fprintf(stderr, "Usage: meminfo\n");
        return 1;
    }

    FILE *file = fopen("/proc/meminfo", "r");
    if (file == NULL)
    {
        print_errno("meminfo", "/proc/meminfo");
        return 1;
    }

    char line[BUFFER_SIZE];
    while (fgets(line, sizeof(line), file) != NULL)
    {
        /* Display the most useful memory fields. */
        if (strncmp(line, "MemTotal:", 9) == 0 ||
            strncmp(line, "MemFree:", 8) == 0 ||
            strncmp(line, "MemAvailable:", 13) == 0 ||
            strncmp(line, "Buffers:", 8) == 0 ||
            strncmp(line, "Cached:", 7) == 0 ||
            strncmp(line, "SwapTotal:", 10) == 0 ||
            strncmp(line, "SwapFree:", 9) == 0)
        {
            fputs(line, stdout);
        }
    }

    if (ferror(file))
    {
        perror("meminfo: read");
        fclose(file);
        return 1;
    }

    fclose(file);
    return 0;
}

static int is_native_command(const char *name)
{
    const char *commands[] = {
        "pwd", "ls", "whoami", "uname", "mkdir", "touch", "rm",
        "cp", "mv", "cat", "echo", "uptime", "meminfo", "clear",
        "history", "help", "cd", "exit"
    };

    size_t count = sizeof(commands) / sizeof(commands[0]);
    for (size_t i = 0; i < count; i++)
        if (strcmp(name, commands[i]) == 0)
            return 1;

    return 0;
}

static int tokenize_command(char *line, char **tokens)
{
    int count = 0;
    char *p = line;

    while (*p != '\0' && count < MAX_ARGS - 1)
    {
        while (isspace((unsigned char)*p))
            p++;

        if (*p == '\0')
            break;

        if (*p == '\'' || *p == '"')
        {
            char quote = *p++;
            char *start = p;
            char *dst = p;

            while (*p != '\0' && *p != quote)
            {
                if (*p == '\\' && *(p + 1) != '\0')
                    p++;
                *dst++ = *p++;
            }

            if (*p == quote)
                p++;

            *dst = '\0';
            tokens[count++] = start;
        }
        else if (*p == '>' && *(p + 1) == '>')
        {
            tokens[count++] = p;
            *p = '\0';
            p++;
            *p = '\0';
            p++;
            tokens[count - 1] = (char *)">>";
        }
        else if (*p == '|' || *p == '<' || *p == '>' || *p == '&')
        {
            static char operators[4][2] = {{'|','\0'},{'<','\0'},{'>','\0'},{'&','\0'}};
            int index = (*p == '|') ? 0 : (*p == '<') ? 1 : (*p == '>') ? 2 : 3;
            tokens[count++] = operators[index];
            p++;
        }
        else
        {
            char *start = p;
            while (*p != '\0' &&
                   !isspace((unsigned char)*p) &&
                   *p != '|' && *p != '<' && *p != '>' && *p != '&')
                p++;

            if (*p != '\0')
            {
                *p = '\0';
                p++;
            }
            tokens[count++] = start;
        }
    }

    tokens[count] = NULL;
    return count;
}

static int is_operator_token(const char *s)
{
    return strcmp(s, "|") == 0 || strcmp(s, "<") == 0 ||
           strcmp(s, ">") == 0 || strcmp(s, ">>") == 0 ||
           strcmp(s, "&") == 0;
}

static int parse_segment(char **tokens, int start, int end,
                         char **argv, char **input_file,
                         char **output_file, int *append_output)
{
    int argc = 0;
    *input_file = NULL;
    *output_file = NULL;
    *append_output = 0;

    for (int i = start; i < end; i++)
    {
        if (strcmp(tokens[i], "<") == 0)
        {
            if (i + 1 >= end || is_operator_token(tokens[i + 1]))
            {
                fprintf(stderr, "myshell: missing input file after '<'\n");
                return -1;
            }
            *input_file = tokens[++i];
        }
        else if (strcmp(tokens[i], ">") == 0 || strcmp(tokens[i], ">>") == 0)
        {
            if (i + 1 >= end || is_operator_token(tokens[i + 1]))
            {
                fprintf(stderr, "myshell: missing output file after '%s'\n", tokens[i]);
                return -1;
            }
            *output_file = tokens[++i];
            *append_output = (strcmp(tokens[i - 1], ">>") == 0);
        }
        else if (strcmp(tokens[i], "&") == 0)
        {
            fprintf(stderr, "myshell: '&' must be at the end of the command\n");
            return -1;
        }
        else if (strcmp(tokens[i], "|") == 0)
        {
            fprintf(stderr, "myshell: invalid pipe placement\n");
            return -1;
        }
        else
        {
            if (argc >= MAX_ARGS - 1)
            {
                fprintf(stderr, "myshell: too many arguments\n");
                return -1;
            }
            argv[argc++] = tokens[i];
        }
    }

    argv[argc] = NULL;
    return argc;
}

static int apply_redirections(const char *input_file, const char *output_file,
                              int append_output)
{
    int fd;

    if (input_file != NULL)
    {
        fd = open(input_file, O_RDONLY);
        if (fd < 0)
        {
            print_errno("redirect input", input_file);
            return -1;
        }
        if (dup2(fd, STDIN_FILENO) < 0)
        {
            perror("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }

    if (output_file != NULL)
    {
        int flags = O_WRONLY | O_CREAT;
        flags |= append_output ? O_APPEND : O_TRUNC;

        fd = open(output_file, flags, 0644);
        if (fd < 0)
        {
            print_errno("redirect output", output_file);
            return -1;
        }
        if (dup2(fd, STDOUT_FILENO) < 0)
        {
            perror("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }

    return 0;
}

static int count_args(char **args)
{
    int count = 0;
    while (args[count] != NULL)
        count++;
    return count;
}


static int run_native_command(int argc, char **argv)
{
    if (strcmp(argv[0], "pwd") == 0) return cmd_pwd(argc, argv);
    if (strcmp(argv[0], "ls") == 0) return cmd_ls(argc, argv);
    if (strcmp(argv[0], "whoami") == 0) return cmd_whoami(argc, argv);
    if (strcmp(argv[0], "uname") == 0) return cmd_uname(argc, argv);
    if (strcmp(argv[0], "mkdir") == 0) return cmd_mkdir(argc, argv);
    if (strcmp(argv[0], "touch") == 0) return cmd_touch(argc, argv);
    if (strcmp(argv[0], "rm") == 0) return cmd_rm(argc, argv);
    if (strcmp(argv[0], "cp") == 0) return cmd_cp(argc, argv);
    if (strcmp(argv[0], "mv") == 0) return cmd_mv(argc, argv);
    if (strcmp(argv[0], "cat") == 0) return cmd_cat(argc, argv);
    if (strcmp(argv[0], "echo") == 0) return cmd_echo(argc, argv);
    if (strcmp(argv[0], "uptime") == 0) return cmd_uptime(argc, argv);
    if (strcmp(argv[0], "meminfo") == 0) return cmd_meminfo(argc, argv);
    if (strcmp(argv[0], "clear") == 0)
    {
        printf("\033[2J\033[H");
        fflush(stdout);
        return 0;
    }
    if (strcmp(argv[0], "history") == 0)
    {
        print_history();
        return 0;
    }
    if (strcmp(argv[0], "help") == 0)
    {
        print_help();
        return 0;
    }
    return -1;
}

static void child_execute(char **args)
{
    int argc = count_args(args);

    if (is_native_command(args[0]))
    {
        int result = run_native_command(argc, args);
        fflush(NULL);
        _exit(result < 0 ? EXIT_FAILURE : result);
    }

    execvp(args[0], args);
    fprintf(stderr, "myshell: %s: %s\n", args[0], strerror(errno));
    _exit(127);
}

static int execute_external(char **args, const char *input_file,
                            const char *output_file, int append_output,
                            int background)
{
    pid_t pid = fork();

    if (pid < 0)
    {
        perror("fork");
        return 1;
    }

    if (pid == 0)
    {
        if (apply_redirections(input_file, output_file, append_output) < 0)
            _exit(EXIT_FAILURE);
        child_execute(args);
    }

    if (background)
    {
        printf("[background pid %d]\n", pid);
        return 0;
    }

    int status;
    if (waitpid(pid, &status, 0) < 0)
    {
        perror("waitpid");
        return 1;
    }

    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static int execute_pipe(char **left_args, const char *left_input,
                        char **right_args, const char *right_output,
                        int right_append, int background)
{
    int pipefd[2];
    if (pipe(pipefd) < 0)
    {
        perror("pipe");
        return 1;
    }

    pid_t left_pid = fork();
    if (left_pid < 0)
    {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return 1;
    }

    if (left_pid == 0)
    {
        if (left_input != NULL && apply_redirections(left_input, NULL, 0) < 0)
            _exit(EXIT_FAILURE);

        if (dup2(pipefd[1], STDOUT_FILENO) < 0)
        {
            perror("dup2");
            _exit(EXIT_FAILURE);
        }
        close(pipefd[0]);
        close(pipefd[1]);
        child_execute(left_args);
    }

    pid_t right_pid = fork();
    if (right_pid < 0)
    {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        waitpid(left_pid, NULL, 0);
        return 1;
    }

    if (right_pid == 0)
    {
        if (right_output != NULL &&
            apply_redirections(NULL, right_output, right_append) < 0)
            _exit(EXIT_FAILURE);

        if (dup2(pipefd[0], STDIN_FILENO) < 0)
        {
            perror("dup2");
            _exit(EXIT_FAILURE);
        }
        close(pipefd[0]);
        close(pipefd[1]);
        child_execute(right_args);
    }

    close(pipefd[0]);
    close(pipefd[1]);

    if (background)
    {
        printf("[background pids %d %d]\n", left_pid, right_pid);
        return 0;
    }

    waitpid(left_pid, NULL, 0);
    waitpid(right_pid, NULL, 0);
    return 0;
}

static void reap_background_processes(void)
{
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
        printf("\n[background process %d finished]\n", pid);
}


static int run_parent_builtin_with_redirection(int argc, char **argv,
                                               const char *input_file,
                                               const char *output_file,
                                               int append_output,
                                               int *shell_status)
{
    int saved_in = -1;
    int saved_out = -1;

    if (input_file != NULL)
    {
        saved_in = dup(STDIN_FILENO);
        if (saved_in < 0)
        {
            perror("dup");
            return 1;
        }
        int fd = open(input_file, O_RDONLY);
        if (fd < 0)
        {
            print_errno("redirect input", input_file);
            close(saved_in);
            return 1;
        }
        if (dup2(fd, STDIN_FILENO) < 0)
        {
            perror("dup2");
            close(fd);
            close(saved_in);
            return 1;
        }
        close(fd);
    }

    if (output_file != NULL)
    {
        saved_out = dup(STDOUT_FILENO);
        if (saved_out < 0)
        {
            perror("dup");
            if (saved_in >= 0)
            {
                dup2(saved_in, STDIN_FILENO);
                close(saved_in);
            }
            return 1;
        }

        int flags = O_WRONLY | O_CREAT | (append_output ? O_APPEND : O_TRUNC);
        int fd = open(output_file, flags, 0644);
        if (fd < 0)
        {
            print_errno("redirect output", output_file);
            close(saved_out);
            if (saved_in >= 0)
            {
                dup2(saved_in, STDIN_FILENO);
                close(saved_in);
            }
            return 1;
        }
        if (dup2(fd, STDOUT_FILENO) < 0)
        {
            perror("dup2");
            close(fd);
            close(saved_out);
            if (saved_in >= 0)
            {
                dup2(saved_in, STDIN_FILENO);
                close(saved_in);
            }
            return 1;
        }
        close(fd);
    }

    int result = 0;
    if (strcmp(argv[0], "cd") == 0)
    {
        if (argc > 2)
        {
            fprintf(stderr, "cd: too many arguments\n");
            result = 1;
        }
        else
        {
            char target[PATH_MAX];
            const char *path = (argc == 1) ? shell_root : argv[1];

            if (build_cd_target(path, target, sizeof(target)) < 0)
                result = 1;
            else if (chdir(target) < 0)
            {
                print_errno("cd", target);
                result = 1;
            }
        }
    }
    else if (strcmp(argv[0], "echo") == 0)
        result = cmd_echo(argc, argv);
    else if (strcmp(argv[0], "help") == 0)
        result = (argc == 1) ? (print_help(), 0) : 1;
    else if (strcmp(argv[0], "exit") == 0)
    {
        if (argc > 2)
        {
            fprintf(stderr, "exit: too many arguments\n");
            result = 1;
        }
        else
        {
            *shell_status = 0;
            if (argc == 2)
            {
                char *end;
                errno = 0;
                long value = strtol(argv[1], &end, 10);
                if (errno != 0 || *end != '\0' || value < 0 || value > 255)
                {
                    fprintf(stderr, "exit: status must be 0-255\n");
                    result = 1;
                }
                else
                    *shell_status = (int)value;
            }
            if (result == 0)
            {
                if (saved_in >= 0) { dup2(saved_in, STDIN_FILENO); close(saved_in); }
                if (saved_out >= 0) { dup2(saved_out, STDOUT_FILENO); close(saved_out); }
                return 2;
            }
        }
    }

    fflush(stdout);
    if (saved_in >= 0) { dup2(saved_in, STDIN_FILENO); close(saved_in); }
    if (saved_out >= 0) { dup2(saved_out, STDOUT_FILENO); close(saved_out); }
    return result;
}

int main(int argc, char **argv)
{
    char *line = NULL;
    size_t line_capacity = 0;
    int shell_status = 0;

    char executable_path[PATH_MAX];
    if (argc > 0 && realpath(argv[0], executable_path) != NULL)
    {
        char *slash = strrchr(executable_path, '/');
        if (slash != NULL)
        {
            *slash = '\0';
            strncpy(shell_root, executable_path, sizeof(shell_root) - 1);
            shell_root[sizeof(shell_root) - 1] = '\0';
        }
    }

    if (shell_root[0] == '\0' && getcwd(shell_root, sizeof(shell_root)) == NULL)
    {
        perror("getcwd");
        return EXIT_FAILURE;
    }

    if (chdir(shell_root) < 0)
    {
        perror("chdir shell root");
        return EXIT_FAILURE;
    }

    printf("====================================\n");
    printf("           MAYESHA'S SHELL                 \n");
    printf("====================================\n");
    printf("Type 'help' for commands.\n\n");

    while (1)
    {
        reap_background_processes();

        char virtual_path[PATH_MAX];
        if (get_virtual_path(virtual_path, sizeof(virtual_path)) == 0)
            printf("myshell:%s$ ", virtual_path);
        else
            printf("myshell:$ ");
        fflush(stdout);

        ssize_t length = getline(&line, &line_capacity, stdin);
        if (length == -1)
        {
            printf("\n");
            break;
        }

        if (length > 0 && line[length - 1] == '\n')
            line[length - 1] = '\0';

        if (line[0] == '\0')
            continue;

        add_history(line);

        char *tokens[MAX_ARGS];
        int token_count = tokenize_command(line, tokens);
        if (token_count == 0)
            continue;

        int background = 0;
        if (strcmp(tokens[token_count - 1], "&") == 0)
        {
            background = 1;
            token_count--;
            tokens[token_count] = NULL;
        }

        if (token_count == 0)
        {
            fprintf(stderr, "myshell: missing command before '&'\n");
            continue;
        }

        int pipe_pos = -1;
        int pipe_count = 0;
        for (int i = 0; i < token_count; i++)
        {
            if (strcmp(tokens[i], "|") == 0)
            {
                pipe_count++;
                pipe_pos = i;
            }
        }

        if (pipe_count > 1)
        {
            fprintf(stderr, "myshell: only one pipe is supported\n");
            continue;
        }

        if (pipe_count == 1)
        {
            if (pipe_pos == 0 || pipe_pos == token_count - 1)
            {
                fprintf(stderr, "myshell: invalid pipe usage\n");
                continue;
            }

            char *left_args[MAX_ARGS];
            char *right_args[MAX_ARGS];
            char *left_input = NULL, *left_output = NULL;
            char *right_input = NULL, *right_output = NULL;
            int left_append = 0, right_append = 0;

            if (parse_segment(tokens, 0, pipe_pos, left_args,
                              &left_input, &left_output, &left_append) < 0 ||
                parse_segment(tokens, pipe_pos + 1, token_count, right_args,
                              &right_input, &right_output, &right_append) < 0)
                continue;

            if (left_output != NULL || right_input != NULL)
            {
                fprintf(stderr,
                        "myshell: in a pipe, use input redirection on the first command and output redirection on the second\n");
                continue;
            }

            execute_pipe(left_args, left_input, right_args,
                         right_output, right_append, background);
            continue;
        }

        char *args[MAX_ARGS];
        char *input_file = NULL;
        char *output_file = NULL;
        int append_output = 0;

        int argc = parse_segment(tokens, 0, token_count, args,
                                 &input_file, &output_file, &append_output);
        if (argc < 0)
            continue;

        if (background &&
            (strcmp(args[0], "cd") == 0 || strcmp(args[0], "exit") == 0))
        {
            fprintf(stderr,
                    "myshell: %s cannot be meaningfully run in background\n",
                    args[0]);
            continue;
        }

        if (strcmp(args[0], "cd") == 0 ||
            strcmp(args[0], "echo") == 0 ||
            strcmp(args[0], "help") == 0 ||
            strcmp(args[0], "exit") == 0)
        {
            int result = run_parent_builtin_with_redirection(
                argc, args, input_file, output_file, append_output,
                &shell_status);

            if (result == 2)
                break;
            continue;
        }

        execute_external(args, input_file, output_file,
                         append_output, background);
    }

    free(line);
    clear_history();
    return shell_status;
}
