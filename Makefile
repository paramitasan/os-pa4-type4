CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2

all: fssim iolab disksched

# Part 3: the file allocation simulator. This is the one you edit.
fssim: fssim.c fslib.c fslib.h fs.h
	$(CC) $(CFLAGS) -o $@ fssim.c fslib.c

# Part 1: real I/O and real file metadata, measured. Given complete.
iolab: iolab.c
	$(CC) $(CFLAGS) -o $@ iolab.c

# Part 2: the six disk scheduling algorithms. Given complete.
disksched: disksched.c
	$(CC) $(CFLAGS) -o $@ disksched.c

run: fssim
	./fssim workloads/w1-basic.txt

test: fssim
	@for f in workloads/w*.txt; do \
		echo "=== $$f ==="; \
		./fssim $$f | grep -E "accesses, full sequential|could NOT be placed"; \
	done

clean:
	rm -f fssim iolab disksched fs_report.csv iolab-out.dat iolab-sparse.dat \
	      iolab-hard.lnk iolab-sym.lnk

.PHONY: all run test clean
