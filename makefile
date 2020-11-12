# Makefile for building demo applications.
# by Brian Fraser
# edited by Pierre :)

# Edit this file to compile extra C files into their own programs.
TARGETS= s-talk

CROSS_TOOL = 
CC_CPP = $(CROSS_TOOL)g++
CC_C = $(CROSS_TOOL)gcc

CFLAGS = -std=c99 -std=gnu11 -o s-talk instructorList.o -Wall -g -pthread

all: clean $(TARGETS)

$(TARGETS):
	$(CC_C) $(CFLAGS) $@.c -o $@ 

clean:
	rm -f $(TARGETS)