/*
 * fslib.c -- helpers provided for Programming Assignment 4.
 * GIVEN FILE: you do not need to change this.
 *
 * The disk model, the free-space bookkeeping, the workload runner, the
 * contiguous baseline and all the printing live here. The allocation method
 * itself does not: that is fssim.c, and it is yours.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "fslib.h"

#define LINE 72

static void rule(char c)
{
    for (int i = 0; i < LINE; i++) putchar(c);
    putchar('\n');
}

/* ===================================================================== */
/*  The disk                                                              */
/* ===================================================================== */

void disk_init(Disk *d, int nblocks, int block_size, int ptr_size)
{
    memset(d, 0, sizeof *d);
    d->nblocks    = (nblocks > MAX_BLOCKS) ? MAX_BLOCKS : nblocks;
    d->block_size = block_size;
    d->ptr_size   = ptr_size;
    d->fat_block  = -1;
    for (int b = 0; b < MAX_BLOCKS; b++) {
        d->use[b]   = B_FREE;
        d->owner[b] = -1;
    }
    for (int i = 0; i < MAX_FILES; i++) {
        d->file[i].idx    = i;
        d->file[i].in_use = 0;
    }
}

int disk_read(Disk *d, int block)
{
    if (block < 0 || block >= d->nblocks) {
        fprintf(stderr,
                "\nerror: fs_map() tried to read block %d, which is not on a "
                "%d block disk.\n"
                "       Your mapping has gone off the end of the structure.\n",
                block, d->nblocks);
        exit(1);
    }
    if (d->use[block] == B_FREE) {
        fprintf(stderr,
                "\nerror: fs_map() tried to read block %d, which is FREE.\n"
                "       Your mapping followed a pointer that was never set.\n",
                block);
        exit(1);
    }
    d->accesses++;
    d->total_accesses++;
    if (d->verbose && d->tracing)
        printf("        read block %3d (%s)\n", block,
               d->use[block] == B_META ? "metadata" : "data");
    return block;
}

/* ===================================================================== */
/*  Allocation primitives                                                 */
/* ===================================================================== */

static int take(Disk *d, int block, int owner, BlockUse use)
{
    d->use[block]   = (unsigned char)use;
    d->owner[block] = owner;
    return block;
}

int alloc_data(Disk *d, File *f, int n)
{
    if (n < 0 || n > MAX_FBLOCKS) return -1;
    int found = 0;
    for (int b = 0; b < d->nblocks && found < n; b++)
        if (d->use[b] == B_FREE) found++;
    if (found < n) return -1;

    int k = 0;
    for (int b = 0; b < d->nblocks && k < n; b++)
        if (d->use[b] == B_FREE)
            f->data[k++] = take(d, b, f->idx, B_DATA);
    f->nblocks = n;
    if (d->verbose) {
        printf("      data blocks:");
        for (int i = 0; i < n; i++) printf(" %d", f->data[i]);
        printf("\n");
    }
    return 0;
}

int alloc_data_contiguous(Disk *d, File *f, int n)
{
    if (n < 0 || n > MAX_FBLOCKS) return -1;
    for (int s = 0; s + n <= d->nblocks; s++) {
        int ok = 1;
        for (int b = s; b < s + n; b++)
            if (d->use[b] != B_FREE) { ok = 0; break; }
        if (!ok) continue;
        for (int i = 0; i < n; i++)
            f->data[i] = take(d, s + i, f->idx, B_DATA);
        f->nblocks = n;
        if (d->verbose)
            printf("      data blocks: %d..%d (contiguous)\n", s, s + n - 1);
        return 0;
    }
    return -1;                      /* no run long enough: external fragmentation */
}

