# ******************************************************
# PROGRAM: Shell			                           *
# CLASS: CISC 361-011                                  *
# AUTHORS:                                             *
#    Alex Sederquest | alexsed@udel.edu | 702414270    *
#    Ben Segal | bensegal@udel.edu | 702425559         *
# ******************************************************

CC=gcc -w
VPATH = utils

sssh: sh.o lists.o
	$(CC) -g sh.o lists.o -o sssh -lpthread

%.o: %.c
	$(CC) $< -c 

clean:
	rm -rf *.o sssh

run: sssh
	./sssh
