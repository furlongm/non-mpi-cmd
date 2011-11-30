/*

 Copyright 2010 Marcus Furlong <furlongm@gmail.com>

 This file is part of non-mpi-cmd.

 non-mpi-cmd is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, version 3 only.

 non-mpi-cmd is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with non-mpi-cmd. If not, see <http://www.gnu.org/licenses/>

*/


#define _SVID_SOURCE

#include <malloc.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <dirent.h>
#include <ctype.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <mpi.h>

#define MAX_BUF 1024

#define WORK_TAG 1
#define CWD_TAG 2
#define DIE_TAG 3 


extern int getopt(int argc, char* const argv[], const char *optstring);
extern char *optarg;
extern int optind, opterr, optopt;
extern int gethostname(char *name, size_t len);

static void master (char*);
static void slave (void);
void construct_cmdline (char*, char*, char*);
int change_dir (char*, struct stat);
void scan_dir (char*);
void usage (char*);
void print_output(char *);

struct file {
	char* name;
	LIST_ENTRY(file) neighbours;
};

LIST_HEAD(files, file) files_head;

int main (int argc, char **argv) {

	int rank = 0;

	int dir_flag = 0, cmd_flag = 0;
	char *dir = NULL, *cmd = NULL;
	int c = 0;
	int nfiles = 0;
	struct stat buf;
	struct file *f;
	opterr = 0;

	LIST_INIT(&files_head);

	while ((c = getopt (argc, argv, "c:d:r")) != -1)
	switch (c) {
		case 'd':
			dir = optarg;
			dir_flag = 1;
			break;
		case 'c':
			cmd = optarg;
			cmd_flag = 1;
			break;
		case '?':
			usage (argv[0]);
	}

	if ((dir_flag == 0) || (optind < argc) || (cmd_flag == 0))
		usage (argv[0]);

	if (stat(dir, &buf) == -1) {
		perror(dir);
		return 1;
	 }

	 if (!S_ISDIR(buf.st_mode)) {
		perror(dir);
		return 1;
	 }

	 if ( chdir(dir)<0 ) {
		perror(dir);
		return 1;
	 }

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	if (rank == 0) {

		stat(cmd, &buf);
		if ( (S_ISREG (buf.st_mode)) && (buf.st_mode & S_IRUSR) && (buf.st_mode & S_IXUSR) ) {
			fprintf (stdout, "[MASTER] Running command: %s\n", cmd);
		} else {
			fprintf (stderr, "[MASTER] Error: %s is not an executable file.\n", cmd);
			usage (argv[0]);
		}
		fflush(stdout);
		fflush(stderr);
	
		scan_dir (dir);
		f = LIST_FIRST(&files_head);
		LIST_FOREACH(f, &files_head, neighbours) {
			nfiles++;
		}
		fprintf (stdout, "[MASTER] Processing %d files\n", nfiles);
		master(cmd);
		fprintf (stdout, "[MASTER] Done.\n");
		fflush(stdout);
	}else{
		slave();
	}

	MPI_Finalize();
	return 0;
}

void print_output(char *output) {

	fprintf(stdout, "%s", output);
	fflush(stdout);
}

