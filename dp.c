#define _XOPEN_SOURCE 600

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define MAX_FILTER_CMD 8192
#define MAX_LOCAL_LINE 8192
#define DEFAULT_FILTER_IDLE_MS 350
#define DEFAULT_ESCAPE_CHAR 0x1d /* Ctrl-] */

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <util.h>
#endif

enum input_mode {
    MODE_REMOTE = 0,
    MODE_PIPE1,
    MODE_FILTER,
    MODE_ESCAPE
};

struct options {
    const char *log_path;
    int debug;
    int idle_ms;
    int escape_enabled;
    unsigned char escape_char;
    int cmd_index;
};

static struct termios g_orig_stdin;
static int g_stdin_saved = 0;
static int g_master_fd = -1;
static FILE *g_log_fp = NULL;
static int g_debug_enabled = 0;

static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

static void debug_log(const char *fmt, ...) {
    va_list ap;
    if (!g_debug_enabled) {
        return;
    }
    fprintf(stderr, "\r\n[dp debug] ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\r\n");
}

static ssize_t write_all(int fd, const void *buf, size_t len) {
    const unsigned char *p = (const unsigned char *)buf;
    size_t total = 0;

    while (total < len) {
        ssize_t n = write(fd, p + total, len - total);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        total += (size_t)n;
    }
    return (ssize_t)total;
}

static void log_bytes(const char *tag, const void *buf, size_t len) {
    if (g_log_fp == NULL || len == 0) {
        return;
    }
    fprintf(g_log_fp, "[%s] ", tag);
    fwrite(buf, 1, len, g_log_fp);
    fputc('\n', g_log_fp);
    fflush(g_log_fp);
}

static void restore_stdin(void) {
    if (g_stdin_saved) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_stdin);
    }
}

static void cleanup_log(void) {
    if (g_log_fp != NULL) {
        fclose(g_log_fp);
        g_log_fp = NULL;
    }
}

static void make_raw_noecho(struct termios *t) {
    t->c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    t->c_oflag |= OPOST;
    t->c_cflag |= CS8;
    t->c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
}

static void setup_local_tty(void) {
    struct termios raw;

    if (!isatty(STDIN_FILENO)) {
        return;
    }

    if (tcgetattr(STDIN_FILENO, &g_orig_stdin) != 0) {
        die("tcgetattr(stdin) failed: %s", strerror(errno));
    }

    g_stdin_saved = 1;
    atexit(restore_stdin);

    raw = g_orig_stdin;
    make_raw_noecho(&raw);

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        die("tcsetattr(stdin raw) failed: %s", strerror(errno));
    }
}

static void sync_window_size(void) {
#if defined(TIOCGWINSZ) && defined(TIOCSWINSZ)
    struct winsize ws;

    if (g_master_fd < 0) {
        return;
    }
    if (!isatty(STDIN_FILENO)) {
        return;
    }
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
        (void)ioctl(g_master_fd, TIOCSWINSZ, &ws);
    }
#endif
}

#ifdef SIGWINCH
static void on_sigwinch(int sig) {
    (void)sig;
    sync_window_size();
}
#endif

static void trim_trailing_whitespace(char *s) {
    size_t len;
    if (s == NULL) {
        return;
    }
    len = strlen(s);
    while (len > 0) {
        unsigned char ch = (unsigned char)s[len - 1];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            s[len - 1] = '\0';
            len--;
        } else {
            break;
        }
    }
}

static char *trim_leading_whitespace(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
        s++;
    }
    return s;
}

static void normalize_double_pipe_pipeline(char *dst, size_t dstsz, const char *src) {
    size_t i = 0;
    size_t j = 0;

    if (dstsz == 0) {
        return;
    }

    while (src[i] != '\0' && j + 1 < dstsz) {
        if (src[i] == '|' && src[i + 1] == '|') {
            while (j > 0 && dst[j - 1] == ' ') {
                j--;
            }
            if (j + 3 >= dstsz) {
                break;
            }
            dst[j++] = ' ';
            dst[j++] = '|';
            dst[j++] = ' ';
            i += 2;
            while (src[i] == ' ' || src[i] == '\t') {
                i++;
            }
        } else {
            dst[j++] = src[i++];
        }
    }

    while (j > 0 && dst[j - 1] == ' ') {
        j--;
    }
    dst[j] = '\0';
}

