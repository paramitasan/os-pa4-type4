/*
 * fslib.h -- helpers provided for Programming Assignment 4.
 * GIVEN FILE: you do not need to change this.
 *
 * You write exactly two functions, both in fssim.c:
 *     fs_allocate()   place a file's blocks on the disk
 *     fs_map()        find logical block N, counting the disk accesses
 */

#ifndef FSLIB_H
#define FSLIB_H

#include "fs.h"

/* --- the functions YOU write, in fssim.c ------------------------------- */

/* Place `nblocks` data blocks for file `f`, plus whatever metadata blocks
 * your method needs. Return 0 on success, -1 if the disk cannot hold it.
 * Use alloc_data() / alloc_data_contiguous() / alloc_meta() below; do not
 * touch d->use[] or d->owner[] yourself. */
int fs_allocate(Disk *d, File *f, int nblocks);

/* Return the physical block holding logical block `logical` of file `f`,
 * calling disk_read() once for EVERY block your method would really have to
 * fetch to get there, in order, including the data block itself.
 *
 * The access count is the whole point of this function. The library checks
 * your return value against f->data[logical], so returning the right block
 * while counting the wrong number of accesses is the failure mode to watch
 * for. */
int fs_map(Disk *d, const File *f, int logical);

/* Your topic's own extra output. Called once after the workload. */
void print_topic_extra(Disk *d);

/* Set at the top of fssim.c. Appears in every heading. */
extern const char *METHOD_NAME;

/* The biggest file this method could address, in blocks, on a disk with
 * d->block_size byte blocks and d->ptr_size byte pointers. Ignore the size
 * of the simulated disk: this is the method's own addressing limit, the
 * arithmetic on ch14's mapping slides. Return -1 if it is unlimited. */
long fs_max_file_blocks(const Disk *d);

/* --- setting up --------------------------------------------------------- */

void disk_init(Disk *d, int nblocks, int block_size, int ptr_size);

/* Run a workload file: one operation per line.
 *     create NAME NBLOCKS      delete NAME      extend NAME NBLOCKS
 * Lines starting with '#' and blank lines are ignored.
 * Returns 0 on success, -1 on failure. */
int  run_workload(Disk *d, const char *path);

File *file_by_name(Disk *d, const char *name);

/* --- what your fs_allocate() may call ---------------------------------- */

/* Grab `n` free blocks ANYWHERE on the disk as this file's data blocks, in
 * ascending block order, filling f->data[] and setting f->nblocks.
 * Returns 0 on success, -1 if there are not n free blocks. */
int  alloc_data(Disk *d, File *f, int n);

/* The same, but the n blocks must be CONTIGUOUS. First fit. This is what
 * the contiguous baseline uses, and it is the one that can fail while the
 * disk still has plenty of free space. */
int  alloc_data_contiguous(Disk *d, File *f, int n);

/* Grab `n` free blocks as METADATA for this file: index blocks, inode
 * blocks, extent records. They are recorded in f->meta[] so the library can
 * free them with the file. Writes the block numbers into out[].
 * Returns 0 on success, -1 if there is no room. */
int  alloc_meta(Disk *d, File *f, int n, int *out);

/* How many blocks a File Allocation Table needs on this disk: one entry per
 * disk block, d->ptr_size bytes each, rounded up to whole blocks. */
int  fat_blocks(const Disk *d);

/* Allocate the volume-wide FAT, shared by every file (topic 2). It occupies
 * fat_blocks(d) consecutive-as-possible blocks and belongs to the volume
 * rather than to any one file, so it is NOT freed when a file is deleted.
 * Returns the FIRST FAT block, or -1 if there is no room. Safe to call
 * repeatedly: it allocates only once. */
int  alloc_fat(Disk *d);

/* Which FAT block holds the entry for disk block `b`. Use this in fs_map()
 * to work out whether walking the chain needs a new FAT block read. */
int  fat_block_for(const Disk *d, int b);

/* --- what your fs_map() must call -------------------------------------- */

/* Count one disk access and return `block`. Call it for every block your
 * method would genuinely have to read. It refuses a block that is free or
 * out of range, which catches a mapping that has gone off the rails. */
int  disk_read(Disk *d, int block);

/* --- measurement, all done for you ------------------------------------- */

/* How many disk accesses your fs_map() needs for one logical block. Resets
 * the counter, calls fs_map(), checks the answer against f->data[], and
 * returns the count. Exits with a message if the block is wrong. */
int  accesses_for(Disk *d, const File *f, int logical);

/* Total accesses to read the whole file from logical block 0 upwards. */
int  accesses_sequential(Disk *d, const File *f);

/* Total accesses for a fixed, reproducible pseudo-random access pattern of
 * the same length as the file. The same pattern is used for every method, so
 * the figures are comparable. */
int  accesses_random(Disk *d, const File *f);

/* --- output ------------------------------------------------------------ */

void print_header(const Disk *d, const char *method_name, const char *workload);

/* The disk as a grid: '.' free, a capital letter for each file's data, the
 * matching lowercase letter for that file's metadata, '#' for the FAT. */
void print_disk_map(const Disk *d);

/* One row per file: logical blocks, where the method starts, metadata
 * blocks, and the accesses needed for first / middle / last block. */
void print_file_table(Disk *d);

/* Free blocks, largest contiguous run, and the external fragmentation that
 * contiguous allocation would suffer on this disk. */
void print_fragmentation(const Disk *d);

/* The method's addressing limit, from fs_max_file_blocks(). */
void print_max_file_size(const Disk *d);

/* --- the contiguous baseline ------------------------------------------- */

/* What contiguous allocation WOULD cost on the same workload: it replays the
 * workload on a private disk, so it never disturbs your run. Given to you so
 * that you only ever implement one allocation method.
 *
 * Fills *data_blocks, *meta_blocks, *seq, *rnd and *failures (files that
 * contiguous allocation could not place at all). */
typedef struct {
    int files_placed, failures;
    int data_blocks, meta_blocks;
    int seq, rnd;
    int first, middle, last;    /* accesses, summed over all files */
    int largest_free_run, free_blocks;
} Baseline;

Baseline contiguous_baseline(const Disk *d, const char *workload);

/* Print the standard "your method vs contiguous allocation" table. */
void print_vs_contiguous(Disk *d, const char *method_name, const char *workload);

/* Append the per-file figures to a CSV. Returns 0 on success. */
int  write_csv(Disk *d, const char *method_name, const char *workload,
               const char *path);

#endif /* FSLIB_H */
