#ifndef _DATATYPE_H_
#define _DATATYPE_H_

#include <stdint.h>

#define NOT_TAKEN	0
#define TAKEN		1

#define MUST 2
#define MAY 3
#define PERSISTENCE 4

//typedef uint64_t addr_t;
typedef long long int addr_t;

typedef struct inst_t inst_t;
typedef struct proc_t proc_t;
typedef struct prog_t prog_t;
typedef struct cfg_edge_t cfg_edge_t;
typedef struct cfg_node_t cfg_node_t;

typedef struct inst_ref_t inst_ref_t;
typedef struct gcfg_node_t gcfg_node_t;
typedef struct gcfg_edge_t gcfg_edge_t;
typedef struct gcfg_t gcfg_t;

typedef struct als_t als_t;
typedef struct ass_t ass_t;
typedef struct acs_t acs_t;
typedef struct cache_t cache_t;

typedef struct _symb_info_t
{
    addr_t addr;
    long long int size;
} symb_info_t;

// instruction type
typedef enum {
    INST_DATA,
	// sequential instruction
	INST_SEQ,
	// control flow transferring instructions
	INST_COND_BRANCH,
	INST_UNCOND_BRANCH,
	INST_COND_CALL,
	INST_UNCOND_CALL,
	INST_COND_RET,
	INST_UNCOND_RET,
    //
    INST_NOP
} inst_type_t;

// decoded instruction
struct inst_t{
	// instruction information
	addr_t  addr;			// instruction address
	// for control flow analysis
	inst_type_t type;
	addr_t target;			// the target address for the control transfer instruction
    uint32_t binary;
};

// procedure
struct proc_t {
	int id;				// procedure id
	addr_t sa;			// procedure starting address
	long long int size;			// size (in bytes)
	int num_inst;		// number of instructions
	inst_t *code;			// instructions
	int num_bb;		 	// number of basic blocks
	cfg_node_t *cfg;		// entry node to the CFG of this procedure
};

// program
struct prog_t{
	char *fname;
	inst_t *code;			// decoded program text
	long long int code_size;		// code size (in bytes)
	long long int num_inst;			// number of instructions
	addr_t ent_addr;
	proc_t *procs; 			// procedures
	int num_proc;			// number of procedures
	int ent_proc_id;		// index of the entrance procedure
};

// control flow graph node type
typedef enum {
	CTRL_SEQ,
	CTRL_COND_BRANCH,
	CTRL_UNCOND_BRANCH,
	CTRL_COND_CALL,
	CTRL_UNCOND_CALL,
	CTRL_COND_RET,
	CTRL_UNCOND_RET
} bb_type_t;

// control flow graph node
struct cfg_node_t {
	int id;						// basic block id
	proc_t *proc;			// up-link to the procedure containing it
	addr_t sa;						// block starting address
	int size;						// size (in bytes)
	int num_inst;					// number of instructions
	inst_t *code;					// pointer to the first instruction

	bb_type_t type;
	cfg_edge_t *out_n, *out_t;		// outgoing edges (non-taken/taken)
	int num_in;						// number of incoming edges
	cfg_edge_t **in;				// incoming edges
	proc_t *callee;			// points to a callee if this pointer is not NULL
};

// control flow graph edge
struct cfg_edge_t{
	cfg_node_t *src;	// source node
	cfg_node_t *dst;	// destination node
	int taken; // control flow graph edge type
};


// abstract cache line state (ALS)
struct als_t{
	addr_t *blocks;			// the memory blocks mapped to this ALS
	int num_block;		// the number of the memory blocks
    int num_block_pers;
    addr_t *blocks_pers;
};

// abstract set state (ASS)
struct ass_t{
	als_t **lines;
};

// abstract cache state (ACS)
struct acs_t{
	ass_t **sets;
};

// cache semantics
struct cache_t{
	int assoc;
	int line_size;
	int num_set;
};

// color type. This information is for traversing
typedef enum {
	WHITE,
	GRAY,
	BLACK
} color_t;

// reference type
typedef enum {
	NC,				// not categorized
	AH, 			// always hit
	AM, 			// always miss
	NCP				// neither classified as ah nor am, but persistent (first hit)
} ref_type_t;

// instruction reference
struct inst_ref_t {
	// for instruction cache analysis
	addr_t addr;
	ref_type_t ref_type;	// category of memory access for this instruction
	acs_t* acs_in;			// incoming ACS
	acs_t* acs_out;			// outgoing ACS
	acs_t* acs_in_prev;		// incoming ACS in previous pass
	acs_t* acs_out_prev;		// outgoing ACS in previous pass
};

// global control flow grade node
struct gcfg_node_t {
	int id;						// basic block id
	int gid;					// global basic block id
	addr_t sa;						// block starting address
	int num_inst;				// number of instructions
	inst_ref_t *inst_ref;			// pointer to the first instruction

	gcfg_edge_t *out_n, *out_t;		// outgoing edges (non-taken/taken)
	int num_in;						// number of incoming edges
	gcfg_edge_t **in;				// incoming edges

	proc_t *proc;					// up-link to the procedure containing it(for printing only)
	proc_t *callee;					// points to a callee if this pointer is not NULL(for printing only)

	// for traversing
	color_t color;					// 0 - white, 1 - gray, 2 - black
	int d;						// discovered time
	int f;						// finished time

	// memory access statistics
	int ah, am, ncp, nc;
};

// global control flow edge
struct gcfg_edge_t{
	gcfg_node_t *src;	// source node
	gcfg_node_t *dst;	// destination node
	int taken;
};

// global control graph
struct gcfg_t {
	gcfg_node_t **node;
	int num_node;
};

#endif
