/*
 * disksched.c -- Programming Assignment 4, Part 2.
 * GIVEN FILE, COMPLETE. You do not write any of this.
 *
 * All six disk scheduling algorithms from the W08 lecture, run on a request
 * file. Your job is to compile it, run it on both supplied request files,
 * check the totals against the answers published on the lecture slides, and
 * answer the two questions in your ASSIGNMENT.md.
 *
 * The algorithms (W0809 slides 13 to 22):
 *
 *   FCFS     service requests in arrival order, no reordering
 *   SSTF     always go to the nearest pending request
 *   SCAN     sweep to the physical end of the disk, then reverse
 *   LOOK     sweep only as far as the furthest request, then reverse
 *   C-SCAN   sweep to the physical end, jump to the far end, sweep again
 *   C-LOOK   sweep to the furthest request, jump to the furthest the other
 *            way, sweep again
 *
 * DIRECTION MATTERS. SCAN, LOOK, C-SCAN and C-LOOK all give different
 * answers depending on which way the head was already moving, and the
 * lecture slides are not consistent about it: slide 16 runs SCAN downward
 * while slide 17 runs C-SCAN upward on the same request queue. This program
 * therefore reports BOTH directions for every algorithm, and that difference
 * is one of the things your report has to explain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_REQ 64

typedef struct {
    int cylinders;          /* the disk has cylinders 0 .. cylinders-1 */
    int head;               /* where the arm is now                    */
    int previous;           /* where it was before, or -1 if not given */
    int req[MAX_REQ];
    int n;
    char name[64];
} Job;

/* A serviced path, for printing. */
typedef struct {
    int  path[MAX_REQ + 4];
    int  len;
    int  total;             /* total head movement, counting any jump   */
    int  total_nojump;      /* the same, not counting the wrap-around   */
} Result;

static void finish(Result *r, int jump_from, int jump_to)
{
    r->total = r->total_nojump = 0;
    for (int i = 1; i < r->len; i++) {
        int d = abs(r->path[i] - r->path[i - 1]);
        r->total += d;
        /* The wrap-around of C-SCAN and C-LOOK is a seek across the whole
         * disk that services nothing. Some textbooks count it and some do
         * not, so report both. The lecture gives both numbers too. */
        if (!(r->path[i - 1] == jump_from && r->path[i] == jump_to))
            r->total_nojump += d;
    }
}

static int cmp_up(const void *a, const void *b)   { return *(const int*)a - *(const int*)b; }
static int cmp_down(const void *a, const void *b) { return *(const int*)b - *(const int*)a; }

/* split the requests into those at or below the head, and those above */
static void split(const Job *j, int *below, int *nb, int *above, int *na)
{
    *nb = *na = 0;
    for (int i = 0; i < j->n; i++) {
        if (j->req[i] <= j->head) below[(*nb)++] = j->req[i];
        else                      above[(*na)++] = j->req[i];
    }
    qsort(below, (size_t)*nb, sizeof(int), cmp_down);   /* nearest first */
    qsort(above, (size_t)*na, sizeof(int), cmp_up);
}

static void push(Result *r, int c) { r->path[r->len++] = c; }

static Result fcfs(const Job *j, int down)
{
    (void)down;
    Result r = {{0}, 0, 0, 0};
    push(&r, j->head);
    for (int i = 0; i < j->n; i++) push(&r, j->req[i]);
    finish(&r, -1, -1);
    return r;
}

static Result sstf(const Job *j, int down)
{
    (void)down;
    Result r = {{0}, 0, 0, 0};
    int rem[MAX_REQ], n = j->n;
    memcpy(rem, j->req, sizeof(int) * (size_t)n);
    int cur = j->head;
    push(&r, cur);
    while (n > 0) {
        int best = 0;
        for (int i = 1; i < n; i++)
            if (abs(rem[i] - cur) < abs(rem[best] - cur) ||
                (abs(rem[i] - cur) == abs(rem[best] - cur) && rem[i] < rem[best]))
                best = i;
        cur = rem[best];
        push(&r, cur);
        rem[best] = rem[--n];
    }
    finish(&r, -1, -1);
    return r;
}

