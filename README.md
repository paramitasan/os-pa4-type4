# Starter Kit: Programming Assignment 4

This is given to you. Copy the whole folder, then edit **`fssim.c`** and
nothing else. There are two short functions to write.

## Build and run

```bash
make                                      # builds fssim, iolab, disksched
./iolab                                   # Part 1: real I/O, measured
./disksched requests/ex2-exercise.txt      # Part 2: the six algorithms
./fssim workloads/w1-basic.txt             # Part 3: 64 blocks of 64 bytes
./fssim workloads/w1-basic.txt -s 4096 -p 4   # ch14's own geometry
./fssim workloads/w2-frag.txt -v           # narrate every access
make test                                  # all five workloads
make clean
```

## The three parts

| Part | What you do | How much work |
|---|---|---|
| 1 | Run `iolab`, answer four questions about what you saw | no code |
| 2 | Run `disksched`, check it against the lecture, answer two questions | no code |
| 3 | Write `fs_allocate()` and `fs_map()` for **your** method | two functions |

**Part 1 is given complete.** `iolab.c` is a finished, commented program.
It writes the same bytes four ways and times them, then shows you a file's
inode number, link count, size against allocated blocks, a sparse file, and
the difference between a hard link and a symbolic link. Compile it, run it,
put the output in your report, explain it. Do not modify it.

**Part 2 is given complete.** `disksched.c` implements all six disk
scheduling algorithms from the W08 lecture and reproduces the answers
published on the slides. Run it on both request files and check.

**Part 3 is the assignment.** Replace the two functions marked TODO.

## What is in the kit

| File | Yours? | What it is |
|---|---|---|
| `fssim.c` | **edit this** | Contiguous allocation (given, the worked example) and your method |
| `iolab.c` | given, complete | Part 1. Read it, run it, do not change it |
| `disksched.c` | given, complete | Part 2. Read it, run it, do not change it |
| `fs.h` | read it | The `Disk` and `File` structures |
| `fslib.c` / `fslib.h` | read it | Disk model, allocators, workload runner, contiguous baseline, all printing |
| `Makefile` | given | Build rules |
| `workloads/w1..w5` | given | The five required workloads |
| `requests/ex1, ex2` | given | The two disk scheduling request files |

## The two functions you write

```c
int fs_allocate(Disk *d, File *f, int nblocks);   /* where the blocks go */
int fs_map(Disk *d, const File *f, int logical);  /* and what it costs to find one */
```

Here is the whole of the given contiguous allocation, so you can see the
shape of what you are being asked to write:

```c
static int contig_allocate(Disk *d, File *f, int nblocks)
{
    if (alloc_data_contiguous(d, f, nblocks) != 0) return -1;
    f->start = f->data[0];
    return 0;
}

static int contig_map(Disk *d, const File *f, int logical)
{
    return disk_read(d, f->start + logical);
}
```

One access to reach any block of any file, and no metadata at all. That is
the standard the other seven methods are measured against, and the thing none
of them can quite match.

### What `fs_allocate()` may call

| Call | What it does |
|---|---|
| `alloc_data(d, f, n)` | `n` free blocks **anywhere**, ascending, into `f->data[]` |
| `alloc_data_contiguous(d, f, n)` | `n` **consecutive** free blocks. Can fail while free space remains |
| `alloc_meta(d, f, n, out)` | `n` blocks as this file's metadata, recorded so they are freed with it |
| `alloc_fat(d)` | the one volume-wide FAT, allocated once (topic 2 only) |

Never write to `d->use[]` or `d->owner[]` yourself.

### What `fs_map()` must call

`disk_read(d, block)` counts one access and returns the block. Call it once
for **every** block your method would genuinely have to fetch, in order,
including the data block itself.

**The access count is what is being marked.** `f->data[logical]` already holds
the right answer and the library checks you against it, stopping with a clear
error if you are wrong. So returning the right block is the easy half; the
mark is in counting honestly. If your method has to read an index block
first, read it.

## Fields on `File` you may use

Use the ones your topic needs and ignore the rest.

| Field | For |
|---|---|
| `data[]`, `nblocks` | filled in by the allocators. The ground truth, and what the library checks you against |
| `meta[]`, `nmeta` | filled in by `alloc_meta()` |
| `start` | contiguous: the first block |
| `head` | linked, FAT: the first block of the chain |
| `index_block` | indexed: the single index block |
| `outer_index` | two-level: the outer index block |
| `index_chain[]`, `nindex` | a chain or array of index blocks |
| `direct[]`, `single`, `dbl`, `triple` | UFS: the inode's pointers |
| `ext_start[]`, `ext_len[]`, `nextents` | extent-based |

## Your self-checks

Three of them, and you should run all three before writing a line:

1. **The kit out of the box.** `./fssim workloads/w1-basic.txt` runs
   contiguous allocation. Every file should show 1 access for its first,
   middle and last block, 0 metadata blocks, and the comparison against the
   baseline should be `+0` on every row, because it is being compared with
   itself.
2. **Part 2 against the slides.** `./disksched requests/ex2-exercise.txt`
   must print, for the head moving downward, FCFS 810, SSTF 219, SCAN 255,
   LOOK 219, C-SCAN 368 and C-LOOK 284. Those are the answers on W08 slides
   20 to 22.
3. **Your addressing limit against ch14.** Run your finished program with
   `-s 4096 -p 4`. If your topic is two-level indexing the limit must come
   out at 1,048,576 blocks, which is ch14's 4 GB. If it is the UFS combined
   scheme it must be 1,074,791,436 blocks, about 4 TB.

## The disk geometry

The default simulated disk is **64 blocks of 64 bytes with 4-byte
pointers**, so 16 pointers fit in a block. Those numbers are deliberately
tiny: they make the addressing limits small enough to actually bite inside a
workload you can read off the screen. A single index block holds only 16
pointers, so a 20-block file genuinely cannot be addressed.

Run with `-s 4096 -p 4` for ch14's own geometry, where one block holds 1,024
pointers and the published figures appear.

## The five workloads

| File | What it is for |
|---|---|
| `w1-basic.txt` | Three files, no deletions. Get your method working here first |
| `w2-frag.txt` | Deletions leave free space in three runs. **Contiguous allocation cannot place the last file at all** |
| `w3-big.txt` | 20-block files: more than one index block, and past topic 3's ceiling |
| `w4-many.txt` | Ten 3-block files, so metadata per file dominates |
| `w5-edge.txt` | One-block files, and files of exactly 16 and exactly 17 blocks |

## Reading the output

The disk map shows where everything went: `.` is free, a capital letter is a
file's data, the matching lowercase letter is that file's metadata, `#` is the
FAT.

```
    0: AAAAAAAA BBBBBCCC CCCCCCCC C.......
   32: ........ ........ ........ ........
```

The per-file table gives the accesses needed for the first, middle and last
block, so a method whose cost grows with the block number is obvious at a
glance.

## A warning about the comparison

If your method places a **different number of files** than contiguous
allocation does, the access totals are counting different work and cannot be
compared. The program detects this, says so, and prints accesses per data
block instead. Use that figure, and do not quote a lower total as a win when
it came from storing fewer files. Topic 3 on `w3-big.txt` is exactly this
trap.