static FILE *open_filter_process(const char *cmd) {
    FILE *fp;

    if (cmd == NULL || *cmd == '\0') {
        return NULL;
    }

    fp = popen(cmd, "w");
    if (fp == NULL) {
        fprintf(stderr, "\r\nDouble Pipe: popen failed for '%s': %s\r\n", cmd, strerror(errno));
    } else {
        debug_log("opened local filter: %s", cmd);
    }
    return fp;
}

static void close_filter_process(FILE **fp) {
    if (fp != NULL && *fp != NULL) {
        debug_log("closing local filter");
        pclose(*fp);
        *fp = NULL;
    }
}

static void spawn_child_or_die(int slave_fd, int argc, char *argv[], int cmd_index) {
    char **child_argv;
    int i;
    int child_argc = argc - cmd_index;

    if (setsid() < 0) {
        die("setsid failed: %s", strerror(errno));
    }

#ifdef TIOCSCTTY
    (void)ioctl(slave_fd, TIOCSCTTY, 0);
#endif

    if (dup2(slave_fd, STDIN_FILENO) < 0 ||
        dup2(slave_fd, STDOUT_FILENO) < 0 ||
        dup2(slave_fd, STDERR_FILENO) < 0) {
        die("dup2(slave_fd) failed: %s", strerror(errno));
    }

    if (slave_fd > STDERR_FILENO) {
        close(slave_fd);
    }

    child_argv = (char **)calloc((size_t)child_argc + 1U, sizeof(char *));
    if (child_argv == NULL) {
        die("calloc(child_argv) failed");
    }

    for (i = 0; i < child_argc; i++) {
        child_argv[i] = argv[cmd_index + i];
    }
    child_argv[child_argc] = NULL;

    execvp(child_argv[0], child_argv);
    die("execvp('%s') failed: %s", child_argv[0], strerror(errno));
}

static int parse_escape_char(const char *s, unsigned char *out) {
    if (s == NULL || *s == '\0' || out == NULL) {
        return -1;
    }
    if (strcmp(s, "ctrl-") == 0) {
        *out = 0x1c;
        return 0;
    }
    if (strcmp(s, "ctrl-") == 0) {
        *out = 0x1c;
        return 0;
    }
    if (strncmp(s, "ctrl-", 5) == 0 && s[5] != '\0' && s[6] == '\0') {
        unsigned char c = (unsigned char)s[5];
        if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
        *out = (unsigned char)(c & 0x1f);
        return 0;
    }
    if (strlen(s) == 1) {
        *out = (unsigned char)s[0];
        return 0;
    }
    return -1;
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [options] <program> [args ...]\n"
            "Options:\n"
            "  --log <file>          Log remote output and local control events\n"
            "  --debug               Show filter/escape state transitions\n"
            "  --idle-ms <n>         Filter idle timeout in ms (default %d)\n"
            "  --escape              Enable escape command mode (default off)\n"
            "  --escape-char <key>   Escape key, e.g. ctrl-], ctrl-t, ~\n"
            "  --help                Show this help\n",
            prog, DEFAULT_FILTER_IDLE_MS);
}

static struct options parse_options(int argc, char *argv[]) {
    struct options opt;
    int i;