static Result scan_(const Job *j, int down)
{
    Result r = {{0}, 0, 0, 0};
    int below[MAX_REQ], above[MAX_REQ], nb, na;
    split(j, below, &nb, above, &na);
    push(&r, j->head);
    if (down) {
        for (int i = 0; i < nb; i++) push(&r, below[i]);
        push(&r, 0);                                /* to the physical end */
        for (int i = 0; i < na; i++) push(&r, above[i]);
    } else {
        for (int i = 0; i < na; i++) push(&r, above[i]);
        push(&r, j->cylinders - 1);
        for (int i = 0; i < nb; i++) push(&r, below[i]);
    }
    finish(&r, -1, -1);
    return r;
}

static Result look(const Job *j, int down)
{
    Result r = {{0}, 0, 0, 0};
    int below[MAX_REQ], above[MAX_REQ], nb, na;
    split(j, below, &nb, above, &na);
    push(&r, j->head);
    if (down) {
        for (int i = 0; i < nb; i++) push(&r, below[i]);   /* only as far as needed */
        for (int i = 0; i < na; i++) push(&r, above[i]);
    } else {
        for (int i = 0; i < na; i++) push(&r, above[i]);
        for (int i = 0; i < nb; i++) push(&r, below[i]);
    }
    finish(&r, -1, -1);
    return r;
}

static Result cscan(const Job *j, int down)
{
    Result r = {{0}, 0, 0, 0};
    int below[MAX_REQ], above[MAX_REQ], nb, na;
    split(j, below, &nb, above, &na);
    int from, to;
    push(&r, j->head);
    if (down) {
        for (int i = 0; i < nb; i++) push(&r, below[i]);
        push(&r, 0); push(&r, j->cylinders - 1);
        from = 0; to = j->cylinders - 1;
        /* having wrapped to the top, the head keeps going DOWN */
        for (int i = na - 1; i >= 0; i--) push(&r, above[i]);
    } else {
        for (int i = 0; i < na; i++) push(&r, above[i]);
        push(&r, j->cylinders - 1); push(&r, 0);
        from = j->cylinders - 1; to = 0;
        /* having wrapped to the bottom, the head keeps going UP */
        for (int i = nb - 1; i >= 0; i--) push(&r, below[i]);
    }
    finish(&r, from, to);
    return r;
}

static Result clook(const Job *j, int down)
{
    Result r = {{0}, 0, 0, 0};
    int below[MAX_REQ], above[MAX_REQ], nb, na;
    split(j, below, &nb, above, &na);
    int from = -1, to = -1;
    push(&r, j->head);
    if (down) {
        for (int i = 0; i < nb; i++) push(&r, below[i]);
        if (nb > 0 && na > 0) { from = below[nb - 1]; to = above[na - 1]; }
        for (int i = na - 1; i >= 0; i--) push(&r, above[i]);
    } else {
        for (int i = 0; i < na; i++) push(&r, above[i]);
        if (na > 0 && nb > 0) { from = above[na - 1]; to = below[nb - 1]; }
        for (int i = nb - 1; i >= 0; i--) push(&r, below[i]);
    }
    finish(&r, from, to);
    return r;
}

typedef Result (*Alg)(const Job *, int);
static const struct { const char *name; Alg fn; int wraps; } ALGS[] = {
    { "FCFS",   fcfs,  0 },
    { "SSTF",   sstf,  0 },
    { "SCAN",   scan_, 0 },
    { "LOOK",   look,  0 },
    { "C-SCAN", cscan, 1 },
    { "C-LOOK", clook, 1 },
};
#define NALGS ((int)(sizeof ALGS / sizeof ALGS[0]))

/* ---------------------------------------------------------------------- */

