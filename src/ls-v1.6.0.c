/*
 * ls-v1.6.0 : adds -R (recursive listing). Combines with -l / -x.   Usage: ls [-l | -x] [-R] [dir ...]
 *   (also keeps v1.5.0 colors: dir=blue exec=green tar/gz/zip=red symlink=pink special=reverse)
 *   dir=blue  exec=green  tar/gz/zip=red  symlink=pink  special files=reverse video
 *   Usage: ls [-l | -x] [dir ...]
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>

extern int errno;

/* ANSI escape codes */
#define COLOR_RESET   "\033[0m"
#define COLOR_BLUE    "\033[0;34m"
#define COLOR_GREEN   "\033[0;32m"
#define COLOR_RED     "\033[0;31m"
#define COLOR_PINK    "\033[0;35m"
#define COLOR_REVERSE "\033[7m"

/* Display mode: which printing function do_ls() dispatches to */
enum display_mode { MODE_DEFAULT, MODE_LONG, MODE_HORIZONTAL };

void do_ls(const char *dir, int mode, int recursive, int show_header);
void build_path(const char *dir, const char *name, char *out, size_t size);
char **read_entries(const char *dir, int *count, size_t *maxlen);
int cmp_names(const void *a, const void *b);
void free_entries(char **names, int n);
int get_term_width(void);
void display_columns(const char *dir, char **names, int n, size_t maxlen);
void display_horizontal(const char *dir, char **names, int n, size_t maxlen);
void print_name(const char *dir, const char *name);
void display_long(const char *dir, char **names, int n);
void mode_to_str(mode_t m, char *s);

