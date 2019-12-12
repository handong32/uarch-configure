CC = gcc
CFLAGS = -O2 -Wall
LFLAGS =

.PHONY: rapl-power-mod

all:	rapl-read rapl-plot rapl-power-mod

rapl-power-mod:
	$(CC) $(LFLAGS) -o rapl-power-mod rapl-power-mod.c -lm

rapl-read:	rapl-read.o
	$(CC) $(LFLAGS) -o rapl-read rapl-read.o -lm

rapl-read.o:	rapl-read.c
	$(CC) $(CFLAGS) -c rapl-read.c

raplr:	raplr.o
	$(CC) -o raplr raplr.o $(LFLAGS) -lm

raplr.o:	raplr.c
	$(CC) $(CFLAGS) -c raplr.c $(LFLAGS) -lm

rapl-plot:	rapl-plot.o
	$(CC) $(LFLAGS) -o rapl-plot rapl-plot.o -lm

rapl-plot.o:	rapl-plot.c
	$(CC) $(CFLAGS) -c rapl-plot.c


clean:	
	rm -f *.o *~ rapl-read rapl-plot raplr rapl-power-mod

install:
	scp rapl-read.c vweaver@sasquatch.eece.maine.edu:public_html/projects/rapl
