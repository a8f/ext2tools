CFLAGS=-std=gnu99 -Wall -g
BINS=ext2_checker ext2_cp ext2_dump ext2_ln ext2_mkdir ext2_restore ext2_rm

all: $(BINS)

%.o : %.c
	gcc $(CFLAGS) -c -o $@ $<

% : %.o
	gcc $(CFLAGS) -o $@ $<

clean :
	rm -f $(BINS) *.o
