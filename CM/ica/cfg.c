#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "datatype.h"
#include "util.h"

extern prog_t prog;
long long int bb_gid = 0;
gcfg_t *gcfg;

addr_t* get_symb_addrs(void);

// Search for an instruction in specified range specified by CODE and NUM
static long long int lookup_inst(inst_t *code, long long int num, addr_t addr)
{
	long long int i;

	for (i = 0; i < num; i++)
		if (code[i].addr == addr)
			return i;
	return -1;
}

// Lookup a basic block in a function with a match of its starting address to the searched address
static cfg_node_t * lookup_bb(cfg_node_t *cfg, long long int num, addr_t addr)
{
	long long int i;

	for (i = 0; i < num; i++)
		if (cfg[i].sa == addr)
			return &cfg[i];
	return NULL;
}

// Lookup a function with a match of its starting address to the searched address
static proc_t * lookup_proc(addr_t addr)
{
	long long int i;

	for (i = 0; i < prog.num_proc; i++)
		if (prog.procs[i].sa == addr)
			return &prog.procs[i];
	return NULL;

}

// Scan the code of a program and set up the information of functions
static void scan_procs(void)
{
	//int cont;
	long long int i, j;
	long long int cur, num_symbs;
	long long int proc_id = 0;
	int *symb_ent;
	addr_t *symb_addrs;
	//inst_t *inst;
	proc_t *proc;

	symb_ent = (int *) calloc(prog.num_inst, sizeof(int));
	CHECK_MEM(symb_ent)
	// Get a list of addresses of the symbols of the code to analyze
	symb_addrs = get_symb_addrs();
	// The first entry in the list is the number of symbols (includes entry function)
	num_symbs = symb_addrs[0];
	// The second entry is the address of entry function
	prog.ent_addr =  symb_addrs[2];
	// Mark an instruction as an entry of a function if its address is the same as any
	// symbol
	for (i = 0, cur = 0, prog.num_proc = 0; i < prog.num_inst && cur < num_symbs; i++) {
		if (prog.code[i].addr == symb_addrs[3 + cur]) {
			symb_ent[i] = 1;
			prog.num_proc++;
			cur++;
		}
	}
	// Set up the information for symbols
	prog.procs = (proc_t *) calloc(prog.num_proc, sizeof(proc_t));
	CHECK_MEM(prog.procs)
	for (i = 0, proc_id = 0; i < prog.num_inst; i++) {
		if (symb_ent[i]) {
			// Start of a new function
			proc = &prog.procs[proc_id];
			proc->id = proc_id;
			proc->sa = prog.code[i].addr;
			proc->size = 4; //prog.code[i].size;
			proc->num_inst = 1;
			proc->code = &prog.code[i];
			if (proc->sa == prog.ent_addr)
				prog.ent_proc_id = proc_id;
			proc_id++;
		} else {
			// Continuation of current function
			proc->size += 4; //prog.code[i].size;
            //if (prog.code[i].type != INST_DATA)
			    proc->num_inst++;
		}
	}
   
    // MANAGEMENT CODE overhead
    // Assume DMA instructions are inserted at every function call and return and estimate the function size accordingly
    for (i = 0; i < prog.num_proc; i++) {
        proc_t* proc = &(prog.procs[i]);
        for (j = 0; j < proc->num_inst; j++) {
            // calls
            if (proc->code[j].type == INST_COND_CALL || proc->code[j].type == INST_UNCOND_CALL) {
                // 3 instructions for setting up arguments (from address, size, to address)
                // 1 instruction for initiating an DMA operation 
                // 1 instruction for argument passing 
                //   (the ID of the caller function so that the callee knows where to return when it returns)
                proc->size += (5*4);
            }
        }
        // return cost is added for all non-main functions
        if (proc->sa != symb_addrs[2]) {
            // 3 instructions for setting up arguments (from address, size, to address)
            // 1 instruction for initiating an DMA operation 
            proc->size += (4*4);
        }
    }

    // Loop identification and exporting loop starting address
    int nLoops = 0;
    addr_t* loops = (addr_t*)malloc(sizeof(addr_t) * 1000);
    int l;
 
    for (i = 0; i < prog.num_proc; i++) {
        proc_t* proc = &(prog.procs[i]);
        for (j = 0; j < proc->num_inst; j++) {
            if (proc->code[j].type == INST_COND_BRANCH || proc->code[j].type == INST_UNCOND_BRANCH) {
                long long int tid = lookup_inst(proc->code, proc->num_inst, proc->code[j].target);
                if (tid == -1) {
                    printf("This case should not happen! 0x%llx\n", proc->code[j].addr);
                    exit(1);
                }

                // Is this a back edge?
                if (tid < j) {
                    long long int loopEntryAddr = proc->code[tid].addr;

                    int bDuplicate = 0;
                    for (l = 0; l < nLoops; l++) {
                        if (loops[l] == loopEntryAddr) {
                            bDuplicate = 1;
                            break;
                        }
                    }
                    if (bDuplicate == 0) {
                        if (nLoops/100 > 0 && nLoops - nLoops/100*100 == 99) {
                            loops = (long long int*)realloc(realloc, (nLoops+1)*sizeof(long long int));
                        }
                        loops[nLoops] = loopEntryAddr;
                        nLoops++;
                    }
                }
            }
        }
    }
    
    //////////////////////////////////////
    // Exporting function code
    for (i = 0; i < prog.num_proc; i++) {
        proc_t *proc = &(prog.procs[i]);

        char fname[10];
        sprintf(fname, "f%lld", i);
        FILE *fp = fopen(fname, "w");

        fprintf(fp, "%d\n", proc->num_inst);
        for (j = 0; j <proc->num_inst; j++) {
            //fwrite(&(proc->code[j].binary), 4, 1, fp);
            fprintf(fp, "%d ", (proc->code[j].type == INST_DATA)? 1:0);
            fprintf(fp, "%x\n", proc->code[j].binary);
        }

        fclose(fp);
    }

    //////////////////////////////////////
    unsigned int START_ADDR = symb_addrs[1];
    FILE *fp = fopen("loopHeads.txt", "w");
    //FILE *fpLE = fopen("loopExits.txt", "w");
    fprintf(fp, "%d\n", nLoops);
    fprintf(fp, "%x\n", START_ADDR);

    for (l = 0; l < nLoops; l++)
        fprintf(fp, "%lld (%llx)\n", loops[l], loops[l]);

    free(loops);
    fclose(fp);
    
    fp = fopen("userCodeRange.txt", "w");
    fprintf(fp, "%llx %llx\n", (long long unsigned int)START_ADDR, START_ADDR+prog.code_size-4);

    fclose(fp);

#if 0
	// Create a directory for the information of symbols
	symb_dir = calloc(prog.num_proc, sizeof(proc_t));
	for(i = 0; i < prog.num_proc; i++) {
		memcpy((void *)&symb_dir[i], (void *)&prog.procs[i], sizeof(proc_t));
	}
	// Copy instruction information for each symbol
	for (cur = 0, i = 0; i < prog.num_proc; i++) {
		prog.procs[i].code = calloc(prog.procs[i].num_inst, sizeof(inst_t));
		memcpy(&prog.procs[i].code[0], &prog.code[cur], sizeof(inst_t) * prog.procs[i].num_inst);
		cur += prog.procs[i].num_inst;
	}

	// If the target of a branch (b) instruction is not in the same symbol, then duplicate the instructions starting
	// from the target address and ending up at the last instruction of the symbol where the target locates
	cont = 1;
	while (cont) {
		cont = 0;
		for (i = 0; i < prog.num_proc; i++) {
			int num_insts = prog.procs[i].num_inst;
			for (j = 0; j < num_insts; j++) {
				inst = &prog.procs[i].code[j];
				if (inst->type == INST_COND_BRANCH || inst->type == INST_UNCOND_BRANCH) {
					long long int tid = lookup_inst(prog.procs[i].code, prog.procs[i].num_inst, inst->target);
					if (tid == -1) {
						if (!cont)
							cont = 1;
						proc_t *caller, *callee;
						caller = &prog.procs[i];
						for (k = 0; k < prog.num_proc; k++) {
							if (inst->target >= symb_dir[k].sa && inst->target < symb_dir[k].sa + symb_dir[k].size) {
								callee = &symb_dir[k];
								break;
							}
						}
						if (k >= prog.num_proc) {
							fprintf(stderr, "scan_procs: instruction address :0x%llx, can not find the callee\n", inst->addr);
							exit(1);
						}
						int caller_num_inst_head = j + 1; // The first half of the caller should include the current instruction
						int caller_num_inst_tail = caller->num_inst - caller_num_inst_head;
						int callee_num_inst_head = (inst->target - (ulong long int)callee->sa) / inst->size;
						int callee_num_inst_tail = callee->num_inst - callee_num_inst_head; // The second half of the callee should include the target instruction
						inst_t *code = calloc((caller->num_inst+callee_num_inst_tail), sizeof(inst_t));
						memcpy(code, caller->code, sizeof(inst_t) * caller_num_inst_head);
						memcpy(code + caller_num_inst_head, callee->code + callee_num_inst_head, sizeof(inst_t) * callee_num_inst_tail);
						if (caller_num_inst_tail > 0)
							memcpy(code + caller_num_inst_head + callee_num_inst_tail, caller->code + caller_num_inst_head, sizeof(inst_t) * caller_num_inst_tail);
						caller->num_inst += callee_num_inst_tail;
						caller->size += callee->code[0].size * callee_num_inst_tail;
						free(caller->code);
						caller->code = code;
						// Do not free the buffer of callee since it might be directly called
					}

				}
			}
		}
	}
	free(symb_dir);
#endif
	free(symb_addrs);
	free(symb_ent);
	return;
}