int alloc_meta(Disk *d, File *f, int n, int *out)
{
    if (n < 0 || f->nmeta + n > MAX_META) return -1;
    int found = 0;
    for (int b = 0; b < d->nblocks && found < n; b++)
        if (d->use[b] == B_FREE) found++;
    if (found < n) return -1;

    int k = 0;
    for (int b = 0; b < d->nblocks && k < n; b++)
        if (d->use[b] == B_FREE) {
            take(d, b, f->idx, B_META);
            f->meta[f->nmeta++] = b;
            if (out != NULL) out[k] = b;
            k++;
        }
    if (d->verbose && n > 0) {
        printf("      metadata   :");
        for (int i = 0; i < n; i++) printf(" %d", out ? out[i] : -1);
        printf("\n");
    }
    return 0;
}

int fat_blocks(const Disk *d)
{
    int entries_per_block = d->block_size / d->ptr_size;
    if (entries_per_block < 1) entries_per_block = 1;
    return (d->nblocks + entries_per_block - 1) / entries_per_block;
}

int fat_block_for(const Disk *d, int b)
{
    int entries_per_block = d->block_size / d->ptr_size;
    if (entries_per_block < 1) entries_per_block = 1;
    if (d->fat_block < 0) return -1;
    return d->fat_block + b / entries_per_block;
}

int alloc_fat(Disk *d)
{
    if (d->fat_block >= 0) return d->fat_block;

    int need = fat_blocks(d);
    /* The FAT lives at a fixed place at the start of the volume, so take the
     * first run long enough, the way a real volume is laid out. */
    for (int s = 0; s + need <= d->nblocks; s++) {
        int ok = 1;
        for (int b = s; b < s + need; b++)
            if (d->use[b] != B_FREE) { ok = 0; break; }
        if (!ok) continue;
        for (int b = s; b < s + need; b++) {
            d->use[b]   = B_META;
            d->owner[b] = -1;            /* the volume's, not any one file's */
        }
        d->fat_block = s;
        if (d->verbose)
            printf("      FAT at blocks %d..%d (%d blocks, volume-wide)\n",
                   s, s + need - 1, need);
        return s;
    }
    return -1;
}

static void release(Disk *d, File *f)
{
    for (int i = 0; i < f->nblocks; i++) {
        int b = f->data[i];
        if (b >= 0 && b < d->nblocks) { d->use[b] = B_FREE; d->owner[b] = -1; }
    }
    for (int i = 0; i < f->nmeta; i++) {
        int b = f->meta[i];
        if (b >= 0 && b < d->nblocks) { d->use[b] = B_FREE; d->owner[b] = -1; }
    }
    int idx = f->idx;
    memset(f, 0, sizeof *f);
    f->idx = idx;
}

/* ===================================================================== */
/*  The workload                                                          */
/* ===================================================================== */

File *file_by_name(Disk *d, const char *name)
{
    for (int i = 0; i < MAX_FILES; i++)
        if (d->file[i].in_use && strcmp(d->file[i].name, name) == 0)
            return &d->file[i];
    return NULL;
}

static File *free_slot(Disk *d)
{
    for (int i = 0; i < MAX_FILES; i++)
        if (!d->file[i].in_use) return &d->file[i];
    return NULL;
}

/* allocate with the method under test, or with the contiguous baseline */
typedef int (*AllocFn)(Disk *, File *, int);

