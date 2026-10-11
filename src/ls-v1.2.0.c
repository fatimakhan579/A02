/*
 * ls-v1.2.0 : default output is now multi-column, "down then across",
 *             adapting to the terminal width.   Usage: ls [-l] [dir ...]
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

void do_ls(const char *dir, int long_flag);
char **read_entries(const char *dir, int *count, size_t *maxlen);
void free_entries(char **names, int n);
int get_term_width(void);
void display_columns(char **names, int n, size_t maxlen);
void display_long(const char *dir, char **names, int n);
void mode_to_str(mode_t m, char *s);

int main(int argc, char *argv[])
{
    int opt, long_flag = 0;
    while ((opt = getopt(argc, argv, "l")) != -1)
    {
        switch (opt)
        {
        case 'l': long_flag = 1; break;
        default:
            fprintf(stderr, "Usage: %s [-l] [dir ...]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (optind == argc)
        do_ls(".", long_flag);
    else
        for (int i = optind; i < argc; i++)
        {
            if (argc - optind > 1)
                printf("%s:\n", argv[i]);
            do_ls(argv[i], long_flag);
            if (i < argc - 1)
                puts("");
        }
    return 0;
}

void do_ls(const char *dir, int long_flag)
{
    int n;
    size_t maxlen;
    char **names = read_entries(dir, &n, &maxlen);
    if (names == NULL)
        return;

    if (long_flag)
        display_long(dir, names, n);
    else
        display_columns(names, n, maxlen);

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

void free_entries(char **names, int n)
{
    for (int i = 0; i < n; i++)
        free(names[i]);
    free(names);
}

int get_term_width(void)
{
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1 || w.ws_col == 0)
        return 80; /* fallback when stdout is not a terminal */
    return w.ws_col;
}

/* "Down then across": item at (row r, column c) is names[c * rows + r]. */
void display_columns(char **names, int n, size_t maxlen)
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
            if ((c + 1) * rows + r < n)
                printf("%-*s", colw, names[idx]);
            else
                printf("%s", names[idx]);
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
        printf("%s %*lu %-*s %-*s %*lld %s %s", perms,
               w_link, (unsigned long)st.st_nlink,
               w_user, uname_of(st.st_uid),
               w_group, gname_of(st.st_gid),
               w_size, (long long)st.st_size,
               tbuf, names[i]);
        if (S_ISLNK(st.st_mode))
        {
            char target[PATH_MAX];
            ssize_t len = readlink(path, target, sizeof target - 1);
            if (len != -1) { target[len] = '\0'; printf(" -> %s", target); }
        }
        putchar('\n');
    }
}