// scan the code of a function and mark the leaders of basic blocks
static void scan_blocks(proc_t *proc)
{
	int type;
	long long int i, tid, bb_id;
	int *bb_ent;
	inst_t *inst;
	cfg_node_t *bb;

	bb_ent = (int *) calloc(proc->num_inst, sizeof(int));
	CHECK_MEM(bb_ent)
	// the first instruction is a leader
	bb_ent[0] = 1;
	proc->num_bb = 1;

	for (i = 0; i < proc->num_inst; i++) {
		inst = &proc->code[i];
		type = inst->type;
		// for branch instructions, mark its target and next instruction as a leader
		if (type == INST_COND_BRANCH || type == INST_UNCOND_BRANCH) {
			// mark the instruction at the branch target address as a leader if not marked yet
			tid = lookup_inst(proc->code, proc->num_inst, inst->target);
			if (tid == -1) {
				fprintf(stderr, "scan_blocks: instruction address :0x%llx, instruction type: %d: no match for the branch target at 0x%llx\n", inst->addr, inst->type, inst->target);
				exit(1);
			}
			if (bb_ent[tid] == 0) {
				bb_ent[tid] = 1;
				proc->num_bb++;
			}
			// mark the next instruction as a leader if not marked yet
			if ((i+1 < proc->num_inst) && (bb_ent[i+1] == 0)) {
				bb_ent[i+1] = 1;
				proc->num_bb++;
			}
		}
		// for a call or return instruction, simply mark the next instruction as a leader
		else if (type == INST_COND_CALL || type == INST_UNCOND_CALL || type == INST_COND_RET || type == INST_UNCOND_RET) {
			if ((i+1 < proc->num_inst) && (bb_ent[i+1] == 0)) {
				bb_ent[i+1] = 1;
				proc->num_bb++;
			}
		}
	}

    // 14/6/11... make one-basic block loop into two-basic block loop.
    //  create a new basic block for the branch instruction in such loops.
	for (i = 0; i < proc->num_inst; i++) {
		inst = &proc->code[i];
		type = inst->type;
		if (type == INST_COND_BRANCH || type == INST_UNCOND_BRANCH) {
			tid = lookup_inst(proc->code, proc->num_inst, inst->target);
            if (tid < i) {        // loop
                int j;
                int bOneBB = 1;
                for (j = tid+1; j <= i; j++) {
                    if (bb_ent[j] == 1) {
                        bOneBB = 0;
                        break;
                    }
                }
                if (bOneBB == 1) {
                    bb_ent[i] = 1;
                    proc->num_bb++;
                }
            }
		}
	}

	proc->cfg = (cfg_node_t *) calloc(proc->num_bb, sizeof(cfg_node_t));
	CHECK_MEM(proc->cfg)

	// set basic info for blocks
	for (i = 0, bb_id = 0; i < proc->num_inst; i++) {
		if (bb_ent[i]) {
			// start of a new block
			bb = &proc->cfg[bb_id];
			// local basic block id
			bb->id = bb_id;
			bb->proc = proc;
			bb->sa = proc->code[i].addr;
			bb->size = 4;//proc->code[i].size;
			bb->num_inst = 1;
			bb->code = &proc->code[i];
			bb_id++;
		} else {
			// continuation of current block
			bb->size += 4;//proc->code[i].size;
			bb->num_inst++;
		}
	}
	free(bb_ent);
}

