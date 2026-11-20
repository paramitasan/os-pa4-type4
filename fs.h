/*
 * fs.h -- the data structures for Programming Assignment 4.
 * GIVEN FILE: read it, you do not need to change it.
 *
 * The Disk is a small simulated block device. The File record holds whatever
 * an allocation method needs to find a file's blocks again: a starting block
 * for contiguous, a chain head for linked, an index block for indexed, and so
 * on. Every method uses only the few fields it needs and ignores the rest.
 *
 * A real file system keeps these fields in a directory entry or an inode on
 * disk. Here they are in memory so you can print them.
 */

#ifndef FS_H
#define FS_H

#define MAX_BLOCKS   256   /* biggest disk the simulation will model      */
#define MAX_FILES     16   /* most files one workload may hold            */
#define MAX_FBLOCKS   64   /* most DATA blocks one file may have          */
#define MAX_META      24   /* most METADATA blocks one file may own       */
#define MAX_EXTENTS   16   /* most extents one file may have (topic 7)    */
#define UFS_DIRECT    12   /* direct pointers in the UFS inode (ch14)     */

/* What a physical block is being used for. */
typedef enum {
    B_FREE = 0,   /* nothing is stored here                                */
    B_DATA,       /* a data block belonging to some file                   */
    B_META        /* a metadata block: an index block, a FAT, an inode map */
} BlockUse;

/* One file. */
typedef struct File {
    char name[24];
    int  in_use;
    int  idx;                   /* this file's slot number, 0 .. MAX_FILES-1 */
    int  nblocks;               /* how many LOGICAL data blocks it has       */

    /* ---- the record every method must keep up to date ------------------ *
     * data[i] is the physical block holding logical block i, in order.
     * alloc_data() fills this in for you. The library uses it to free the
     * file, to draw the disk map, and to CHECK that your fs_map() returns
     * the right answer, so it must be correct and in order.               */
    int  data[MAX_FBLOCKS];

    /* meta[] is every metadata block this file owns. alloc_meta() records
     * them here for you, and the library frees them with the file.        */
    int  meta[MAX_META];
    int  nmeta;

    /* ---- per-method fields: use the ones your topic needs -------------- */
    int  start;                 /* contiguous: first data block             */
    int  head;                  /* linked, FAT: first block of the chain    */
    int  index_block;           /* indexed: the single index block          */
    int  outer_index;           /* two-level: the outer index block         */
    int  index_chain[MAX_META]; /* linked index blocks: the chain of them   */
    int  nindex;                /*   how many index blocks are in the chain */
    int  direct[UFS_DIRECT];    /* UFS: the direct pointers                 */
    int  single, dbl, triple;   /* UFS: the three indirect index blocks     */
    int  ext_start[MAX_EXTENTS];/* extent-based: first block of each extent */
    int  ext_len[MAX_EXTENTS];  /*   and its length                         */
    int  next_ext[MAX_EXTENTS]; /*   block holding the next extent record   */
    int  nextents;
} File;

/* The simulated disk. */
typedef struct Disk {
    int  nblocks;               /* size of the disk, in blocks              */
    int  block_size;            /* bytes per block                          */
    int  ptr_size;              /* bytes per pointer, for the arithmetic    */

    unsigned char use[MAX_BLOCKS];   /* B_FREE / B_DATA / B_META            */
    int           owner[MAX_BLOCKS]; /* which file, or -1                   */

    File file[MAX_FILES];
    int  nfiles;

    /* ---- counters ------------------------------------------------------ */
    int  accesses;              /* disk accesses since the last reset       */
    int  total_accesses;        /* every access in the whole run            */
    int  fat_block;             /* topic 2: the volume-wide FAT, or -1      */
    int  failures;              /* files the workload could not place at all */

    int  verbose;               /* -v: narrate every allocation and access  */
    int  tracing;               /* set while an access trace is wanted      */
} Disk;

#endif /* FS_H */
