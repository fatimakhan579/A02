# Operating Systems – Programming Assignment 02: Report

In this assignment I extended the basic `ls` program step by step (long listing, columns, `-x`, sorting, colors and `-R`). Below are my answers to the report questions of each feature.

---

## Feature 2 (v1.1.0) – Long listing format (`-l`)

### Q1. What is the difference between `stat()` and `lstat()`? When is `lstat()` better in `ls`?

Both functions fill a `struct stat` with information about a file (size, owner, permissions, time and so on). The only difference is what they do with a **symbolic link**:

- `stat()` follows the link and gives me the information of the file the link points to.
- `lstat()` does not follow it and gives me the information of the **link itself**.

Example: if `link1 -> alpha.txt`, then `stat("link1")` tells me about `alpha.txt`, but `lstat("link1")` tells me that `link1` is a link (type `l`).

`ls` should use `lstat()`, because `ls -l` has to print `lrwxrwxrwx ... link1 -> alpha.txt`. With `stat()` I would never find out that it is a link, and it would look like a normal file. I also use `lstat()` in `-R` so that I do not enter a symlink that points to a directory, which could create an endless loop, and in the color code to detect symlinks.

### Q2. How do bitwise operators and macros pull information out of `st_mode`?

`st_mode` is one integer that stores several things at once as bits: the **file type** in the upper bits and the **permissions** in the lower bits. To read one piece I use `&` (AND) with a mask. The result is non-zero only if that bit is set.

- **File type:** `(st_mode & S_IFMT) == S_IFDIR` means it is a directory. `S_IFMT` keeps only the type bits. The macros `S_ISDIR(m)`, `S_ISREG(m)`, `S_ISLNK(m)` do exactly this for me.
- **Permissions:** `st_mode & S_IRUSR` is true if the owner can read. In the same way there are `S_IWUSR`, `S_IXUSR` (owner), `S_IRGRP`, `S_IWGRP`, `S_IXGRP` (group) and `S_IROTH`, `S_IWOTH`, `S_IXOTH` (others).
- **Special bits:** `S_ISUID` (setuid), `S_ISGID` (setgid) and `S_ISVTX` (sticky). In the string they replace the `x`: `s` or `S` for setuid and setgid, `t` or `T` for sticky. Small letter means the execute bit is also on, capital letter means it is off.

In my function `mode_to_str()` I check these bits one by one and write `r`, `w`, `x` or `-` into a 10-character string such as `drwxr-xr-x`.

---

## Feature 3 (v1.2.0) – Column display (down then across)

### Q1. What is the logic of "down then across" printing? Why is one simple loop not enough?

First I read all the names into an array and find the longest name. Then:

1. `colwidth = longest_name + 2` (2 spaces gap)
2. `cols = terminal_width / colwidth` (at least 1)
3. `rows = ceil(n / cols)`, which I calculate as `(n + cols - 1) / cols`

The names are placed **down** the first column, then the second column, and so on. So the name in row `r` and column `c` is `names[c * rows + r]`.

Example: 10 files and 3 columns gives 4 rows.
```
row 0:  names[0]  names[4]  names[8]
row 1:  names[1]  names[5]  names[9]
row 2:  names[2]  names[6]
row 3:  names[3]  names[7]
```

A single loop `for i in 0..n` would print `names[0], names[1], names[2]...` on one row, so the order would be left-to-right, not top-to-bottom. The terminal prints one line at a time, so for each line I must jump through the array in steps of `rows`. That needs two loops (rows outside, columns inside).

### Q2. What is `ioctl` used for here? What are the limits of a fixed 80-column width?

`ioctl(STDOUT_FILENO, TIOCGWINSZ, &w)` asks the terminal for its size, and `w.ws_col` is the number of columns of the current window. My program uses it to decide how many columns of names can fit.

If I only used a fixed 80:
- on a narrow window the lines would be longer than the screen and wrap in the middle, which breaks the layout;
- on a wide window I would use only 80 columns and waste the rest of the space;
- it would not follow the real window size when I resize the terminal.

So I use 80 only as a fallback, when `ioctl` fails (for example when the output goes to a file or a pipe).

---

## Feature 4 (v1.3.0) – Horizontal display (`-x`)

### Q1. Which is harder, "down then across" or "across"? Which needs more pre-calculation?

"Down then across" is harder. Before I print the first line I must already know the number of columns **and** the number of rows, and then I need the formula `c * rows + r` for every item.

"Across" (`-x`) is simpler. I only need the column width. I go through the array once, print each name padded to the column width, and keep a counter `pos` of how far I am on the line. If `pos + colwidth` would go past the terminal width, I print a newline and set `pos = 0`. I never need to know the number of rows.