// create a new control flow graph edge from src to dst, "taken" specifies whether this edge is a
// taken edge or a not-taken edge
static void new_edge(cfg_node_t *src, cfg_node_t *dst, int taken)
{
	cfg_edge_t *e = (cfg_edge_t *) malloc(sizeof(cfg_edge_t));
	CHECK_MEM(e)
	e->src = src;
	e->dst = dst;
	e->taken =  taken;
	if (taken == NOT_TAKEN)
		src->out_n = e;
	else
		src->out_t = e;
	dst->num_in++;
	dst->in = (cfg_edge_t **) realloc(dst->in, dst->num_in * sizeof(cfg_edge_t *));
	CHECK_MEM(dst->in)
	dst->in[dst->num_in - 1] = e;
}

// Connect the basic blocks in one function according to the transfer of control flow
static void create_cfg_edges(proc_t *proc)
{
	int type;
	long long int i;
	cfg_node_t *bb, *bb1;

	for (i = 0; i < proc->num_bb; i++) {
		bb = &proc->cfg[i];
		type = bb->code[bb->num_inst-1].type;
		// transfer of control flow in one function
		if (type == INST_COND_BRANCH) {
			// create the not-taken edge
			if (i+1 < proc->num_bb) {
				bb1 = &proc->cfg[i+1];
				new_edge(bb, bb1, NOT_TAKEN);
			}
			// create the taken edge
			bb1 = lookup_bb(proc->cfg, proc->num_bb, bb->code[bb->num_inst-1].target);
			if (bb1 == NULL) {
				fprintf(stderr, "create_cfg_edges: 0x%llx: no match for conditional branch target at 0x%llx\n", bb->code[bb->num_inst-1].addr, bb->code[bb->num_inst-1].target);
				exit(1);
			}
			new_edge(bb, bb1, TAKEN);
			bb->type = CTRL_COND_BRANCH;
		} else if (type == INST_UNCOND_BRANCH) {
			// create the taken edge
			bb1 = lookup_bb(proc->cfg, proc->num_bb, bb->code[bb->num_inst-1].target);
			if (bb1 == NULL) {
				fprintf(stderr, "create_cfg_edges: 0x%llx: no match for unconditional branch target at 0x%llx\n", bb->code[bb->num_inst-1].addr, bb->code[bb->num_inst-1].target);
				exit(1);
			}
			new_edge(bb, bb1, TAKEN);
			bb->type = CTRL_UNCOND_BRANCH;
		}
		// transfer of control flow between different functions and sequential execution
		// for function call and return instruction, the taken edges will be created later
		else if (type == INST_UNCOND_RET)
			bb->type = CTRL_UNCOND_RET;
		else if (type == INST_UNCOND_CALL)
			bb->type = CTRL_UNCOND_CALL;
		else {
			if (type == INST_COND_CALL)
				bb->type = CTRL_COND_CALL;
			else if (type == INST_COND_RET)
				bb->type = CTRL_COND_RET;
			else
				bb->type = CTRL_SEQ;
			// create the not-taken edge if the current block is not the last one in a function
			if (i+1 < proc->num_bb) {
				bb1 = &proc->cfg[i+1];
				new_edge(bb, bb1, NOT_TAKEN);
			}
		}
	}
}