static void master (char* cmd) {

	char fullcmd[MAX_BUF];
	char hostname[HOST_NAME_MAX];
	int size, rank;
	MPI_Status status;
	char result[MAX_BUF];
	struct file* f = LIST_FIRST(&files_head);;

	bzero(hostname,HOST_NAME_MAX-1);
	gethostname(hostname,HOST_NAME_MAX-1);

	MPI_Comm_size(MPI_COMM_WORLD, &size);
	fprintf(stdout, "[MASTER] Running with %d processes on %s\n", size, hostname);


	if (size == 1) {
		
		LIST_FOREACH(f, &files_head, neighbours) {
			construct_cmdline(cmd, f->name, fullcmd);
			FILE *output = popen(fullcmd, "r");
			char buffer[MAX_BUF]; 
			fgets(buffer, sizeof(buffer), output);
			pclose(output);
			bzero(result, MAX_BUF);
			strcat(result, "[MASTER] ");
			strcat(result, hostname);
			strcat(result, ": ");
			strcat(result, buffer);
			print_output(result);
		}
	}else{
		for (rank = 1;  rank < size; ++rank, f=LIST_NEXT(f, neighbours)) {
			construct_cmdline(cmd, f->name, fullcmd);
			MPI_Send(fullcmd, strlen(fullcmd)+1, MPI_CHAR, rank, WORK_TAG, MPI_COMM_WORLD);
		}

		LIST_FOREACH(f, &files_head, neighbours) {
			if (rank == 1) {
				MPI_Recv(&result, MAX_BUF+1, MPI_CHAR, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
				print_output(result);
				construct_cmdline(cmd, f->name, fullcmd);
				MPI_Send(fullcmd, strlen(fullcmd)+1, MPI_CHAR, status.MPI_SOURCE, WORK_TAG, MPI_COMM_WORLD);
			}else{
				rank--;
			}
		}

		for (rank = 1; rank < size; ++rank) {
			MPI_Recv(&result, MAX_BUF+1, MPI_CHAR, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
			print_output(result);
		}

		for (rank = 1; rank < size; ++rank)
			MPI_Send(0, 0, MPI_INT, rank, DIE_TAG, MPI_COMM_WORLD);
	}
}

static void slave (void) {

	char fullcmd[MAX_BUF];
	MPI_Status status;
	char result[MAX_BUF];
	char hostname[HOST_NAME_MAX];

	bzero(hostname,HOST_NAME_MAX-1);
	gethostname(hostname,HOST_NAME_MAX-1);

	while(1){

		MPI_Recv(fullcmd, sizeof(fullcmd), MPI_CHAR, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

		if (status.MPI_TAG == DIE_TAG)
			return;

		FILE *output = popen(fullcmd, "r");
		char buffer[MAX_BUF];
		fgets(buffer, sizeof(buffer), output);
		pclose(output);
		bzero(result, MAX_BUF);
		strcat(result, "[SLAVE] ");
		strcat(result, hostname);
		strcat(result, ": ");
		strcat(result, buffer);
		MPI_Send(&result, strlen(result)+1, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
	}
}

void construct_cmdline (char* cmd, char *filename, char* fullcmd) {

	bzero(fullcmd, MAX_BUF);
	strcat(fullcmd, cmd);
	strcat(fullcmd, " ");
	strcat(fullcmd, filename);
}

int change_dir (char *dir, struct stat buf) {

	if (stat(dir, &buf) == -1) {
		perror(dir);
		return 1;
	}

	if (!S_ISDIR(buf.st_mode)) {
		perror(dir);
		return 1;
	}

	if ( chdir(dir)<0 ) {
		perror(dir);
		return 1;
	}
	return 0;
}

void scan_dir (char *dir) {

	int i, n;
	char tempfile[MAX_BUF];
	struct dirent **namelist;
	struct stat buf;

	n = scandir (dir, &namelist, 0, alphasort);

	if (n <= 0)
		perror ("scandir");
	else {
		i = 2;
		strcat (dir, "/");
		while (i < n) {
			strcpy (tempfile, dir);
			strcat (tempfile, namelist[i]->d_name);
			stat (tempfile, &buf);
			if (S_ISREG (buf.st_mode)){
				struct file *f;
				f = malloc(sizeof(*f));
				f->name = malloc(sizeof(char[MAX_BUF]));
				 bzero(f->name, MAX_BUF);
				strcat(f->name, namelist[i]->d_name);
//				fprintf(stdout, "[SCAN_DIR] %s\n", f->name);
				LIST_INSERT_HEAD(&files_head, f, neighbours);
			}
			i++;
		}
		while (n>0)
			free (namelist[--n]);
		free (namelist);
	}
}

void usage (char *progname) {
	fprintf (stderr, "Usage: %s -c COMMAND -d DIRECTORY\n", progname );
	exit (1);
}

