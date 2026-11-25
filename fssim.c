/*
 * fssim.c -- Programming Assignment 4, the file YOU edit.
 *
 * Out of the box this runs CONTIGUOUS allocation, which is given to you
 * complete as a worked example and is also the baseline every topic is
 * compared against. Run it, read it, then replace the two functions marked
 * TODO with your own allocation method.
 *
 * You write:
 *     fs_allocate()           where the blocks go
 *     fs_map()                how to find logical block N, and what it costs
 *     fs_max_file_blocks()    the method's addressing limit (one line of
 *                             arithmetic, no loops)
 *     print_topic_extra()     your topic's extra output
 *
 * You should not need to change anything else in the kit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fslib.h"

/* Put your method's name here. It appears in every heading. */
const char *METHOD_NAME = "Indexed Allocation with Linked Index Blocks";

/* ===================================================================== */
/*  THE WORKED EXAMPLE: contiguous allocation                             */
/* ===================================================================== */

/*
 * A contiguous file is a start block and a length. That is the whole of its
 * metadata, and it needs no blocks on disk at all to store it, because it
 * fits in the directory entry.
 *
 * ch14 gives the mapping as LA / 512 producing a quotient Q and remainder R,
 * with "block to be accessed = Q + starting address" and "displacement into
 * block = R". Q is the logical block number and R the offset inside it, so
 * for whole blocks the mapping is just start + logical.
 */
__attribute__((unused))
static int contig_allocate(Disk *d, File *f, int nblocks)
{
    /* The blocks must be consecutive, which is the one thing that can fail
     * while the disk still has plenty of free space. */
    if (alloc_data_contiguous(d, f, nblocks) != 0)
        return -1;
    f->start = f->data[0];
    return 0;
}

__attribute__((unused))
static int contig_map(Disk *d, const File *f, int logical)
{
    /* One access, whatever the file size and whatever the block. */
    return disk_read(d, f->start + logical);
}

/* ===================================================================== */
/*  YOUR METHOD                                                           */
/* ===================================================================== */

/*
 * Place nblocks data blocks for file f, plus whatever metadata your method
 * needs. Return 0 on success, -1 if it will not fit.
 *
 * What you may call:
 *     alloc_data(d, f, n)             n free blocks ANYWHERE, in order
 *     alloc_data_contiguous(d, f, n)  n CONSECUTIVE free blocks
 *     alloc_meta(d, f, n, out)        n blocks as this file's metadata
 *     alloc_fat(d)                    the one volume-wide FAT block
 *
 * All of them record what they did, so the library can free the file and
 * draw the disk map. Never write to d->use[] or d->owner[] yourself.
 *
 * Fields on `f` you may set, whichever your topic needs: start, head,
 * index_block, outer_index, index_chain[] and nindex, direct[], single, dbl,
 * triple, ext_start[] / ext_len[] / next_ext[] and nextents.
 */
int fs_allocate(Disk *d, File *f, int nblocks)
{
    int per = d->block_size / d->ptr_size - 1;
    int index_count = (nblocks + per - 1) / per;    
    if (index_count < 1)
        index_count = 1;

    if (alloc_meta(d, f, index_count, f->index_chain) != 0)
        return -1;
    
    f->nindex = index_count;
    return alloc_data(d, f, nblocks);
}

/*
 * Return the physical block holding logical block `logical`, calling
 * disk_read() once for EVERY block your method would really have to fetch
 * on the way, in order, including the data block itself.
 *
 * f->data[logical] is the right answer, and the library checks you against
 * it. So the mark is not in the return value, it is in the ACCESS COUNT.
 * Walk your structure honestly: if indexed allocation has to read an index
 * block first, read it.
 */
int fs_map(Disk *d, const File *f, int logical)
{
    int per = d->block_size / d->ptr_size - 1;
    int which_index = logical / per;

    for (int i = 0; i <= which_index; i++)
        disk_read(d, f->index_chain[i]);

    return disk_read(d, f->data[logical]);
}

/*
 * The biggest file your method could address, in blocks, given
 * d->block_size byte blocks and d->ptr_size byte pointers. Ignore how small
 * the simulated disk is: this is the method's own limit, the arithmetic on
 * ch14's mapping slides. Return -1 if the method imposes no limit.
 */