// create a control flow graph for a function in three steps:
// - find basic block entries and create basic blocks
// - set basic info for blocks
// - finish up the construction of CFG by connecting blocks with edges
static void create_cfg(proc_t *proc)
{
	long long int i;
	cfg_node_t *bb;
	addr_t	addr;
	proc_t	*callee;

	scan_blocks(proc);
	// connect the basic blocks in this function
	create_cfg_edges(proc);

	// build links from a calling basic block to the called basic block
	for (i = 0; i < proc->num_bb; i++) {
		bb = &proc->cfg[i];
		if (bb->type == CTRL_COND_CALL || bb->type == CTRL_UNCOND_CALL) {
			addr = bb->code[bb->num_inst-1].target;
			callee = lookup_proc(addr);
			if (callee == NULL) {
				//fprintf(stderr, "0x%x: no match for the callee addr at 0x%x\n", addr);
				//exit(1);
				bb->type = CTRL_SEQ;
			}else
				bb->callee = callee;
		}
	}

}

void dump_cfg(FILE *fp)
{
	long long int i, j;
	cfg_node_t *bb;
	proc_t *proc;

	fprintf(fp, "\nglobal CFG:\n");
	for (i = 0; i < prog.num_proc; i++) {
		proc = &prog.procs[i];
		fprintf(fp, "\tproc[%d]:\n", proc->id);
		for (j = 0; j < proc->num_bb; j++) {
			bb = &proc->cfg[j];
			fprintf(fp, "\t\t%d : %08llx : [ ", bb->id, bb->sa);
			if (bb->out_n != NULL)
				fprintf(fp, "%d",  bb->out_n->dst->id);
			else
				fprintf(fp, "   ");
			fprintf(fp, " , ");
			if (bb->out_t != NULL)
				fprintf(fp, "%d",  bb->out_t->dst->id);
			else
				fprintf(fp, "   ");
			fprintf(fp, " ] ");
			if (bb->callee != NULL)
				fprintf(fp, "call P%d", bb->callee->id);
			else fprintf(fp, "       ");
			fprintf(fp, "\n");
		}
	}
	fprintf(fp, "entry function: %d\n", prog.ent_proc_id);
	fprintf(fp, "entry address: 0x%llx\n", prog.ent_addr);
	fprintf(fp, "\n");
}

