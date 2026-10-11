/*
 * ls-v1.1.0 : adds the -l (long listing) option
 * Usage:  ls [-l] [dir ...]
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

extern int errno;

void do_ls(const char *dir, int long_flag);
void do_ls_long(const char *dir);
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
    if (long_flag)
    {
        do_ls_long(dir);
        return;
    }
    /* original v1.0.0 behaviour */
    struct dirent *entry;
    DIR *dp = opendir(dir);
    if (dp == NULL)
    {
        fprintf(stderr, "Cannot open directory : %s\n", dir);
        return;
    }
    errno = 0;
    while ((entry = readdir(dp)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;
        printf("%s\n", entry->d_name);
    }
    if (errno != 0)
        perror("readdir failed");
    closedir(dp);
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

/* Pass 1: lstat everything to find column widths and the block total.
   Pass 2 (after rewinddir): print each entry using those widths. */
void do_ls_long(const char *dir)
{
    DIR *dp = opendir(dir);
    if (dp == NULL)
    {
        fprintf(stderr, "Cannot open directory : %s\n", dir);
        return;
    }
    struct dirent *entry;
    struct stat st;
    char path[PATH_MAX];
    long total = 0;
    int w_link = 1, w_user = 1, w_group = 1, w_size = 1;

    while ((entry = readdir(dp)) != NULL)
    {
        if (entry->d_name[0] == '.') continue;
        snprintf(path, sizeof path, "%s/%s", dir, entry->d_name);
        if (lstat(path, &st) == -1) { perror(path); continue; }
        total += st.st_blocks;
        int l;
        if ((l = numlen(st.st_nlink)) > w_link) w_link = l;
        if ((l = (int)strlen(uname_of(st.st_uid))) > w_user) w_user = l;
        if ((l = (int)strlen(gname_of(st.st_gid))) > w_group) w_group = l;
        if ((l = numlen(st.st_size)) > w_size) w_size = l;
    }
    printf("total %ld\n", total / 2);

    rewinddir(dp);
    while ((entry = readdir(dp)) != NULL)
    {
        if (entry->d_name[0] == '.') continue;
        snprintf(path, sizeof path, "%s/%s", dir, entry->d_name);
        if (lstat(path, &st) == -1) continue;

        char perms[11], tbuf[32];
        mode_to_str(st.st_mode, perms);
        fmt_time(st.st_mtime, tbuf, sizeof tbuf);
        printf("%s %*lu %-*s %-*s %*lld %s %s", perms,
               w_link, (unsigned long)st.st_nlink,
               w_user, uname_of(st.st_uid),
               w_group, gname_of(st.st_gid),
               w_size, (long long)st.st_size,
               tbuf, entry->d_name);
        if (S_ISLNK(st.st_mode))
        {
            char target[PATH_MAX];
            ssize_t len = readlink(path, target, sizeof target - 1);
            if (len != -1) { target[len] = '\0'; printf(" -> %s", target); }
        }
        putchar('\n');
    }
    closedir(dp);
}
