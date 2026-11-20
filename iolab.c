/*
 * iolab.c -- Programming Assignment 4, Part 1.
 * GIVEN FILE, COMPLETE. You do not write any of this.
 *
 * Your job for Part 1 is to compile it, run it, put the output in your
 * report, and answer the four questions in your ASSIGNMENT.md. Read the code
 * first: every number it prints is explained by a comment.
 *
 * What it demonstrates, all of it from the W08 and ch13 material:
 *
 *   1. Buffering. The same bytes written four ways, from one system call per
 *      byte up to one per 64 KiB. This is the kernel I/O subsystem's
 *      buffering (W0809 slide 54) and why I/O performance (slide 63) is
 *      dominated by the number of system calls, not the number of bytes.
 *   2. File metadata: the inode number, the link count, and the difference
 *      between a file's SIZE and the space actually ALLOCATED to it
 *      (ch13, File Attributes).
 *   3. Sparse files. A file can claim to be megabytes long while occupying
 *      almost no blocks, because a file is a MAP from logical block to
 *      physical block, not a run of bytes. That is the same indirection the
 *      allocation methods in Part 3 implement.
 *   4. Hard links against symbolic links: one inode with two names, versus
 *      a second file whose contents are a path (ch13, Acyclic-Graph
 *      Directories).
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

/* How much to write. Chosen at run time: see calibrate() below. The
 * one-byte-at-a-time case costs one system call per byte, and a system call
 * is cheap on a native Linux file system but very expensive on a Windows
 * drive mounted into WSL, so a fixed size would either be too small to
 * measure or would appear to hang. */
static long PAYLOAD = 256 * 1024;
#define PAYLOAD_MIN  (2 * 1024)
#define PAYLOAD_MAX  (256 * 1024)
#define BUDGET_MS    1500.0        /* keep the slowest case under 1.5 s */
#define BIGBUF    (64 * 1024)
#define DATFILE   "iolab-out.dat"
#define SPARSEFILE "iolab-sparse.dat"
#define HARDLINK  "iolab-hard.lnk"
#define SYMLINK_  "iolab-sym.lnk"

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

/* ---------------------------------------------------------------------- *
 *  Four ways to write the same 256 KiB
 * ---------------------------------------------------------------------- */

/*
 * Time a few hundred one-byte writes, then pick a payload whose
 * one-byte-at-a-time case will finish inside BUDGET_MS. Every one of the
 * four methods then writes that same payload, so the comparison stays fair.
 */
static double calibrate(void)
{
    const int probe = 512;
    int fd = open(DATFILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror(DATFILE); exit(1); }
    double t0 = now_ms();
    for (int i = 0; i < probe; i++)
        if (write(fd, "x", 1) != 1) { perror("write"); exit(1); }
    close(fd);
    double per_ms = (now_ms() - t0) / probe;

    long want = (per_ms > 0.0) ? (long)(BUDGET_MS / per_ms) : PAYLOAD_MAX;
    long p = PAYLOAD_MIN;
    while (p * 2 <= want && p * 2 <= PAYLOAD_MAX) p *= 2;
    PAYLOAD = p;

    printf("\n  Calibrating against this file system\n");
    printf("  --------------------------------------------------------------"
           "--------------\n");
    printf("    cost of one write() system call   %.3f us\n", per_ms * 1000.0);
    printf("    payload chosen                    %ld bytes (%ld KiB)\n",
           PAYLOAD, PAYLOAD / 1024);
    if (per_ms * 1000.0 > 20.0) {
        printf("\n    NOTE: that is very slow for a single system call. You are\n");
        printf("    almost certainly running on a Windows drive mounted into\n");
        printf("    WSL (a /mnt/c path), where every system call crosses\n");
        printf("    between Linux and Windows. On a native Linux file system\n");
        printf("    the same call costs well under a microsecond.\n");
        printf("    The comparison below is still valid, but for figures that\n");
        printf("    reflect the kernel rather than the mount, copy this kit to\n");
        printf("    a native Linux path and run it there. Report both if you\n");
        printf("    can: the gap is itself the lesson.\n");
    }
    return per_ms;
}