### Q2. How did I handle the different display modes?

I made an `enum` with three values: `MODE_DEFAULT`, `MODE_LONG` and `MODE_HORIZONTAL`. In `main()` the `getopt()` loop sets this variable (`-l` gives long, `-x` gives horizontal; if both are given the last one wins). The mode is passed to `do_ls()`. After `do_ls()` has read and sorted the names, it uses a `switch (mode)` to call the right function: `display_long()`, `display_horizontal()`, or `display_columns()` for the default case. The reading and sorting code is the same for all modes, only the printing function changes.

---

## Feature 5 (v1.4.0) – Alphabetical sort

### Q1. Why must all entries be read into memory before sorting? What is the problem with millions of files?

`readdir()` gives names in no particular order (the order the file system stores them). To know which name comes first alphabetically I have to look at **all** of them, so I cannot print while reading. That is why I store them in a dynamic array first and sort afterwards.

For a directory with millions of files this is a problem because:
- it uses a lot of memory (each name is allocated with `strdup`, plus the array of pointers);
- sorting takes extra time (about n log n comparisons);
- nothing is shown on the screen until everything is read and sorted.

(The real `ls` has the same cost, which is why it has the `-U` option that skips sorting.)

### Q2. What is the comparison function of `qsort()`? Why does it take `const void *`?

The signature is:
```c
int cmp_names(const void *a, const void *b);
```
It must return a negative number if the first item should come first, `0` if they are equal, and a positive number if the first should come after the second.

`qsort()` is a general function that can sort any kind of array, so it does not know my data type. That is why it hands me the elements as `const void *` (pointer to anything). `const` means that I must not change the data while comparing.

My array is an array of strings (`char *`), so `a` and `b` are really pointers to a `char *`, that is `char **`. Inside the function I first cast them and then dereference once to get the strings:
```c
const char *s1 = *(const char * const *)a;
const char *s2 = *(const char * const *)b;
return strcmp(s1, s2);
```
`strcmp()` gives the negative / zero / positive result that `qsort` expects.

---

## Feature 6 (v1.5.0) – Colorized output

### Q1. How do ANSI escape codes make colors? What is the code for green?

An ANSI escape code is a special sequence of characters that starts with the ESC character (`\033`). The terminal does not print it; it reads it as a command to change the text style. Everything printed after it uses that color until I send the reset code `\033[0m`.

The format is `ESC [ <code> m`. For green text:
```c
printf("\033[0;32m%s\033[0m", filename);
```
`0;32` means normal style with green color, and `\033[0m` puts the terminal back to default. Other codes I used are `34` (blue), `31` (red), `35` (pink/magenta) and `7` (reverse video).

One thing I noticed: these characters take no space on the screen, so I cannot use `%-20s` to align colored names (printf would count the invisible characters). I print the colored name first and then add the spaces myself using `colwidth - strlen(name)`.

### Q2. Which bits of `st_mode` tell me that a file is executable?

There are three execute bits:
- `S_IXUSR` – executable by the **owner**
- `S_IXGRP` – executable by the **group**
- `S_IXOTH` – executable by **others**

A file is executable if any of them is set:
```c
if (st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) { /* green */ }
```
Directories always have execute bits too, so I check for directories first and only then for executable files.

---

## Feature 7 (v1.6.0) – Recursive listing (`-R`)

### Q1. What is a base case? What is it in my recursive `ls`?

A base case is the situation where a recursive function stops calling itself and just returns. Without it the function would call itself forever.

In my `do_ls()` the recursive call happens only when an entry is a directory. So the base case is a directory that has **no subdirectories**: the loop finds nothing to go into, makes no more calls, and the function returns. Since every call goes one level deeper into the tree (and I skip `.` and `..`, and use `lstat` so I do not follow symlinks), the recursion always ends.

### Q2. Why do I need the full path before the recursive call? What if I call `do_ls("subdir")`?

`readdir()` only gives the **name** of an entry (for example `subdir`), not where it is. When I call `opendir("subdir")`, the system looks for it in the **current working directory** of my program, not inside the parent directory I was listing.

Example: I run `./ls -R /home/user/project`, and it finds `src` inside it. If I call `do_ls("src")`, it looks for `./src` where I started the program, which is a different place or does not exist, so it fails or lists the wrong folder. If I build the full path first (`/home/user/project/src`) it works from anywhere. I build it with `snprintf` (parent + `/` + name) in `build_path()`. The same full path is also needed for `lstat()` to check if the entry is a directory.
