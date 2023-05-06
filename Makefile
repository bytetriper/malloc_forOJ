#
# Makefile for the malloc lab driver
#
CC = gcc
CFLAGS = -Wall -Wextra -O2 -DDRIVER
#CFLAG_NOWARNING = -Wall -Wextra -O2 -g -DDRIVER
OBJS = mdriver.o mm.o memlib.o fsecs.o fcyc.o clock.o ftimer.o driverlib.o
TEST_OBJS = test.o mm.o memlib.o
all: mdriver

mdriver: $(OBJS)
	$(CC) $(CFLAGS) -o mdriver $(OBJS) -lm

test: $(TEST_OBJS)
	$(CC) $(CFLAGS) -o test $(TEST_OBJS) -lm

test.o: test.c mm.h memlib.h
driver.o: mdriver.c fsecs.h fcyc.h clock.h memlib.h config.h mm.h driverlib.h
memlib.o: memlib.c memlib.h
mm.o: mm.c mm.h memlib.h
fsecs.o: fsecs.c fsecs.h config.h
fcyc.o: fcyc.c fcyc.h
ftimer.o: ftimer.c ftimer.h config.h
clock.o: clock.c clock.h
driverlib.o: driverlib.c driverlib.h

clean:
	rm -f *~ *.o mdriver
