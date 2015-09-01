#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "datatype.h"

prog_t prog;

void read_code(void);
void create_gcfg(void);
int icache_analysis(int, int, int);
void dump_gcfg_4_ilp(FILE*);

int main(int argc, char **argv)
{
	int capacity, line_size, assoc;
	if (argc < 2)
		fprintf(stderr,"error: the location of the binary or the address of main function is not provided!\n");
	prog.fname = argv[1];

	// Set up the arguments for cache semantics
	if (argc >= 5) {
		capacity = atoi(argv[2]);
		line_size = atoi(argv[3]);
		assoc = atoi(argv[4]);
	}
	else {
		capacity= 512;
		line_size = 16;
		assoc = 4;
	}

	//printf("Main: capacity = %u, line_size = %u, assoc = %u\n", capacity, line_size, assoc);

	// read the binary of a program for analysis
	read_code();
	// build the global control flow graph for the program
	create_gcfg();
	// analyze the instruction cache behavior
	icache_analysis(capacity, line_size, assoc);
	// print out the global control flow graph
	//dump_cfg(stdout);
	dump_gcfg_4_ilp(stdout);
 	//print_catg();
	return 0;
}