static int do_workload(Disk *d, const char *path, AllocFn alloc, int *failures)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "error: cannot open workload '%s'\n", path);
        return -1;
    }
    char line[256], op[32], name[24];
    int  n;
    *failures = 0;

    while (fgets(line, sizeof line, fp) != NULL) {
        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '#' || *p == '\0') continue;

        if (sscanf(p, "%31s", op) != 1) continue;

        if (strcmp(op, "create") == 0) {
            if (sscanf(p, "%31s %23s %d", op, name, &n) != 3) {
                fprintf(stderr, "error: bad create line: %s", line);
                fclose(fp); return -1;
            }
            File *f = free_slot(d);
            if (f == NULL) {
                fprintf(stderr, "error: more than %d files\n", MAX_FILES);
                fclose(fp); return -1;
            }
            snprintf(f->name, sizeof f->name, "%s", name);
            f->in_use = 1;
            if (d->verbose) printf("  create %s, %d blocks\n", name, n);
            if (alloc(d, f, n) != 0) {
                printf("  create %-6s %2d blocks : FAILED, not enough room\n",
                       name, n);
                f->in_use = 0;
                release(d, f);
                (*failures)++;
            } else {
                d->nfiles++;
            }
        } else if (strcmp(op, "delete") == 0) {
            if (sscanf(p, "%31s %23s", op, name) != 2) {
                fprintf(stderr, "error: bad delete line: %s", line);
                fclose(fp); return -1;
            }
            File *f = file_by_name(d, name);
            if (f == NULL) {
                fprintf(stderr, "error: delete of unknown file '%s'\n", name);
                fclose(fp); return -1;
            }
            if (d->verbose) printf("  delete %s\n", name);
            release(d, f);
        } else {
            fprintf(stderr, "error: unknown operation '%s'\n", op);
            fclose(fp); return -1;
        }
    }
    fclose(fp);
    return 0;
}

int run_workload(Disk *d, const char *path)
{
    return do_workload(d, path, fs_allocate, &d->failures);
}

/* ===================================================================== */
/*  Measurement                                                           */
/* ===================================================================== */

typedef int (*MapFn)(Disk *, const File *, int);

static int count_with(Disk *d, const File *f, int logical, MapFn map)
{
    d->accesses = 0;
    int got = map(d, f, logical);
    if (got != f->data[logical]) {
        fprintf(stderr,
                "\nerror: fs_map(%s, %d) returned block %d, but logical block "
                "%d of that file\n       is physically at block %d.\n"
                "       The mapping is wrong, so the access count means "
                "nothing.\n",
                f->name, logical, got, logical, f->data[logical]);
        exit(1);
    }
    return d->accesses;
}

int accesses_for(Disk *d, const File *f, int logical)
{
    if (logical < 0 || logical >= f->nblocks) return 0;
    return count_with(d, f, logical, fs_map);
}

static int seq_with(Disk *d, const File *f, MapFn map)
{
    int total = 0;
    for (int i = 0; i < f->nblocks; i++) total += count_with(d, f, i, map);
    return total;
}

int accesses_sequential(Disk *d, const File *f)
{
    return seq_with(d, f, fs_map);
}

/* A fixed pattern, so every method is measured on the same accesses. It
 * depends only on the file length, never on the method or the disk. */
static int rnd_with(Disk *d, const File *f, MapFn map)
{
    if (f->nblocks <= 0) return 0;
    unsigned long seed = 20261127UL;
    int total = 0;
    for (int i = 0; i < f->nblocks; i++) {
        seed = seed * 1103515245UL + 12345UL;
        int which = (int)((seed >> 16) % (unsigned long)f->nblocks);
        total += count_with(d, f, which, map);
    }
    return total;
}

int accesses_random(Disk *d, const File *f)
{
    return rnd_with(d, f, fs_map);
}

/* ===================================================================== */
/*  The contiguous baseline                                               */
/* ===================================================================== */

/*
 * Contiguous allocation, written out in full. This is the method every topic
 * is compared against, and it is given to you so that you only ever have to
 * implement one.
 *
 * A file is a start block and a length, so logical block i is simply
 * start + i, and reaching it costs exactly one disk access however big the
 * file is. ch14 states the mapping as LA / 512 giving Q and R, with
 *     block to be accessed = Q + starting address
 *     displacement into block = R
 * where Q is the logical block number and R the offset inside it.
 */
static int baseline_allocate(Disk *d, File *f, int nblocks)
{
    if (alloc_data_contiguous(d, f, nblocks) != 0) return -1;
    f->start = f->data[0];
    return 0;                       /* no metadata at all: that is the appeal */
}

