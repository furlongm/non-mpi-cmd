#CC=gcc
CC=mpicc
CFLAGS=-Wall -g3 -std=c99

non-mpi-cmd: non-mpi-cmd.o

clean:
	rm -f non-mpi-cmd non-mpi-cmd.o 

debug:
	gdb --args ./non-mpi-cmd -c /bin/ls -d .

run:
	./non-mpi-cmd -c /bin/ls -d .