long fs_max_file_blocks(const Disk *d)
{
    (void)d;
    /* Contiguous allocation needs only a start and a length, so the method
     * itself imposes no ceiling; the disk does. */
    return -1;
}

/* ===================================================================== */
/*  YOUR TOPIC'S EXTRA OUTPUT                                            */
/* ===================================================================== */

/*
 * Called once after the workload has run. The three calls below are REQUIRED
 * FOR EVERY TOPIC. Add your topic's own extra output where the TODO is,
 * before them.
 */

#define LINE 72
static void rule(char c)
{
    for (int i = 0; i < LINE; i++) putchar(c);
    putchar('\n');
}

void print_topic_extra(Disk *d)
{
    int per = d->block_size / d->ptr_size - 1;

    rule('=');
    puts("A STAIRCASE, NOT A FLAT COST");
    rule('=');

    printf("Each index block holds %d data pointers plus one link, so blocks\n", per);
    printf("0..%d cost 2 accesses, %d..%d cost 3, and so on: one more for\n",
           per - 1, per, 2 * per - 1);
    printf("every %d logical blocks.\n", per);
    rule('-');
    printf("%-12s %6s %6s  %s\n",
           "file", "blocks", "index", "accesses by logical block");
    rule('-');

    for (int i = 0; i < MAX_FILES; i++) {
        const File *f = &d->file[i];
        if (!f->in_use)
            continue;

        printf("%-12s %6d %6d ", f->name, f->nblocks, f->nindex);

        for (int logical = 0; logical < f->nblocks; logical++) {
            if (logical > 0 && logical % per == 0)
                printf(" |");

            printf(" %d", accesses_for(d, f, logical));
        }
        putchar('\n');
    }

    rule('-');
    puts("The '|' marks where one index block ends and the next begins,");
    puts("which is exactly where the cost steps up. Topic 3 is flat at 2");
    puts("but cannot address past 16 blocks; topic 5 is flat at 3 and can");
    puts("address far more. This method trades a growing cost for an");
    puts("unbounded size.\n");

    print_max_file_size(d);
    print_fragmentation(d);
    /* print_vs_contiguous() is called from main, after this. */
    (void)d;
}

/* ===================================================================== */
/*  main: given, you do not need to change it                            */
/* ===================================================================== */

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s <workload-file> [-b BLOCKS] [-s BLOCKSIZE] "
            "[-p PTRSIZE] [-v]\n"
            "  -b BLOCKS     blocks on the simulated disk (default 64, max %d)\n"
            "  -s BLOCKSIZE  bytes per block              (default 64)\n"
            "  -p PTRSIZE    bytes per pointer            (default 4)\n"
            "  -v            narrate every allocation and every disk access\n",
            prog, MAX_BLOCKS);
}

int main(int argc, char *argv[])
{
    if (argc < 2) { usage(argv[0]); return 1; }

    const char *workload = argv[1];
    int nblocks = 64, block_size = 64, ptr_size = 4, verbose = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            nblocks = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            block_size = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            ptr_size = atoi(argv[++i]);
        } else {
            usage(argv[0]); return 1;
        }
    }
    if (nblocks < 1 || nblocks > MAX_BLOCKS || block_size < 8 || ptr_size < 1
        || ptr_size > block_size) {
        fprintf(stderr, "error: implausible disk geometry\n");
        return 1;
    }

    Disk *d = malloc(sizeof *d);
    if (d == NULL) { fprintf(stderr, "out of memory\n"); return 1; }

    disk_init(d, nblocks, block_size, ptr_size);
    d->verbose = verbose;

    print_header(d, METHOD_NAME, workload);

    if (verbose) printf("Running the workload\n\n");
    if (run_workload(d, workload) != 0) { free(d); return 1; }
    if (verbose) printf("\n");

    print_disk_map(d);
    d->tracing = verbose;
    print_file_table(d);
    d->tracing = 0;
    print_topic_extra(d);
    print_vs_contiguous(d, METHOD_NAME, workload);

    if (write_csv(d, METHOD_NAME, workload, "fs_report.csv") == 0)
        printf("Wrote fs_report.csv\n");

    free(d);
    return 0;
}