    opt.log_path = NULL;
    opt.debug = 0;
    opt.idle_ms = DEFAULT_FILTER_IDLE_MS;
    opt.escape_enabled = 0;
    opt.escape_char = DEFAULT_ESCAPE_CHAR;
    opt.cmd_index = -1;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            exit(EXIT_SUCCESS);
        } else if (strcmp(argv[i], "--debug") == 0) {
            opt.debug = 1;
        } else if (strcmp(argv[i], "--escape") == 0) {
            opt.escape_enabled = 1;
        } else if (strcmp(argv[i], "--log") == 0) {
            if (i + 1 >= argc) {
                die("--log requires a file path");
            }
            opt.log_path = argv[++i];
        } else if (strcmp(argv[i], "--idle-ms") == 0) {
            char *end;
            long v;
            if (i + 1 >= argc) {
                die("--idle-ms requires a number");
            }
            errno = 0;
            v = strtol(argv[++i], &end, 10);
            if (errno != 0 || end == argv[i] || *end != '\0' || v < 50 || v > 10000) {
                die("invalid --idle-ms value: %s", argv[i]);
            }
            opt.idle_ms = (int)v;
        } else if (strcmp(argv[i], "--escape-char") == 0) {
            if (i + 1 >= argc) {
                die("--escape-char requires a key value");
            }
            if (parse_escape_char(argv[++i], &opt.escape_char) != 0) {
                die("invalid --escape-char value: %s", argv[i]);
            }
            opt.escape_enabled = 1;
        } else if (strncmp(argv[i], "--", 2) == 0) {
            die("unknown option: %s", argv[i]);
        } else {
            opt.cmd_index = i;
            break;
        }
    }

    if (opt.cmd_index < 0) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }
    return opt;
}

static void render_escape_help(void) {
    static const char msg[] =
        "\r\n[double-pipe escape mode]\r\n"
        "  r / resume    return to remote session\r\n"
        "  q / quit      exit double pipe\r\n"
        "  !cmd          run a local shell command\r\n"
        "  ? / help      show this help\r\n"
        "\r\nescape> ";
    (void)write_all(STDOUT_FILENO, msg, sizeof(msg) - 1U);
}

int main(int argc, char *argv[]) {
    int master_fd;
    int slave_fd;
    pid_t pid;
    struct options opt;
    enum input_mode mode = MODE_REMOTE;
    char filter_cmd[MAX_FILTER_CMD];
    char normalized_cmd[MAX_FILTER_CMD];
    char local_line[MAX_LOCAL_LINE];
    size_t filter_len = 0;
    size_t local_len = 0;
    FILE *filter_fp = NULL;
    unsigned char last_eol = '\r';

    opt = parse_options(argc, argv);
    g_debug_enabled = opt.debug;

    if (opt.log_path != NULL) {
        g_log_fp = fopen(opt.log_path, "a");
        if (g_log_fp == NULL) {
            die("cannot open log file '%s': %s", opt.log_path, strerror(errno));
        }
        atexit(cleanup_log);
        fprintf(g_log_fp, "===== double pipe session start =====\n");
        fflush(g_log_fp);
    }

    master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd < 0) {
        die("posix_openpt failed: %s", strerror(errno));
    }

    if (grantpt(master_fd) != 0) {
        die("grantpt failed: %s", strerror(errno));
    }

    if (unlockpt(master_fd) != 0) {
        die("unlockpt failed: %s", strerror(errno));
    }

    slave_fd = open(ptsname(master_fd), O_RDWR | O_NOCTTY);
    if (slave_fd < 0) {
        die("open(ptsname(master_fd)) failed: %s", strerror(errno));
    }

    pid = fork();
    if (pid < 0) {
        die("fork failed: %s", strerror(errno));
    }

    if (pid == 0) {
        close(master_fd);
        spawn_child_or_die(slave_fd, argc, argv, opt.cmd_index);
        return EXIT_FAILURE;
    }

    close(slave_fd);
    g_master_fd = master_fd;

    setup_local_tty();
    sync_window_size();

#ifdef SIGWINCH
    signal(SIGWINCH, on_sigwinch);
#endif
#ifdef SIGINT
    signal(SIGINT, SIG_IGN);
#endif
#ifdef SIGQUIT
    signal(SIGQUIT, SIG_IGN);