static void row(const char *how, double ms, long syscalls, const char *note)
{
    char calls[24];
    if (syscalls < 0) snprintf(calls, sizeof calls, "%s", "see note");
    else              snprintf(calls, sizeof calls, "%ld", syscalls);
    printf("    %-34s %8.2f ms  %12s  %s\n", how, ms, calls, note);
}

static void experiment(void)
{
    char *buf = malloc(BIGBUF);
    if (buf == NULL) { perror("malloc"); exit(1); }
    memset(buf, 'x', BIGBUF);

    printf("\n  Writing %ld bytes (%ld KiB) four different ways\n",
           PAYLOAD, PAYLOAD / 1024);
    printf("  --------------------------------------------------------------"
           "--------------\n");
    printf("    %-34s %11s  %12s  %s\n", "method", "elapsed", "write() calls",
           "");
    printf("  --------------------------------------------------------------"
           "--------------\n");

    /* 1. stdio, one character at a time, with stdio's own default buffer.
     *    One fputc() per byte, but the C library batches them into a buffer
     *    and issues roughly PAYLOAD/BUFSIZ real write() calls. */
    {
        FILE *f = fopen(DATFILE, "w");
        if (f == NULL) { perror(DATFILE); exit(1); }
        double t0 = now_ms();
        for (long i = 0; i < PAYLOAD; i++) fputc('x', f);
        fclose(f);
        double t1 = now_ms();
        char note[96];
        snprintf(note, sizeof note, "~%ld, hidden inside stdio (BUFSIZ=%d)",
                 PAYLOAD / BUFSIZ > 0 ? PAYLOAD / BUFSIZ : 1, BUFSIZ);
        row("stdio fputc, default buffer", t1 - t0, -1, note);
    }

    /* 2. stdio again, but with a 64 KiB buffer instead of BUFSIZ. Same
     *    number of fputc() calls, far fewer real write() calls. */
    {
        FILE *f = fopen(DATFILE, "w");
        if (f == NULL) { perror(DATFILE); exit(1); }
        setvbuf(f, buf, _IOFBF, BIGBUF);
        double t0 = now_ms();
        for (long i = 0; i < PAYLOAD; i++) fputc('x', f);
        fclose(f);
        double t1 = now_ms();
        char note[96];
        snprintf(note, sizeof note, "~%ld, hidden inside stdio (64 KiB buffer)",
                 PAYLOAD / BIGBUF > 0 ? PAYLOAD / BIGBUF : 1);
        row("stdio fputc, 64 KiB buffer", t1 - t0, -1, note);
        /* setvbuf borrowed our buffer; fclose has released it. */
        memset(buf, 'x', BIGBUF);
    }

    /* 3. The raw system call, one byte at a time. No buffering anywhere, so
     *    this really is one write() per byte: PAYLOAD system calls, each one
     *    a trip into the kernel. */
    {
        int fd = open(DATFILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { perror(DATFILE); exit(1); }
        double t0 = now_ms();
        long calls = 0;
        for (long i = 0; i < PAYLOAD; i++) {
            if (write(fd, "x", 1) != 1) { perror("write"); exit(1); }
            calls++;
        }
        close(fd);
        double t1 = now_ms();
        row("write() one byte at a time", t1 - t0, calls, "one per byte");
    }

    /* 4. The raw system call with a big buffer. The same bytes, the same
     *    kernel, four system calls instead of 262,144. */
    {
        int fd = open(DATFILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { perror(DATFILE); exit(1); }
        double t0 = now_ms();
        long calls = 0, left = PAYLOAD;
        while (left > 0) {
            long chunk = (left > BIGBUF) ? BIGBUF : left;
            ssize_t n = write(fd, buf, (size_t)chunk);
            if (n < 0) { perror("write"); exit(1); }
            left -= n;
            calls++;
        }
        close(fd);
        double t1 = now_ms();
        row("write() in 64 KiB blocks", t1 - t0, calls, "one per 64 KiB");
    }

    printf("  --------------------------------------------------------------"
           "--------------\n");
    printf("    The bytes written are identical in all four cases. Only the\n");
    printf("    number of trips into the kernel changed. That is what the\n");
    printf("    kernel I/O subsystem's buffering is for.\n");
    printf("    Check the real counts yourself:  strace -c -e trace=write "
           "./iolab\n");
    free(buf);
}

/* ---------------------------------------------------------------------- *
 *  What the file system thinks the file is
 * ---------------------------------------------------------------------- */

static void show_stat(const char *path, const char *label)
{
    struct stat st;
    if (lstat(path, &st) != 0) { perror(path); return; }

    printf("    %-22s %-16s\n", label, path);
    printf("        inode number        %lu\n",  (unsigned long)st.st_ino);
    printf("        hard links          %lu\n",  (unsigned long)st.st_nlink);
    printf("        size (st_size)      %lld bytes\n", (long long)st.st_size);
    printf("        allocated           %lld blocks of 512 B = %lld bytes\n",
           (long long)st.st_blocks, (long long)st.st_blocks * 512);
    printf("        block size hint     %ld bytes\n", (long)st.st_blksize);
    if (S_ISLNK(st.st_mode)) {
        char target[256];
        ssize_t n = readlink(path, target, sizeof target - 1);
        if (n > 0) { target[n] = '\0'; printf("        symlink target      %s\n", target); }
    }
    printf("\n");
}

static void metadata(void)
{
    printf("\n  File metadata: what the directory entry and inode hold\n");
    printf("  --------------------------------------------------------------"
           "--------------\n");
    show_stat(DATFILE, "the file we wrote");

    /* A sparse file. Seek a long way past the end and write one byte. The
     * file now CLAIMS to be 8 MiB long, but the file system only allocated
     * the blocks that were actually written to. Everything in between is a
     * hole: logical blocks with no physical block behind them.
     *
     * This is exactly the indirection the allocation methods in Part 3
     * implement. A contiguous file cannot do this at all. */
    {
        int fd = open(SPARSEFILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { perror(SPARSEFILE); exit(1); }
        if (lseek(fd, 8L * 1024 * 1024 - 1, SEEK_SET) < 0) { perror("lseek"); exit(1); }
        if (write(fd, "", 1) != 1) { perror("write"); exit(1); }
        close(fd);
    }
    printf("    A SPARSE FILE: seek to 8 MiB - 1, write a single byte\n\n");
    show_stat(SPARSEFILE, "sparse file");
    printf("        ^ it reports 8 MiB of size while occupying almost no\n");
    printf("          blocks. The gap is a HOLE: logical blocks with no\n");
    printf("          physical block behind them. Reading a hole returns\n");
    printf("          zeroes without any disk access at all.\n\n");

    /* A hard link is a second directory entry pointing at the SAME inode.
     * A symbolic link is a different file whose contents are a path. */
    unlink(HARDLINK);
    unlink(SYMLINK_);
    if (link(DATFILE, HARDLINK) != 0) perror("link");
    if (symlink(DATFILE, SYMLINK_) != 0) perror("symlink");

    printf("    HARD LINK against SYMBOLIC LINK\n\n");
    show_stat(DATFILE,  "original");
    show_stat(HARDLINK, "hard link");
    show_stat(SYMLINK_, "symbolic link");
    printf("        ^ the hard link has the SAME inode number as the original\n");
    printf("          and the link count of both went up to 2: one file, two\n");
    printf("          names. The symbolic link has its OWN inode, a size of\n");
    printf("          just the length of the path it holds, and it would\n");
    printf("          dangle if the original were deleted.\n");
}

/* ---------------------------------------------------------------------- */

int main(void)
{
    printf("======================================================="
           "=================\n");
    printf("  iolab: real I/O and real file metadata, measured\n");
    printf("======================================================="
           "=================\n");

    calibrate();
    experiment();
    metadata();

    printf("\n======================================================="
           "=================\n");
    printf("  Part 3 simulates the mapping from logical block to physical\n");
    printf("  block. The sparse file above is that mapping in the real\n");
    printf("  kernel, and the reason a file is not just a run of bytes.\n");
    printf("======================================================="
           "=================\n");
    return 0;
}