// global control flow
static void new_gcfg_edge(gcfg_node_t *src, gcfg_node_t *dst, int taken)
{
	gcfg_edge_t *e = (gcfg_edge_t *) malloc(sizeof(gcfg_edge_t));
	CHECK_MEM(e)
	e->src = src;
	e->dst = dst;
	e->taken = taken;
	if (taken == NOT_TAKEN)
		src->out_n = e;
	else
		src->out_t = e;
	dst->num_in++;
	dst->in = (gcfg_edge_t **) realloc(dst->in, dst->num_in * sizeof(gcfg_edge_t *));
	CHECK_MEM(dst->in)
	dst->in[dst->num_in - 1] = e;
}

int temp_count = 0;

// Recursive inline every function call in a function specifed by PROC
gcfg_node_t * func_inline(proc_t *proc)
{
	int i, j;
	gcfg_node_t *rb = (gcfg_node_t *)calloc(proc->num_bb, sizeof(gcfg_node_t));
	// attach the newly created node to the pointer of global control flow graph
	gcfg->node = (gcfg_node_t **)realloc(gcfg->node, (bb_gid + proc->num_bb) * sizeof(gcfg_node_t*));
	for (i = 0; i < proc->num_bb; i++)
		gcfg->node[bb_gid + i] = &rb[i];
	// Copy node information
	for (i = 0; i < proc->num_bb; i++) {
		rb[i].id = proc->cfg[i].id;
		rb[i].gid = bb_gid++;
		rb[i].sa = proc->cfg[i].sa;
		rb[i].num_inst = proc->cfg[i].num_inst;
		rb[i].inst_ref = (inst_ref_t *)calloc(rb[i].num_inst, sizeof(inst_ref_t));
		for (j = 0; j < rb[i].num_inst; j++)
			rb[i].inst_ref[j].addr = proc->cfg[i].code[j].addr;
		rb[i].proc = proc;
		rb[i].callee = proc->cfg[i].callee;
	}
	// Copy edge information
	for (i = 0; i < proc->num_bb; i++) {
		// Copy all the outgoing edges (incoming edges will be copied as an outgoing edge of other node)
		if (proc->cfg[i].out_n) {
			gcfg_node_t *src = &rb[i];
			gcfg_node_t *dst = &rb[proc->cfg[i].out_n->dst->id];
			new_gcfg_edge(src, dst, proc->cfg[i].out_n->taken);
		}
		if (proc->cfg[i].out_t) {
			gcfg_node_t *src = &rb[i];
			gcfg_node_t *dst = &rb[proc->cfg[i].out_t->dst->id];
			new_gcfg_edge(src, dst, proc->cfg[i].out_t->taken);
		}
	}
	// Expand a function call to its basic blocks, and create an edge from the
	// calling block to the entry block of the called function
	for (i = 0; i < proc->num_bb; i++)
		if (proc->cfg[i].type == CTRL_COND_CALL || proc->cfg[i].type == CTRL_UNCOND_CALL) {
			stack_frame* frame;
			frame = calloc(1, sizeof(stack_frame));
			if (i+1 < proc->num_bb)
				frame->lr = &rb[i+1];
			// Current basic block is the last one in a function
			else {
				fprintf(stderr, "func_inline: no more basic blocks after a function call\n");
				exit(1);
				/*
				stack_frame* call_context = pop();
				// If current function is the main function or if the calling block is the
				// last one in the calling function, mark the returning block as null
				if (call_context == NULL || call_context->lr == NULL)
					frame->lr = NULL;
				// Mark the returning block as the returning block of its calling function
				else
					frame->lr = call_context->lr;
				push(call_context);
				*/
			}
			push(frame);
			gcfg_node_t *src = &rb[i];
			// Get the entry block of the called function
			gcfg_node_t *dst = func_inline(proc->cfg[i].callee);
			new_gcfg_edge(src, dst, TAKEN);
		}

	// create the edge from each exit block to the calling block
	stack_frame *frame = pop();
	// If current function is not main function or if not any of the ancestor calling blocks is the last block
	// in the function it belongs to, create an edge
	if (frame && frame->lr != NULL) {
		for (i = 0; i < proc->num_bb; i++)
			if (proc->cfg[i].type ==  CTRL_COND_RET || proc->cfg[i].type == CTRL_UNCOND_RET) {
				gcfg_node_t *src = &rb[i];
				gcfg_node_t *dst = (gcfg_node_t *)(frame->lr);
				new_gcfg_edge(src, dst, TAKEN);
			}
		free(frame);
	}
	return rb;
}