int main(int argc, char *argv[])
{
    int opt, mode = MODE_DEFAULT, recursive = 0;
    while ((opt = getopt(argc, argv, "lxR")) != -1)
    {
        switch (opt)
        {
        case 'l': mode = MODE_LONG; break;
        case 'x': mode = MODE_HORIZONTAL; break;
        case 'R': recursive = 1; break;
        default:
            fprintf(stderr, "Usage: %s [-l | -x] [-R] [dir ...]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    /* headers ("dir:") are printed with -R or when several dirs are given */
    int show_header = recursive || (argc - optind > 1);

    if (optind == argc)
        do_ls(".", mode, recursive, show_header);
    else
        for (int i = optind; i < argc; i++)
        {
            do_ls(argv[i], mode, recursive, show_header);
            if (i < argc - 1)
                puts("");
        }
    return 0;
}

/* out = dir + "/" + name (no doubled slash if dir already ends in '/') */
void build_path(const char *dir, const char *name, char *out, size_t size)
{
    size_t len = strlen(dir);
    snprintf(out, size, "%s%s%s", dir, (len > 0 && dir[len - 1] == '/') ? "" : "/", name);
}

void do_ls(const char *dir, int mode, int recursive, int show_header)
{
    int n;
    size_t maxlen;
    char **names = read_entries(dir, &n, &maxlen);
    if (names == NULL)
        return;

    if (show_header)
        printf("%s:\n", dir);

    qsort(names, n, sizeof(char *), cmp_names);

    switch (mode)
    {
    case MODE_LONG:       display_long(dir, names, n); break;
    case MODE_HORIZONTAL: display_horizontal(dir, names, n, maxlen); break;
    default:              display_columns(dir, names, n, maxlen); break;
    }

    /* Recursive step: descend into each subdirectory using its FULL path.
       lstat (not stat) so symlinks to directories are not followed -> no loops.
       Base case: a directory with no subdirectories makes no further calls. */
    if (recursive)
    {
        char path[PATH_MAX];
        struct stat st;
        for (int i = 0; i < n; i++)
        {
            if (strcmp(names[i], ".") == 0 || strcmp(names[i], "..") == 0)
                continue;
            build_path(dir, names[i], path, sizeof path);
            if (lstat(path, &st) == -1)
                continue;
            if (S_ISDIR(st.st_mode))
            {
                putchar('\n');
                do_ls(path, mode, recursive, 1);
            }
        }
    }

    free_entries(names, n);
}

/* Read every (non-hidden) entry into a malloc'ed array of strings. */
char **read_entries(const char *dir, int *count, size_t *maxlen)
{
    DIR *dp = opendir(dir);
    if (dp == NULL)
    {
        fprintf(stderr, "ls: cannot open directory '%s': %s\n", dir, strerror(errno));
        return NULL;
    }
    int cap = 16, n = 0;
    size_t max = 0;
    char **names = malloc(cap * sizeof(char *));
    if (names == NULL) { perror("malloc"); exit(EXIT_FAILURE); }

    struct dirent *entry;
    errno = 0;
    while ((entry = readdir(dp)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;
        if (n == cap)
        {
            cap *= 2;
            char **tmp = realloc(names, cap * sizeof(char *));
            if (tmp == NULL) { perror("realloc"); exit(EXIT_FAILURE); }
            names = tmp;
        }
        names[n] = strdup(entry->d_name);
        if (names[n] == NULL) { perror("strdup"); exit(EXIT_FAILURE); }
        if (strlen(names[n]) > max)
            max = strlen(names[n]);
        n++;
    }
    if (errno != 0)
        perror("readdir failed");
    closedir(dp);
    *count = n;
    *maxlen = max;
    return names;
}

/* qsort passes pointers TO the array elements; each element is a char *,
   so the arguments are really char ** hidden behind const void *. */
int cmp_names(const void *a, const void *b)
{
    const char *s1 = *(const char *const *)a;
    const char *s2 = *(const char *const *)b;
    return strcmp(s1, s2);
}

void free_entries(char **names, int n)
{
    for (int i = 0; i < n; i++)
        free(names[i]);
    free(names);
}

static int has_suffix(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

/* Pick a color from st_mode (obtained with lstat so symlinks are detected). */
static const char *color_for(const char *name, mode_t m)
{
    if (S_ISLNK(m)) return COLOR_PINK;
    if (S_ISDIR(m)) return COLOR_BLUE;
    if (S_ISCHR(m) || S_ISBLK(m) || S_ISSOCK(m) || S_ISFIFO(m)) return COLOR_REVERSE;
    if (m & (S_IXUSR | S_IXGRP | S_IXOTH)) return COLOR_GREEN;
    if (has_suffix(name, ".tar") || has_suffix(name, ".gz") || has_suffix(name, ".zip"))
        return COLOR_RED;
    return NULL;
}

/* Print one filename, colored. Prints exactly strlen(name) visible characters. */
void print_name(const char *dir, const char *name)
{
    struct stat st;
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!isatty(STDOUT_FILENO) || lstat(path, &st) == -1)
    {
        fputs(name, stdout);
        return;
    }
    const char *c = color_for(name, st.st_mode);
    if (c)
        printf("%s%s%s", c, name, COLOR_RESET);
    else
        fputs(name, stdout);
}

int get_term_width(void)
{
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1 || w.ws_col == 0)
        return 80; /* fallback when stdout is not a terminal */
    return w.ws_col;
}

/* "Across": fill left to right, wrap when the next item would overflow the line. */
void display_horizontal(const char *dir, char **names, int n, size_t maxlen)
{
    int width = get_term_width();
    int colw = (int)maxlen + 2;
    int pos = 0;

    for (int i = 0; i < n; i++)
    {
        if (pos + colw > width && pos > 0)
        {
            putchar('\n');
            pos = 0;
        }
        print_name(dir, names[i]);
        printf("%*s", colw - (int)strlen(names[i]), "");
        pos += colw;
    }
    if (n > 0)
        putchar('\n');
}

/* "Down then across": item at (row r, column c) is names[c * rows + r]. */
void display_columns(const char *dir, char **names, int n, size_t maxlen)
{
    if (n == 0)
        return;
    int colw = (int)maxlen + 2;
    int cols = get_term_width() / colw;
    if (cols < 1) cols = 1;
    int rows = (n + cols - 1) / cols; /* ceiling division */

    for (int r = 0; r < rows; r++)
    {
        for (int c = 0; c < cols; c++)
        {
            int idx = c * rows + r;
            if (idx >= n)
                break;
            /* pad every item except the last one on the row */
            /* escape codes have zero width, so pad by hand using strlen(name) */
            print_name(dir, names[idx]);
            if ((c + 1) * rows + r < n)
                printf("%*s", colw - (int)strlen(names[idx]), "");
        }
        putchar('\n');
    }
}

/* ---------- helpers for the long listing ---------- */

static int numlen(unsigned long long n)
{
    int len = 1;
    while (n >= 10) { n /= 10; len++; }
    return len;
}

static const char *uname_of(uid_t uid)
{
    static char buf[32];
    struct passwd *pw = getpwuid(uid);
    if (pw) return pw->pw_name;
    snprintf(buf, sizeof buf, "%u", (unsigned)uid);
    return buf;
}

static const char *gname_of(gid_t gid)
{
    static char buf[32];
    struct group *gr = getgrgid(gid);
    if (gr) return gr->gr_name;
    snprintf(buf, sizeof buf, "%u", (unsigned)gid);
    return buf;
}

/* ctime() gives "Sun Oct 11 14:03:22 2026\n"; month/day start at index 4,
   hh:mm at 11, year at 20. Files older than ~6 months show the year instead. */
static void fmt_time(time_t t, char *out, size_t n)
{
    char *c = ctime(&t);
    time_t now = time(NULL);
    if (t > now + 60 || now - t > 15778476)
        snprintf(out, n, "%.6s  %.4s", c + 4, c + 20);
    else
        snprintf(out, n, "%.12s", c + 4);
}

/* Convert st_mode to the familiar "drwxr-xr-x" string (incl. setuid/setgid/sticky) */
void mode_to_str(mode_t m, char *s)
{
    s[0] = S_ISREG(m) ? '-' : S_ISDIR(m) ? 'd' : S_ISLNK(m) ? 'l' :
           S_ISCHR(m) ? 'c' : S_ISBLK(m) ? 'b' : S_ISFIFO(m) ? 'p' :
           S_ISSOCK(m) ? 's' : '?';
    s[1] = (m & S_IRUSR) ? 'r' : '-';
    s[2] = (m & S_IWUSR) ? 'w' : '-';
    s[3] = (m & S_ISUID) ? ((m & S_IXUSR) ? 's' : 'S') : ((m & S_IXUSR) ? 'x' : '-');
    s[4] = (m & S_IRGRP) ? 'r' : '-';
    s[5] = (m & S_IWGRP) ? 'w' : '-';
    s[6] = (m & S_ISGID) ? ((m & S_IXGRP) ? 's' : 'S') : ((m & S_IXGRP) ? 'x' : '-');
    s[7] = (m & S_IROTH) ? 'r' : '-';
    s[8] = (m & S_IWOTH) ? 'w' : '-';
    s[9] = (m & S_ISVTX) ? ((m & S_IXOTH) ? 't' : 'T') : ((m & S_IXOTH) ? 'x' : '-');
    s[10] = '\0';
}

void display_long(const char *dir, char **names, int n)
{
    struct stat st;
    char path[PATH_MAX];
    long total = 0;
    int w_link = 1, w_user = 1, w_group = 1, w_size = 1;

    for (int i = 0; i < n; i++)
    {
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        if (lstat(path, &st) == -1) { perror(path); continue; }
        total += st.st_blocks;
        int l;
        if ((l = numlen(st.st_nlink)) > w_link) w_link = l;
        if ((l = (int)strlen(uname_of(st.st_uid))) > w_user) w_user = l;
        if ((l = (int)strlen(gname_of(st.st_gid))) > w_group) w_group = l;
        if ((l = numlen(st.st_size)) > w_size) w_size = l;
    }
    printf("total %ld\n", total / 2);

    for (int i = 0; i < n; i++)
    {
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        if (lstat(path, &st) == -1) continue;

        char perms[11], tbuf[32];
        mode_to_str(st.st_mode, perms);
        fmt_time(st.st_mtime, tbuf, sizeof tbuf);
        printf("%s %*lu %-*s %-*s %*lld %s ", perms,
               w_link, (unsigned long)st.st_nlink,
               w_user, uname_of(st.st_uid),
               w_group, gname_of(st.st_gid),
               w_size, (long long)st.st_size,
               tbuf);
        print_name(dir, names[i]);
        if (S_ISLNK(st.st_mode))
        {
            char target[PATH_MAX];
            ssize_t len = readlink(path, target, sizeof target - 1);
            if (len != -1) { target[len] = '\0'; printf(" -> %s", target); }
        }
        putchar('\n');
    }
}