static int baseline_map(Disk *d, const File *f, int logical)
{
    return disk_read(d, f->start + logical);
}

static void frag_of(const Disk *d, int *free_blocks, int *largest_run)
{
    int fb = 0, run = 0, best = 0;
    for (int b = 0; b < d->nblocks; b++) {
        if (d->use[b] == B_FREE) { fb++; run++; if (run > best) best = run; }
        else run = 0;
    }
    *free_blocks = fb;
    *largest_run = best;
}

Baseline contiguous_baseline(const Disk *d, const char *workload)
{
    Baseline r;
    memset(&r, 0, sizeof r);

    Disk *b = malloc(sizeof *b);
    if (b == NULL) { fprintf(stderr, "out of memory\n"); exit(1); }
    disk_init(b, d->nblocks, d->block_size, d->ptr_size);

    if (do_workload(b, workload, baseline_allocate, &r.failures) != 0) {
        free(b);
        return r;
    }

    for (int i = 0; i < MAX_FILES; i++) {
        File *f = &b->file[i];
        if (!f->in_use) continue;
        r.files_placed++;
        r.data_blocks += f->nblocks;
        r.meta_blocks += f->nmeta;
        r.seq         += seq_with(b, f, baseline_map);
        r.rnd         += rnd_with(b, f, baseline_map);
        r.first       += count_with(b, f, 0, baseline_map);
        r.middle      += count_with(b, f, f->nblocks / 2, baseline_map);
        r.last        += count_with(b, f, f->nblocks - 1, baseline_map);
    }
    frag_of(b, &r.free_blocks, &r.largest_free_run);
    free(b);
    return r;
}

/* ===================================================================== */
/*  Output                                                                */
/* ===================================================================== */

void print_header(const Disk *d, const char *method_name, const char *workload)
{
    rule('=');
    printf("  %s\n", method_name);
    printf("  workload   : %s\n", workload);
    printf("  disk       : %d blocks of %d bytes  (%d KiB total)\n",
           d->nblocks, d->block_size, d->nblocks * d->block_size / 1024);
    printf("  pointer    : %d bytes, so %d pointers fit in one block\n",
           d->ptr_size, d->block_size / d->ptr_size);
    rule('=');
    printf("\n");
}

void print_disk_map(const Disk *d)
{
    printf("Disk map   '.' free   A-P a file's data   a-p that file's "
           "metadata   '#' FAT\n\n");
    const int per = 32;
    for (int row = 0; row * per < d->nblocks; row++) {
        printf("  %3d: ", row * per);
        for (int c = 0; c < per; c++) {
            int b = row * per + c;
            if (b >= d->nblocks) break;
            char ch;
            if (d->use[b] == B_FREE)            ch = '.';
            else if (d->owner[b] < 0)           ch = '#';
            else if (d->use[b] == B_DATA)       ch = (char)('A' + d->owner[b]);
            else                                ch = (char)('a' + d->owner[b]);
            putchar(ch);
            if (c % 8 == 7) putchar(' ');
        }
        printf("\n");
    }
    printf("\n");
}

void print_file_table(Disk *d)
{
    rule('-');
    printf("  %-8s %5s %6s %5s  %s\n", "file", "blks", "start", "meta",
           "disk accesses: first / middle / last   seq   rnd");
    rule('-');

    int tb = 0, tm = 0, ts = 0, tr = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        File *f = &d->file[i];
        if (!f->in_use) continue;
        int first = accesses_for(d, f, 0);
        int mid   = accesses_for(d, f, f->nblocks / 2);
        int last  = accesses_for(d, f, f->nblocks - 1);
        int seq   = accesses_sequential(d, f);
        int rnd   = accesses_random(d, f);
        printf("  %-8s %5d %6d %5d  %10d %8d %6d %7d %5d\n",
               f->name, f->nblocks, f->data[0], f->nmeta,
               first, mid, last, seq, rnd);
        tb += f->nblocks; tm += f->nmeta; ts += seq; tr += rnd;
    }
    rule('-');
    printf("  %-8s %5d %6s %5d  %10s %8s %6s %7d %5d\n",
           "TOTAL", tb, "", tm, "", "", "", ts, tr);
    rule('-');
    printf("\n");
}