static inline void build_gcfg(void)
{
	gcfg = calloc(1, sizeof(gcfg_t));
	//gcfg->node = func_inline(&prog.procs[prog.ent_proc_id]);
	func_inline(&prog.procs[prog.ent_proc_id]);
	gcfg->num_node = bb_gid;
}

// Construct a control flow graph for the input program
void create_gcfg(void)
{
	int i;
	// Scan the code of a program and set up the information of functions
	scan_procs();

	// Create a control flow graph for each function
	for (i = 0; i < prog.num_proc; i++)
		create_cfg(&prog.procs[i]);
	// Build the global control flow graph based on the control flow graph for each function
	build_gcfg();
}


// Printf out global control flow graph
void dump_gcfg(FILE *fp)
{
	int i;
	gcfg_node_t *bb;

	fprintf(fp, "\nglobal CFG:\n");
	for (i = 0; i < gcfg->num_node; i++) {
		bb = gcfg->node[i];
		if (bb->sa == bb->proc->sa)
			fprintf(fp, "\tproc[%d]:\n", bb->proc->id);
		fprintf(fp, "\t\t%d : %08llx : [ ", bb->gid, bb->sa);
		if (bb->out_n != NULL)
			fprintf(fp, "%d",  bb->out_n->dst->gid);
		else
			fprintf(fp, "   ");
		fprintf(fp, " , ");
		if (bb->out_t != NULL)
			fprintf(fp, "%d",  bb->out_t->dst->gid);
		else
			fprintf(fp, "   ");
		fprintf(fp, " ] ");
		if (bb->callee != NULL)
			fprintf(fp, "call P%d", bb->callee->id);
		else fprintf(fp, "       ");
		fprintf(fp, "\n");
	}
	fprintf(fp, "entry function: %d\n", prog.ent_proc_id);
	fprintf(fp, "entry address: 0x%llx\n", prog.ent_addr);
	fprintf(fp, "\n");
}

// Printf out global control flow graph for ILP tool
void dump_gcfg_4_ilp(FILE *fp)
{
	int i;
	gcfg_node_t *bb;

	fprintf(fp, "\ndigraph %s {\n", prog.fname);
        fprintf(fp, "FUNCTIONS\n");
	for(i = 0; i < prog.num_proc; i++) {
            fprintf(fp, "\t%5lld;\n", prog.procs[i].size);
        }

	fprintf(fp, "NODES\n");
	for (i = 0; i < gcfg->num_node; i++) {
		bb = gcfg->node[i];
		fprintf(fp, "\t%d [ADDR = %lld, EC = %d, SZ = %d, AH = %d, AM = %d, FM = %d ];\n", bb->gid, bb->sa, bb->proc->id, bb->num_inst, bb->ah, bb->am+bb->nc, bb->ncp);
	}

	fprintf(fp, "EDGES\n");
	for (i = 0; i < gcfg->num_node; i++) {
		bb = gcfg->node[i];
		if (bb->out_n != NULL)
			fprintf(fp, "\t%d -> %d;\n", bb->gid, bb->out_n->dst->gid);
		if (bb->out_t != NULL)
			fprintf(fp, "\t%d -> %d;\n", bb->gid, bb->out_t->dst->gid);
	}
	fprintf(fp, "}\n");
}