static int load(Job *j, const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) { fprintf(stderr, "error: cannot open '%s'\n", path); return -1; }

    j->cylinders = 200; j->head = 0; j->previous = -1; j->n = 0;
    const char *base = strrchr(path, '/');
    snprintf(j->name, sizeof j->name, "%s", base ? base + 1 : path);

    char line[512], key[32];
    while (fgets(line, sizeof line, f) != NULL) {
        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '#' || *p == '\0') continue;
        if (sscanf(p, "%31s", key) != 1) continue;

        if (strcmp(key, "cylinders") == 0)      sscanf(p, "%31s %d", key, &j->cylinders);
        else if (strcmp(key, "head") == 0)      sscanf(p, "%31s %d", key, &j->head);
        else if (strcmp(key, "previous") == 0)  sscanf(p, "%31s %d", key, &j->previous);
        else if (strcmp(key, "requests") == 0) {
            char *q = p + strlen("requests");
            char *tok = strtok(q, " \t\n,");
            while (tok != NULL && j->n < MAX_REQ) {
                j->req[j->n++] = atoi(tok);
                tok = strtok(NULL, " \t\n,");
            }
        }
    }
    fclose(f);
    if (j->n == 0) { fprintf(stderr, "error: '%s' has no requests\n", path); return -1; }
    return 0;
}

static void print_path(const Result *r)
{
    for (int i = 0; i < r->len; i++)
        printf("%s%d", i ? " -> " : "", r->path[i]);
    printf("\n");
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <request-file>\n", argv[0]);
        return 1;
    }
    Job j;
    if (load(&j, argv[1]) != 0) return 1;

    /* If the previous position is given, it tells us which way the arm is
     * already moving, which is what SCAN and friends continue doing. */
    int down = 1;
    const char *why = "assumed downward (no 'previous' given)";
    if (j.previous >= 0) {
        down = (j.previous > j.head);
        why  = down ? "downward, because the previous request was higher"
                    : "upward, because the previous request was lower";
    }

    printf("========================================================="
           "===============\n");
    printf("  Disk scheduling: %s\n", j.name);
    printf("========================================================="
           "===============\n");
    printf("  cylinders        : 0 to %d\n", j.cylinders - 1);
    printf("  head starts at   : %d\n", j.head);
    if (j.previous >= 0) printf("  previous request : %d\n", j.previous);
    printf("  current direction: %s\n", why);
    printf("  request queue    :");
    for (int i = 0; i < j.n; i++) printf(" %d", j.req[i]);
    printf("\n");
    printf("========================================================="
           "===============\n\n");

    printf("  Total head movement, in cylinders, for BOTH starting directions\n");
    printf("  ---------------------------------------------------------------"
           "-------\n");
    printf("  %-8s %14s %14s   %s\n", "", "head moving", "head moving", "");
    printf("  %-8s %14s %14s   %s\n", "algorithm", "DOWN first", "UP first",
           "note");
    printf("  ---------------------------------------------------------------"
           "-------\n");
    for (int a = 0; a < NALGS; a++) {
        Result dn = ALGS[a].fn(&j, 1);
        Result up = ALGS[a].fn(&j, 0);
        const char *note = "";
        if (dn.total == up.total) note = "direction makes no difference";
        else if (a >= 4)          note = "the wrap-around dominates";
        printf("  %-8s %14d %14d   %s\n", ALGS[a].name, dn.total, up.total, note);
    }
    printf("  ---------------------------------------------------------------"
           "-------\n");
    printf("  FCFS and SSTF never reverse on purpose, so direction cannot\n");
    printf("  change their answer. The other four are all defined in terms\n");
    printf("  of a direction, so it changes everything.\n\n");

    printf("  The service order, for the direction above (%s)\n",
           down ? "downward" : "upward");
    printf("  ---------------------------------------------------------------"
           "-------\n");
    for (int a = 0; a < NALGS; a++) {
        Result r = ALGS[a].fn(&j, down);
        printf("  %-8s total %4d", ALGS[a].name, r.total);
        if (ALGS[a].wraps)
            printf("  (%d without the wrap-around seek)", r.total_nojump);
        printf("\n           ");
        print_path(&r);
    }
    printf("  ---------------------------------------------------------------"
           "-------\n");
    printf("  C-SCAN and C-LOOK include one seek across the disk that\n");
    printf("  services no request. Textbooks differ on whether to count it,\n");
    printf("  so both figures are shown. The lecture slides quote both.\n");
    printf("========================================================="
           "===============\n");
    return 0;
}