void print_fragmentation(const Disk *d)
{
    int fb, run;
    frag_of(d, &fb, &run);
    int used = d->nblocks - fb;

    rule('-');
    printf("  SPACE\n");
    rule('-');
    printf("  Blocks on the disk        : %d\n", d->nblocks);
    printf("  In use                    : %d  (%.1f%%)\n",
           used, 100.0 * used / d->nblocks);
    printf("  Free                      : %d\n", fb);
    printf("  Largest contiguous run    : %d\n", run);
    if (fb > 0)
        printf("  Free but unusable for one contiguous file of %d blocks: %d\n",
               fb, fb - run);
    printf("\n  A method that needs contiguous space can only use the largest\n");
    printf("  run, so %d of those %d free blocks are external fragmentation\n",
           fb - run, fb);
    printf("  as far as contiguous allocation is concerned. A method that can\n");
    printf("  scatter its blocks can use all %d.\n", fb);
    rule('-');
    printf("\n");
}

void print_max_file_size(const Disk *d)
{
    long blks = fs_max_file_blocks(d);
    int  ppb  = d->block_size / d->ptr_size;

    rule('-');
    printf("  ADDRESSING LIMIT OF THIS METHOD\n");
    rule('-');
    printf("  Block size %d bytes, pointer %d bytes, so %d pointers per block.\n",
           d->block_size, d->ptr_size, ppb);
    if (blks < 0) {
        printf("  Maximum file size         : no limit from the method itself\n");
    } else {
        double bytes = (double)blks * d->block_size;
        printf("  Maximum file size         : %ld blocks\n", blks);
        printf("                            = %.0f bytes\n", bytes);
        if (bytes >= 1024.0 * 1024 * 1024 * 1024)
            printf("                            = %.2f TB\n", bytes / (1024.0*1024*1024*1024));
        else if (bytes >= 1024.0 * 1024 * 1024)
            printf("                            = %.2f GB\n", bytes / (1024.0*1024*1024));
        else if (bytes >= 1024.0 * 1024)
            printf("                            = %.2f MB\n", bytes / (1024.0*1024));
        else
            printf("                            = %.2f KB\n", bytes / 1024.0);
    }
    rule('-');
    printf("\n");
}