#endif
#ifdef SIGTSTP
    signal(SIGTSTP, SIG_IGN);
#endif

    for (;;) {
        fd_set rfds;
        int rc;
        struct timeval tv;
        struct timeval *tvp = NULL;

        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        FD_SET(master_fd, &rfds);

        if (filter_fp != NULL && mode == MODE_REMOTE) {
            tv.tv_sec = opt.idle_ms / 1000;
            tv.tv_usec = (opt.idle_ms % 1000) * 1000;
            tvp = &tv;
        }

        rc = select((STDIN_FILENO > master_fd ? STDIN_FILENO : master_fd) + 1,
                    &rfds, NULL, NULL, tvp);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            die("select failed: %s", strerror(errno));
        }

        if (rc == 0) {
            if (filter_fp != NULL && mode == MODE_REMOTE) {
                debug_log("idle timeout reached; auto-closing local filter");
                log_bytes("EVENT", "idle-close", strlen("idle-close"));
                close_filter_process(&filter_fp);
                (void)write_all(master_fd, &last_eol, 1);
            }
            continue;
        }

        if (FD_ISSET(master_fd, &rfds)) {
            unsigned char buf[4096];
            ssize_t n = read(master_fd, buf, sizeof(buf));
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                die("read(master_fd) failed: %s", strerror(errno));
            }
            if (n == 0) {
                break;
            }

            if (filter_fp != NULL) {
                if (fwrite(buf, 1, (size_t)n, filter_fp) != (size_t)n) {
                    close_filter_process(&filter_fp);
                    fprintf(stderr, "\r\nDouble Pipe: local filter write failed\r\n");
                } else {
                    fflush(filter_fp);
                }
            } else {
                if (write_all(STDOUT_FILENO, buf, (size_t)n) < 0) {
                    die("write(stdout) failed: %s", strerror(errno));
                }
            }
            log_bytes("REMOTE", buf, (size_t)n);
        }

        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            unsigned char ch;
            ssize_t n = read(STDIN_FILENO, &ch, 1);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                die("read(stdin) failed: %s", strerror(errno));
            }
            if (n == 0) {
                break;
            }

            if (opt.escape_enabled && mode == MODE_REMOTE && ch == opt.escape_char) {
                mode = MODE_ESCAPE;
                local_len = 0;
                local_line[0] = '\0';
                debug_log("entered escape mode");
                render_escape_help();
                continue;
            }

            if (mode != MODE_FILTER && mode != MODE_ESCAPE && filter_fp != NULL) {
                close_filter_process(&filter_fp);
            }

            if (mode == MODE_REMOTE) {
                if (ch == '|') {
                    mode = MODE_PIPE1;
                } else {
                    if (write_all(master_fd, &ch, 1) < 0) {
                        die("write(master_fd) failed: %s", strerror(errno));
                    }
                }
            } else if (mode == MODE_PIPE1) {
                if (ch == '|') {
                    mode = MODE_FILTER;
                    filter_len = 0;
                    filter_cmd[0] = '\0';
                    debug_log("capturing local pipeline after ||");
                    (void)write_all(STDOUT_FILENO, "||", 2);
                } else {
                    unsigned char pipe_char = '|';
                    if (write_all(master_fd, &pipe_char, 1) < 0) {
                        die("write(master_fd,'|') failed: %s", strerror(errno));
                    }
                    mode = MODE_REMOTE;
                    if (write_all(master_fd, &ch, 1) < 0) {
                        die("write(master_fd) failed: %s", strerror(errno));
                    }
                }
            } else if (mode == MODE_FILTER) {
                if (ch == '\r' || ch == '\n') {
                    char *cmd;
                    unsigned char eol = ch;
                    last_eol = ch;

                    filter_cmd[filter_len] = '\0';
                    trim_trailing_whitespace(filter_cmd);
                    cmd = trim_leading_whitespace(filter_cmd);
                    normalize_double_pipe_pipeline(normalized_cmd, sizeof(normalized_cmd), cmd);

                    (void)write_all(STDOUT_FILENO, "\r\n", 2);
                    if (normalized_cmd[0] != '\0') {
                        filter_fp = open_filter_process(normalized_cmd);
                        log_bytes("FILTER", normalized_cmd, strlen(normalized_cmd));
                    }
                    if (write_all(master_fd, &eol, 1) < 0) {
                        die("write(master_fd,eol) failed: %s", strerror(errno));
                    }
                    mode = MODE_REMOTE;
                    filter_len = 0;
                    filter_cmd[0] = '\0';
                    normalized_cmd[0] = '\0';
                } else if (ch == 0x7f || ch == 0x08) {
                    if (filter_len > 0) {
                        filter_len--;
                        filter_cmd[filter_len] = '\0';
                        (void)write_all(STDOUT_FILENO, "\b \b", 3);
                    }
                } else if (ch == 0x1b) {
                    mode = MODE_REMOTE;
                    filter_len = 0;
                    filter_cmd[0] = '\0';
                    normalized_cmd[0] = '\0';
                    debug_log("cancelled local pipeline capture");
                    (void)write_all(STDOUT_FILENO, "\r\n", 2);
                } else {
                    if (filter_len + 1 < sizeof(filter_cmd)) {
                        filter_cmd[filter_len++] = (char)ch;
                        filter_cmd[filter_len] = '\0';
                        (void)write_all(STDOUT_FILENO, &ch, 1);
                    }
                }
            } else if (mode == MODE_ESCAPE) {
                if (ch == '\r' || ch == '\n') {
                    local_line[local_len] = '\0';
                    trim_trailing_whitespace(local_line);
                    {
                        char *cmd = trim_leading_whitespace(local_line);
                        if (strcmp(cmd, "") == 0 || strcmp(cmd, "r") == 0 || strcmp(cmd, "resume") == 0) {
                            mode = MODE_REMOTE;
                            debug_log("leaving escape mode");
                            (void)write_all(STDOUT_FILENO, "\r\n", 2);
                        } else if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
                            (void)write_all(STDOUT_FILENO, "\r\n", 2);
                            break;
                        } else if (strcmp(cmd, "?") == 0 || strcmp(cmd, "help") == 0) {
                            render_escape_help();
                        } else if (cmd[0] == '!') {
                            (void)write_all(STDOUT_FILENO, "\r\n", 2);
                            debug_log("running local command from escape mode: %s", cmd + 1);
                            log_bytes("LOCAL", cmd + 1, strlen(cmd + 1));
                            restore_stdin();
                            (void)system(cmd + 1);
                            setup_local_tty();
                            (void)write_all(STDOUT_FILENO, "\r\nescape> ", 10);
                        } else {
                            static const char bad[] = "\r\nunknown escape command\r\nescape> ";
                            (void)write_all(STDOUT_FILENO, bad, sizeof(bad) - 1U);
                        }
                    }
                    local_len = 0;
                    local_line[0] = '\0';
                } else if (ch == 0x7f || ch == 0x08) {
                    if (local_len > 0) {
                        local_len--;
                        local_line[local_len] = '\0';
                        (void)write_all(STDOUT_FILENO, "\b \b", 3);
                    }
                } else if (ch == 0x1b) {
                    mode = MODE_REMOTE;
                    local_len = 0;
                    local_line[0] = '\0';
                    debug_log("escape mode cancelled");
                    (void)write_all(STDOUT_FILENO, "\r\n", 2);
                } else {
                    if (local_len + 1 < sizeof(local_line)) {
                        local_line[local_len++] = (char)ch;
                        local_line[local_len] = '\0';
                        (void)write_all(STDOUT_FILENO, &ch, 1);
                    }
                }
            }
        }

        {
            int status;
            pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid) {
                break;
            }
        }
    }

    close_filter_process(&filter_fp);
    restore_stdin();
    cleanup_log();
    close(master_fd);
    return 0;
}
