# OS Programming Assignment 02 – Report

## Feature 2 (v1.1.0): Long listing

**Q1. Difference between `stat()` and `lstat()`; when is `lstat()` more appropriate in `ls`?**
`stat()` follows symbolic links and returns metadata of the *target* file, while `lstat()` does not follow them and returns metadata of the *link itself* (type `S_IFLNK`, the link's own size, owner, permissions and time). `ls -l` must show a symlink as `lrwxrwxrwx ... name -> target`, and it must know the entry *is* a link, so `lstat()` is the right call. It is also the safe call for `-R` (a symlink to a directory must not be descended into, otherwise cyclic links cause infinite recursion) and for colouring links.

**Q2. How are bitwise operators and macros used to read `st_mode`?**
`st_mode` packs the file type in the high bits and the permission/special bits in the low 12 bits. The type is extracted by masking with `S_IFMT`: `(st_mode & S_IFMT) == S_IFDIR` (the macro `S_ISDIR(m)` does exactly this). Each permission bit is tested by AND-ing with its mask: `st_mode & S_IRUSR` is non-zero if the owner can read, and likewise `S_IWUSR`, `S_IXUSR`, `S_IRGRP … S_IXOTH`. The special bits `S_ISUID`, `S_ISGID`, `S_ISVTX` are tested the same way and displayed as `s/S` (owner and group execute slots) and `t/T` (others execute slot); lowercase means the execute bit is also set, uppercase means it is not.

## Feature 3 (v1.2.0): Column display

**Q1. Logic of "down then across"; why is a single loop not enough?**
First all names are stored and the longest is found. `colwidth = maxlen + 2`, `cols = terminal_width / colwidth` (at least 1) and `rows = ceil(n / cols)`. Printing then goes row by row: for row `r` the item in column `c` is `names[c * rows + r]` (skipped if the index is `>= n`). Since the array is ordered *down* each column, consecutive array elements end up on different lines. A single loop printing `names[0], names[1], …` would produce left-to-right (row-major) order, and because the terminal can only print line by line, we must visit the elements of one row (which are `rows` apart in the array) before moving to the next row.

**Q2. Purpose of `ioctl`; limits of a fixed 80-column fallback?**
`ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)` asks the terminal driver for its current size (`ws_col`, `ws_row`), so the number of columns adapts to the actual window. With a fixed width of 80, output would wrap badly on narrow windows (lines longer than the terminal break and misalign) and waste space on wide ones (fewer columns than could fit). It also would not react to resizing. The fallback is only used when stdout is not a terminal (pipe/file).

## Feature 4 (v1.3.0): Horizontal display (-x)

**Q1. Complexity: vertical vs horizontal?**
Horizontal is simpler: one pass over the array, tracking the current x-position and printing a newline when the next item would overflow. No row count is needed. "Down then across" needs more pre-calculation: number of columns, then number of rows (ceiling division), and an index formula `c * rows + r` for every cell, because the number of rows must be known *before* printing the first line.

**Q2. How are display modes managed?**
An `enum display_mode { MODE_DEFAULT, MODE_LONG, MODE_HORIZONTAL }` variable is set while parsing options with `getopt()` (`-l` → `MODE_LONG`, `-x` → `MODE_HORIZONTAL`; the last one given wins). It is passed to `do_ls()`, which reads and sorts the entries and then uses a `switch (mode)` to call `display_long()`, `display_horizontal()` or (default) `display_columns()`.

## Feature 5 (v1.4.0): Alphabetical sort

**Q1. Why read all entries before sorting; drawbacks for millions of files?**
`readdir()` returns entries in arbitrary (on-disk/hash) order, and a sort can only decide the first item after having seen *every* item. So all names must be kept in memory first. For directories with millions of files this costs a lot of memory (every name is a `strdup`'d string plus an array pointer), time to read and sort (O(n log n)), and no output appears until everything is read. Real `ls` has the same cost, which is why `ls -U` (unsorted) is faster on huge directories.

**Q2. Purpose and signature of the `qsort()` comparison function; why `const void *`?**
Signature: `int cmp(const void *a, const void *b)`. It returns a negative, zero or positive value if `*a` is respectively less than, equal to, or greater than `*b`. `qsort()` is generic and works on any element type, so it cannot know the type; it just hands the function pointers to two array elements as `const void *`. Our elements are `char *`, so the pointers are really `char **`; inside we cast and dereference once: `strcmp(*(const char * const *)a, *(const char * const *)b)`. `const` promises the comparison will not modify the data.

## Feature 6 (v1.5.0): Colours

**Q1. How do ANSI escape codes work? Code for green?**
The terminal interprets the byte sequence `ESC [ <codes> m` (ESC = `\033`) as a text-attribute command instead of printing it. Everything printed afterwards uses that colour until it is reset by `\033[0m`. Green text: `printf("\033[0;32m%s\033[0m", name);`

**Q2. Which `st_mode` bits show a file is executable?**
`S_IXUSR` (0100) for owner, `S_IXGRP` (0010) for group, `S_IXOTH` (0001) for others. A file is coloured as executable if `st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)` is non-zero (directories are checked first, since they always have x bits).

## Feature 7 (v1.6.0): Recursive listing (-R)

**Q1. What is a base case; what is it in recursive `ls`?**
A base case is the condition under which a recursive function stops calling itself, otherwise it recurses forever. In `ls -R` the base case is a directory that contains no subdirectories: the loop that looks for entries with `S_ISDIR` finds none, so no further `do_ls()` call is made and the function returns. (Using `lstat` and skipping `.`/`..` guarantees the recursion always moves downwards and terminates.)

**Q2. Why must a full path be built before the recursive call?**
`readdir()` only returns the entry *name* (`subdir`), relative to the directory being listed. `opendir("subdir")` is resolved relative to the *current working directory* of the process, not to `parent_dir`, so it would fail (or open the wrong directory if one with the same name exists in the cwd). Building `parent_dir/subdir` gives a path that is valid from wherever the program runs, and the same full path is what `lstat()` needs to detect directories.