void print_vs_contiguous(Disk *d, const char *method_name, const char *workload)
{
    Baseline b = contiguous_baseline(d, workload);

    int placed = 0, data = 0, meta = 0, seq = 0, rnd = 0;
    int first = 0, mid = 0, last = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        File *f = &d->file[i];
        if (!f->in_use) continue;
        placed++;
        data += f->nblocks;
        meta += f->nmeta;
        seq  += accesses_sequential(d, f);
        rnd  += accesses_random(d, f);
        first += accesses_for(d, f, 0);
        mid   += accesses_for(d, f, f->nblocks / 2);
        last  += accesses_for(d, f, f->nblocks - 1);
    }
    int fb, run;
    frag_of(d, &fb, &run);
    int fat = (d->fat_block >= 0) ? fat_blocks(d) : 0;

    rule('=');
    printf("  %s  vs  CONTIGUOUS ALLOCATION\n", method_name);
    printf("  %s\n", workload);
    rule('=');
    printf("  %-34s %12s %10s %9s\n", "", "contiguous", "yours", "diff");
    rule('-');
    printf("  %-34s %12d %10d %+9d\n", "files successfully placed",
           b.files_placed, placed, placed - b.files_placed);
    printf("  %-34s %12d %10d %+9d\n", "files that could NOT be placed",
           b.failures, d->failures, d->failures - b.failures);
    printf("  %-34s %12d %10d %+9d\n", "data blocks used",
           b.data_blocks, data, data - b.data_blocks);
    printf("  %-34s %12d %10d %+9d\n", "metadata blocks used",
           b.meta_blocks, meta + fat, meta + fat - b.meta_blocks);
    rule('-');
    printf("  %-34s %12d %10d %+9d\n", "accesses, first block of each file",
           b.first, first, first - b.first);
    printf("  %-34s %12d %10d %+9d\n", "accesses, middle block",
           b.middle, mid, mid - b.middle);
    printf("  %-34s %12d %10d %+9d\n", "accesses, last block",
           b.last, last, last - b.last);
    printf("  %-34s %12d %10d %+9d\n", "accesses, full sequential read",
           b.seq, seq, seq - b.seq);
    printf("  %-34s %12d %10d %+9d\n", "accesses, random pattern",
           b.rnd, rnd, rnd - b.rnd);
    rule('-');
    printf("  %-34s %12d %10d %+9d\n", "largest free run left",
           b.largest_free_run, run, run - b.largest_free_run);
    rule('-');

    if (b.failures > d->failures)
        printf("  Contiguous allocation could not place %d file(s) that your\n"
               "  method placed. That is external fragmentation, and it is the\n"
               "  headline result on this workload.\n",
               b.failures - d->failures);
    else if (d->failures > 0 && d->failures == b.failures)
        printf("  Both methods failed to place %d file(s). On this workload the\n"
               "  disk is genuinely too full, so this is not a fragmentation\n"
               "  result. Say so rather than claiming a win.\n", d->failures);
    else if (d->failures > b.failures)
        printf("  Your method failed to place %d file(s) that contiguous\n"
               "  allocation managed. Find out what your metadata is costing.\n",
               d->failures - b.failures);
    if (placed != b.files_placed) {
        /* The two methods did not store the same set of files, so their
         * access totals are not measuring the same work and must not be
         * compared directly. */
        printf("  The two methods placed DIFFERENT numbers of files (%d against\n"
               "  %d), so the access totals above count different work and are\n"
               "  NOT comparable. Compare accesses per data block instead:\n"
               "      contiguous %.2f   yours %.2f   per block, sequential\n",
               placed, b.files_placed,
               b.data_blocks ? (double)b.seq / b.data_blocks : 0.0,
               data ? (double)seq / data : 0.0);
    } else if (seq > b.seq) {
        printf("  Your method needs %d MORE accesses than contiguous for a full\n"
               "  sequential read, on the same %d data blocks. Say what buys\n"
               "  that cost.\n", seq - b.seq, data);
    } else if (seq < b.seq) {
        printf("  Your method needs %d FEWER accesses than contiguous for a\n"
               "  sequential read.\n", b.seq - seq);
    } else {
        printf("  Your method ties contiguous on sequential reads. Explain what\n"
               "  it costs elsewhere, because nothing here is free.\n");
    }
    rule('=');
    printf("\n");
}

int write_csv(Disk *d, const char *method_name, const char *workload,
              const char *path)
{
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        fprintf(stderr, "error: cannot write '%s'\n", path);
        return -1;
    }
    fprintf(fp, "workload,method,file,data_blocks,meta_blocks,"
                "acc_first,acc_middle,acc_last,acc_sequential,acc_random\n");
    for (int i = 0; i < MAX_FILES; i++) {
        File *f = &d->file[i];
        if (!f->in_use) continue;
        fprintf(fp, "%s,%s,%s,%d,%d,%d,%d,%d,%d,%d\n",
                workload, method_name, f->name, f->nblocks, f->nmeta,
                accesses_for(d, f, 0),
                accesses_for(d, f, f->nblocks / 2),
                accesses_for(d, f, f->nblocks - 1),
                accesses_sequential(d, f),
                accesses_random(d, f));
    }
    fclose(fp);
    return 0;
}
