#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "datatype.h"

//#define DEBUG_JOIN
//#define DEBUG_UPDATE
#define LOG2(num) log(num)/log(2.0)
#define NEXT_POWER_OF_2(num) pow(2.0, ceil(LOG2(num)))

extern gcfg_t *gcfg;
static cache_t icache;

long long int time = 0;

static inline int max(int a, int b)
{
	return a > b ? a : b;
}

static inline int min(int a, int b)
{
	return a < b ? a : b;
}

// ##########cache operations##########

// mask the offset to 0
static inline addr_t offset_mask(addr_t addr)
{
	int offset_size = LOG2(icache.line_size);
	return addr >> offset_size << offset_size;
}

// return the set index of an memory address
int set_index(addr_t addr)
{
	int index, tag_size, index_size, offset_size;
	offset_size = LOG2(icache.line_size);
	index_size = LOG2(icache.num_set);
	// sizeof(addr) => # of bytes
	// sizeof(addr) * 8 => # of bits
	tag_size = sizeof(addr) * 8 - offset_size - index_size;
	// (<< tag_size >> tag_size) => zeros out tag bits 
	// (>> offset_size) => removes offset bits
	// tttiiiiioooo ==> 000iiiii
	index = ((long long unsigned int)addr << tag_size) >> (tag_size + offset_size);
	return index;
}

// initialize the instruction cache semantics. Align a parameter to a power of 2 if it is not
void icache_init(int capacity, int line_size, int assoc)
{
	// set up the instruction cache semantics
	icache.line_size = NEXT_POWER_OF_2(line_size);
	icache.assoc = NEXT_POWER_OF_2(assoc);
	icache.num_set = NEXT_POWER_OF_2(capacity/line_size/assoc);
}

// ##########abstract cache line state(ALS) operation##########

// return the pointer to a new abstract cache line state
als_t* als_new(void)
{
	als_t* ret_als = (als_t *)calloc(1, sizeof(als_t));
    ret_als->num_block = 0;
    ret_als->blocks = NULL;

    ret_als->num_block_pers = 0;
    ret_als->blocks_pers = NULL;

    return ret_als;
}

void als_init(als_t* als)
{
    if (als->num_block) {
        free(als->blocks);
        als->blocks = NULL;
        als->num_block = 0;
    }
    
    /*
    if (als->num_block_pers) {
        free(als->blocks_pers);
        als->blocks_pers = NULL;
        als->num_block_pers = 0;
    }
    */
}

void als_init_pers(als_t* als)
{
    if (als->num_block_pers) {
        free(als->blocks_pers);
        als->blocks_pers = NULL;
        als->num_block_pers = 0;
    }
}

// return the pointer to a new identical abstract cache line state
// if the input is null pointer, the output is null pointer too
/*inline als_t* als_copy(als_t* line)
{
	als_t *line_out;

	if (line == NULL)
		return NULL;
	line_out = als_new();
	line_out->num_block = line->num_block;
	if (line->num_block > 0) {
		line_out->blocks = (addr_t *)calloc(line_out->num_block, sizeof(addr_t));
		memcpy(line_out->blocks, line->blocks, line_out->num_block * sizeof(addr_t));
	}
	return line_out;
}*/
static inline void als_copy(als_t* src, als_t* dst)
{
    if (src == NULL || dst == NULL) {
        printf("@als_copy: an als is NULL\n");
        exit(1);
    }

    if (dst->num_block)
        free(dst->blocks);
    if (src->num_block) {
        dst->blocks = (addr_t*)calloc(src->num_block, sizeof(addr_t));
        memcpy(dst->blocks, src->blocks, src->num_block*sizeof(addr_t));
    }
    dst->num_block = src->num_block;

    if (dst->num_block_pers)
        free(dst->blocks_pers);
    if (src->num_block_pers) {
        dst->blocks_pers = (addr_t*)calloc(src->num_block_pers, sizeof(addr_t));
        memcpy(dst->blocks_pers, src->blocks_pers, src->num_block_pers*sizeof(addr_t));
    }
    dst->num_block_pers = src->num_block_pers;
}

// free an abstract cache line state
static inline int als_free(als_t *line)
{
	if (line) {
		if (line->num_block > 0) {
			free(line->blocks);
			line->num_block = 0;
		}
        if (line->num_block_pers > 0) {
            free(line->blocks_pers);
            line->num_block_pers = 0;
        }

		free(line);
	}
	return 0;
}

// return a positive number if addr is found in line; otherwise, return -1 if it is missed
static inline int als_lookup(als_t *line, addr_t addr)
{
	int i;
	addr_t m;
	if (line == NULL)
		return -1;
	// mask the offset
	m = offset_mask(addr);
	// check if the block to find in is in the abstract cache line state
	for (i = 0; i < line->num_block; i++) {
		if (m == line->blocks[i])
			return i;
    }
	return -1;
}

static inline int als_lookup_pers(als_t *line, addr_t addr)
{
	int i;
	addr_t m;
	if (line == NULL)
		return -1;
	// mask the offset
	m = offset_mask(addr);
	// check if the block to find in is in the abstract cache line state
	for (i = 0; i < line->num_block_pers; i++) {
		if (m == line->blocks_pers[i])
			return i;
    }
	return -1;
}

#if 0
// return the intersection of two abstract cache line states. Return a dumb
// state if any of the input states is null
//als_t *als_intersect(als_t *line1, als_t *line2)
void als_intersect(als_t *line1, als_t *line2, als_t* target)
{
	int i, j;
	addr_t m;
	//als_t *line_out;

    als_init(target);
    if (line1 == NULL || line2 == NULL) {
        printf("@als_intersect: als is NULL\n");
        exit(1);
    }

    /*
	line_out = als_new();
	if (line1 == NULL || line2 == NULL) {
        return NULL;
		//return line_out;
    }
    */

	for(i = 0; i < line1->num_block; i++) {
		m = line1->blocks[i];
		for(j = 0; j < line2->num_block; j++)
			if (m == line2->blocks[j])
				break;

		if (j < line2->num_block) { // m is also found in line2
			target->num_block++;
			target->blocks = (addr_t *)realloc(target->blocks, target->num_block * sizeof(addr_t));
			target->blocks[target->num_block-1] = m;
            /*
			line_out->num_block++;
			line_out->blocks = (addr_t *)realloc(line_out->blocks, line_out->num_block * sizeof(addr_t));
			line_out->blocks[line_out->num_block-1] = m;
            */
		}
	}
	for(i = 0; i < line1->num_block_pers; i++) {
		m = line1->blocks_pers[i];
		for(j = 0; j < line2->num_block_pers; j++) {
			if (m == line2->blocks_pers[j])
				break;
        }

		if (j < line2->num_block_pers) { 
			target->num_block_pers++;
			target->blocks_pers = (addr_t *)realloc(target->blocks_pers, target->num_block_pers * sizeof(addr_t));
			target->blocks_pers[target->num_block_pers-1] = m;
		}
	}

	//return line_out;
}

// return the union of two abstract cache line states. Return a dumb
// state if both of the input states are null
//als_t* als_union(als_t *line1, als_t *line2)
void als_union(als_t *line1, als_t *line2, als_t *target)
{
	int i, j;
	//als_t *line_out;

    als_init(target);
    if (line1 == NULL || line2 == NULL) {
        printf("@als_union: als is NULL\n");
        exit(1);
    }

    /*
	if (line1 == NULL && line2 == NULL) {
        return NULL;
	}
	if (line1 == NULL) {
		line_out = als_copy(line2);
		return line_out;
	}
	if (line2 == NULL) {
		line_out = als_copy(line1);
		return line_out;
	}
    */

	//als_t *line_common = als_intersect(line1, line2);
	//line_out = als_copy(line_common);
	//line_out = als_intersect(line1, line2);
    als_intersect(line1, line2, target);
 
	for (i = 0; i < line1->num_block; i++) {
		//for (j = 0; j < line_common->num_block; j++) {
		//	if (line1->blocks[i] == line_common->blocks[j])
		for (j = 0; j < target->num_block; j++) {
			if (line1->blocks[i] == target->blocks[j])
				break;
        }
		//if (j == line_common->num_block) {
		if (j == target->num_block) { // this means there is no line1->blocks[i] in target
			target->num_block++;
			target->blocks = (addr_t *)realloc(target->blocks, target->num_block * sizeof(addr_t));
			target->blocks[target->num_block-1] = line1->blocks[i];
		}
	}
	for (i = 0; i < line2->num_block; i++) {
		//for (j = 0; j < line_common->num_block; j++)
			//if (line2->blocks[i] == line_common->blocks[j])
		for (j = 0; j < target->num_block; j++) {
			if (line2->blocks[i] == target->blocks[j])
				break;
        }
		//if (j >= line_common->num_block) {
		if (j == target->num_block) { // this means there is no line2->blocks[i] in target
			target->num_block++;
			target->blocks = (addr_t *)realloc(target->blocks, target->num_block * sizeof(addr_t));
			target->blocks[target->num_block-1] = line2->blocks[i];
		}
	}
	for (i = 0; i < line1->num_block_pers; i++) {
		for (j = 0; j < target->num_block_pers; j++) {
			if (line1->blocks_pers[i] == target->blocks_pers[j])
				break;
        }
		if (j == target->num_block_pers) {
			target->num_block_pers++;
			target->blocks_pers = (addr_t *)realloc(target->blocks_pers, target->num_block_pers * sizeof(addr_t));
			target->blocks_pers[target->num_block_pers-1] = line1->blocks_pers[i];
		}
	}
	for (i = 0; i < line2->num_block_pers; i++) {
		for (j = 0; j < target->num_block_pers; j++) {
			if (line2->blocks_pers[i] == target->blocks_pers[j])
				break;
        }
		if (j == target->num_block_pers) { 
			target->num_block_pers++;
			target->blocks_pers = (addr_t *)realloc(target->blocks_pers, target->num_block_pers * sizeof(addr_t));
			target->blocks_pers[target->num_block_pers-1] = line2->blocks_pers[i];
		}
	}
	//return line_out;
}
#endif

// ##########abstract set state(ASS) operations##########

// return the pointer to a new abstract set state
ass_t* ass_new(void)
{
	int i;
	ass_t *set;

	set = (ass_t *)calloc(1, sizeof(ass_t));
	set->lines = (als_t **)calloc(icache.assoc+1, sizeof(als_t*));
	for (i = 0; i < icache.assoc+1; i++)
		set->lines[i] = als_new();
	return set;
}

void ass_init(ass_t* ass)
{
    int i;
    for (i = 0; i < icache.assoc+1; i++) {
        als_init(ass->lines[i]);
        /*
        if (ass->lines[i]->num_block)
            free(ass->lines[i]->blocks);
        ass->lines[i]->num_block = 0;
        */
    }
}

void ass_init_pers(ass_t* ass)
{
    int i;
    for (i = 0; i < icache.assoc+1; i++) {
        als_init_pers(ass->lines[i]);
    }
}

// return the pointer to a duplicate abstract set state. Return a null
// pointer if the input state is null
static inline void ass_copy(ass_t* src, ass_t* dst)
{
    int i;

    for (i = 0; i < icache.assoc+1; i++) {
        als_copy(src->lines[i], dst->lines[i]);
    }
}

/*
ass_t* ass_copy(ass_t* set)
{
	int i;
	ass_t *set_out;
	//als_t *line;

	if (set == NULL)
		return NULL;
	set_out = ass_new();
	for (i = 0; i < icache.assoc; i++) {
		set_out->lines[i]->num_block = set->lines[i]->num_block;
		if (set_out->lines[i]->num_block > 0) {
			set_out->lines[i]->blocks = (addr_t *)calloc(set_out->lines[i]->num_block, sizeof(addr_t));
			memcpy(set_out->lines[i]->blocks, set->lines[i]->blocks, set_out->lines[i]->num_block * sizeof(addr_t));
		}
	}
	return set_out;
}
*/
// free an abstract set state
int ass_free(ass_t *set)
{
	int i;

	if(!set)
		return 0;
	for (i = 0; i < icache.assoc+1; i++)
		als_free(set->lines[i]);
	free(set->lines);
	free(set);
	return 0;
}

// return the line number within a set if it is a hit, or -1 if missed
static inline int ass_lookup(ass_t *set, addr_t addr)
{
	int i;
	addr_t m;
	//als_t *line;
	if (set == NULL)
		return -1;
	// mask the offset
	m = offset_mask(addr);
	// check if the block to find in is in the abstract set state
	for (i = 0; i < icache.assoc; i++) {
		if(als_lookup(set->lines[i], m) != -1)
			return i;
    }
	return -1;
}

static inline int ass_lookup_pers(ass_t *set, addr_t addr)
{
	int i;
	addr_t m;
	//als_t *line;
	if (set == NULL)
		return -1;
	// mask the offset
	m = offset_mask(addr);
	// check if the block to find in is in the abstract set state
	for (i = 0; i < icache.assoc+1; i++) {
		if(als_lookup_pers(set->lines[i], m) != -1)
			return i;
    }
	return -1;
}

// return the intersection of two abstract set states. Return a dumb
// state if any of the input states is null
//ass_t *ass_intersect(ass_t *set1, ass_t *set2, int (*comp)(int, int))
void ass_intersect(ass_t *set1, ass_t *set2, ass_t* target, int (*comp)(int, int))
{
	int i, j;
	int h, x;
	addr_t m;
	//ass_t *set_out;
	als_t *line_out;

    ass_init(target);
    if (set1 == NULL || set2 == NULL) {
        printf("@ass_intersect: ass is NULL\n");
        exit(1);
    }

	/*set_out = ass_new();
	if (set1 == NULL || set2 == NULL) {
		return set_out;
    }*/

	for(i = 0; i < icache.assoc; i++) {
		for(j = 0; j < set1->lines[i]->num_block; j++) {
			m = set1->lines[i]->blocks[j];
			h = ass_lookup(set2, m);
			if (h != -1) {
				x = comp(i, h);
				// locate the set
				//line_out = set_out->lines[x];
				line_out = target->lines[x];

				line_out->num_block++;
                addr_t* new_blocks = (addr_t*)realloc(line_out->blocks, line_out->num_block * sizeof(addr_t));
                line_out->blocks = new_blocks;
				//line_out->blocks = (addr_t *)realloc(line_out->blocks, line_out->num_block * sizeof(addr_t));
				line_out->blocks[line_out->num_block-1] = m;
			}
		}
    }

	for(i = 0; i < icache.assoc+1; i++) {
		for(j = 0; j < set1->lines[i]->num_block_pers; j++) {
			m = set1->lines[i]->blocks_pers[j];
			h = ass_lookup_pers(set2, m);
			if (h != -1) {
				x = comp(i, h);
				// locate the set
				//line_out = set_out->lines[x];
				line_out = target->lines[x];

				line_out->num_block_pers++;
                addr_t* new_blocks_pers = (addr_t*)realloc(line_out->blocks_pers, line_out->num_block_pers * sizeof(addr_t));
                line_out->blocks_pers = new_blocks_pers;
				//line_out->blocks = (addr_t *)realloc(line_out->blocks, line_out->num_block * sizeof(addr_t));
				line_out->blocks_pers[line_out->num_block_pers-1] = m;
			}
		}
    }

	//return set_out;
}

void ass_intersect_pers(ass_t *set1, ass_t *set2, ass_t* target, int (*comp)(int, int))
{
	int i, j;
	int h, x;
	addr_t m;
	als_t *line_out;

    ass_init_pers(target);
    if (set1 == NULL || set2 == NULL) {
        printf("@ass_intersect: ass is NULL\n");
        exit(1);
    }

	for(i = 0; i < icache.assoc+1; i++) {
		for(j = 0; j < set1->lines[i]->num_block_pers; j++) {
			m = set1->lines[i]->blocks_pers[j];
			h = ass_lookup_pers(set2, m);
			if (h != -1) {
				x = comp(i, h);
				// locate the set
				line_out = target->lines[x];

				line_out->num_block_pers++;
                addr_t* new_blocks_pers = (addr_t*)realloc(line_out->blocks_pers, line_out->num_block_pers * sizeof(addr_t));
                line_out->blocks_pers = new_blocks_pers;
				line_out->blocks_pers[line_out->num_block_pers-1] = m;
			}
		}
    }
}

// return the union of two abstract set states. Return a dumb
// state if both of the input states are null
//ass_t* ass_union(ass_t *set1, ass_t *set2, int (*comp)(int, int))
void ass_union(ass_t *set1, ass_t *set2, ass_t *target, int (*comp)(int, int))
{
	int i, j, k;
	//ass_t *set_out; //, *set_common;
	//als_t *line_out;

    ass_init(target);
    if (set1 == NULL || set2 == NULL) {
        printf("@ass_union: ass is NULL\n");
        exit(1);
    }

    /*
    set_out = ass_new();
	if (set1 == NULL && set2 == NULL) {
		return ass_new();
    }
	if (set1 == NULL) {
		return ass_copy(set2);
    }
	if (set2 == NULL) {
		return ass_copy(set1);
    }
    */

    for (i = 0; i < icache.assoc; i++) {
        for (j = 0; j < set1->lines[i]->num_block; j++) {
            int min_age = i;
            k = ass_lookup(set2, set1->lines[i]->blocks[j]);
            if (k != -1) {
                min_age = comp(i, k);
            }
            
            target->lines[min_age]->num_block++;
            addr_t* new_blocks = (addr_t*)realloc(target->lines[min_age]->blocks, target->lines[min_age]->num_block*sizeof(addr_t));
            if (new_blocks == NULL) {
                printf("@ass_union: realloc returns NULL\n");
                exit(1);
            }
            target->lines[min_age]->blocks = new_blocks;
            target->lines[min_age]->blocks[target->lines[min_age]->num_block - 1] = set1->lines[i]->blocks[j];
        }
    }
    for (i = 0; i < icache.assoc; i++) {
        for (j = 0; j < set2->lines[i]->num_block; j++) {
            if (ass_lookup(target, set2->lines[i]->blocks[j]) == -1) {
                target->lines[i]->num_block++;
                addr_t* new_blocks = (addr_t*)realloc(target->lines[i]->blocks, target->lines[i]->num_block*sizeof(addr_t));
                if (new_blocks == NULL) {
                    printf("@ass_union: realloc returns NULL\n");
                    exit(1);
                }
                target->lines[i]->blocks = new_blocks;
                target->lines[i]->blocks[target->lines[i]->num_block - 1] = set2->lines[i]->blocks[j];
            }
        }
    }
/*
	// union(s1, s2) = intersect(s1, s2) + {m|m is in s1 but not s2} + {m|m is in s2 but not s1}
	// intersect(s1, s2)
	set_common = ass_intersect(set1, set2, comp);
	set_out = ass_copy(set_common);
	//{m|m is in s1 but not s2}
	for (i = 0; i < icache.assoc; i++) {
		for (j = 0; j < set1->lines[i]->num_block; j++) {
			if (ass_lookup(set_common, set1->lines[i]->blocks[j]) == -1) {
				line_out = set_out->lines[i];
				line_out->num_block++;
                addr_t* new_blocks = (addr_t*)realloc(line_out->blocks, line_out->num_block * sizeof(addr_t));
                line_out->blocks = new_blocks;
				//line_out->blocks = (addr_t *)realloc(line_out->blocks, line_out->num_block * sizeof(addr_t));
				line_out->blocks[line_out->num_block-1] = set1->lines[i]->blocks[j];
			}
        }
    }
	//{m|m is in s2 but not s1}
	for (i = 0; i < icache.assoc; i++) {
		for (j = 0; j < set2->lines[i]->num_block; j++) {
			if (ass_lookup(set_common, set2->lines[i]->blocks[j]) == -1) {
				line_out = set_out->lines[i];
				line_out->num_block++;
                addr_t* new_blocks = (addr_t*)realloc(line_out->blocks, line_out->num_block * sizeof(addr_t));
                line_out->blocks = new_blocks;

				line_out->blocks = (addr_t *)realloc(line_out->blocks, line_out->num_block * sizeof(addr_t));
				line_out->blocks[line_out->num_block-1] = set2->lines[i]->blocks[j];
			}
        }
    }
	ass_free(set_common);

	return set_out;
*/
}

void ass_union_pers(ass_t *set1, ass_t *set2, ass_t *target, int (*comp)(int, int))
{
	int i, j, k;

    ass_init_pers(target);
    if (set1 == NULL || set2 == NULL) {
        printf("@ass_union: ass is NULL\n");
        exit(1);
    }

    for (i = 0; i < icache.assoc+1; i++) {
        for (j = 0; j < set1->lines[i]->num_block_pers; j++) {
            int age = i;
            k = ass_lookup_pers(set2, set1->lines[i]->blocks_pers[j]);
            if (k != -1) {
                age = comp(i, k);
            }
            
            target->lines[age]->num_block_pers++;
            addr_t* new_blocks = (addr_t*)realloc(target->lines[age]->blocks_pers, target->lines[age]->num_block_pers*sizeof(addr_t));
            if (new_blocks == NULL) {
                printf("@ass_union: realloc returns NULL\n");
                exit(1);
            }
            target->lines[age]->blocks_pers = new_blocks;
            target->lines[age]->blocks_pers[target->lines[age]->num_block_pers - 1] = set1->lines[i]->blocks_pers[j];
        }
    }
    for (i = 0; i < icache.assoc+1; i++) {
        for (j = 0; j < set2->lines[i]->num_block_pers; j++) {
            if (ass_lookup_pers(target, set2->lines[i]->blocks_pers[j]) == -1) {
                target->lines[i]->num_block_pers++;
                addr_t* new_blocks = (addr_t*)realloc(target->lines[i]->blocks_pers, target->lines[i]->num_block_pers*sizeof(addr_t));
                if (new_blocks == NULL) {
                    printf("@ass_union: realloc returns NULL\n");
                    exit(1);
                }
                target->lines[i]->blocks_pers = new_blocks;
                target->lines[i]->blocks_pers[target->lines[i]->num_block_pers - 1] = set2->lines[i]->blocks_pers[j];
            }
        }
    }
}

// compare two abstract set states and return 0 if they are the same; otherwise, return 1
int ass_cmp(ass_t *set1, ass_t *set2)
{
	int i, j;
	//ass_t *set_common;
	if (set1 == NULL && set2 == NULL)
		return 0;
	if (set1 == NULL || set2 == NULL)
		return 1;

    for (i = 0; i < icache.assoc; i++) {
        if (set1->lines[i]->num_block != set2->lines[i]->num_block)
            return 1;

        for (j = 0; j < set1->lines[i]->num_block; j++) {
            if (ass_lookup(set2, set1->lines[i]->blocks[j]) != i) 
                return 1;
        }
    }
    for (i = 0; i < icache.assoc; i++) {
        for (j = 0; j < set2->lines[i]->num_block; j++) {
            if (ass_lookup(set1, set2->lines[i]->blocks[j]) != i) 
                return 1;
        }
    }

    for (i = 0; i < icache.assoc+1; i++) {
        if (set1->lines[i]->num_block_pers != set2->lines[i]->num_block_pers) {
            return 1;
        }

        for (j = 0; j < set1->lines[i]->num_block_pers; j++) {
            if (ass_lookup_pers(set2, set1->lines[i]->blocks_pers[j]) != i) {
                return 1;
            }
        }
    }
    for (i = 0; i < icache.assoc+1; i++) {
        for (j = 0; j < set2->lines[i]->num_block_pers; j++) {
            if (ass_lookup_pers(set1, set2->lines[i]->blocks_pers[j]) != i) {
                return 1;
            }
        }
    }

/*
	set_common = ass_intersect(set1, set2, min);
	for (i = 0; i < icache.assoc; i++) {
		for (j = 0; j < set1->lines[i]->num_block; j++) {
			if (ass_lookup(set_common, set1->lines[i]->blocks[j]) == -1) {
                // LEAK!
                ass_free(set_common);
				return 1;
            }
        }
    }
	for (i = 0; i < icache.assoc; i++) {
		for (j = 0; j < set2->lines[i]->num_block; j++) {
			if (ass_lookup(set_common, set2->lines[i]->blocks[j]) == -1) {
                // LEAK!
                ass_free(set_common);
				return 1;
            }
        }
    }
    // LEAK!
    ass_free(set_common);
*/
	return 0;
}

// print out an abstract set state
void ass_print(ass_t* set)
{
	int i, j;
	for (i = 0; i < icache.assoc; i++) {
		printf("line[%d]: %u blocks (%d blocks in pers) ", i, set->lines[i]->num_block, set->lines[i]->num_block_pers);
		for (j = 0; j< set->lines[i]->num_block; j++)
			printf("0x%llx ", set->lines[i]->blocks[j]);
        if (set->lines[i]->num_block_pers) {
            printf("(");
		    for (j = 0; j< set->lines[i]->num_block_pers; j++)
			    printf(" 0x%llx", set->lines[i]->blocks_pers[j]);
            printf(" )");
        }
		printf("\n");
	}
	printf("\n");
}

// ##########abstract cache state(ACS) operations##########

// return a pointer to new abstract cache state
acs_t* acs_new(void)
{
	int i;
	acs_t *cache;

	cache = (acs_t *)calloc(1, sizeof(acs_t));
	cache->sets = (ass_t **)calloc(icache.num_set, sizeof(ass_t*));
	for (i = 0; i < icache.num_set; i++)
		cache->sets[i] = ass_new();
	return cache;

}

void acs_init(acs_t* acs)
{
    int i;
    for (i = 0; i < icache.num_set; i++) {
        ass_init(acs->sets[i]);
        
        /*
        int j;
        for (j = 0; j < icache.assoc; j++) {
            acs->sets[i]->lines[j]->num_block = 0;
            free(acs->sets[i]->lines[j]->blocks);
            acs->sets[i]->lines[j]->blocks = NULL;
        }
        */
    }
}

void acs_init_pers(acs_t* acs)
{
    int i;
    for (i = 0; i < icache.num_set; i++) {
        ass_init_pers(acs->sets[i]);
    }
}

// return the pointer to a duplicate abstract cache state. Return a null pointer
// if the input state is null
//acs_t* acs_copy(acs_t* cache)
void acs_copy(acs_t* src, acs_t* dst)
{
	int i;
	//acs_t *cache_out;
	//als_t *line;

    for (i = 0; i < icache.num_set; i++) {
        ass_copy(src->sets[i], dst->sets[i]);
    }
    /*
	if (cache == NULL)
		return NULL;

	cache_out = acs_new();
	for (i = 0; i < icache.num_set; i++) {
        cache_out->sets[i] = ass_new();
        ass_copy(cache->sets[i], cache_out->sets[i]);
		//cache_out->sets[i] = ass_copy(cache->sets[i]);
	}
	return cache_out;
    */
}

// free an abstract cache state
int acs_free(acs_t *cache)
{
	int i;

	if(!cache)
		return 0;
	for (i = 0; i < icache.num_set; i++)
		ass_free(cache->sets[i]);
	free(cache->sets);
	free(cache);
	return 0;
}

// return the line number within a set if it is a hit; otherwise, return -1 if missed
int acs_lookup(acs_t *cache, addr_t addr)
{
	addr_t m;
	ass_t *set;
	if (cache == NULL)
		return -1;
	set = cache->sets[set_index(addr)];
	// mask the offset
	m = offset_mask(addr);
	return ass_lookup(set, m);
}

int acs_lookup_pers(acs_t *cache, addr_t addr)
{
	addr_t m;
	ass_t *set;
	if (cache == NULL)
		return -1;
	set = cache->sets[set_index(addr)];
	// mask the offset
	m = offset_mask(addr);
	return ass_lookup_pers(set, m);
}

// return the intersection of two abstract cache states. Return a dumb
// state if any of the states is null
//acs_t* acs_intersect(acs_t *cache1, acs_t *cache2, int (*cmp)(int, int))
void acs_intersect(acs_t *cache1, acs_t *cache2, acs_t *target, int (*cmp)(int, int))
{
	int i;
	//int empty;
	//acs_t *cache_out;

    if (cache1 == NULL || cache2 == NULL) {
        printf("@acs_intersect: acs is NULL\n");
        exit(1);
    }

	//cache_out = acs_new();
	//cache_out = (acs_t *)calloc(1, sizeof(acs_t));
	//cache_out->sets = (ass_t **)calloc(icache.num_set, sizeof(ass_t*));

    acs_init(target);

	if (cache1 == NULL || cache2 == NULL) {
        return;
		//return acs_new();
    }
	for(i = 0; i < icache.num_set; i++) {
        // LEAK!!
        //ass_free(cache_out->sets[i]);
		//cache_out->sets[i] = ass_intersect(cache1->sets[i], cache2->sets[i], cmp);
        ass_intersect(cache1->sets[i], cache2->sets[i], target->sets[i], cmp);
	}
	//return cache_out;
}

void acs_intersect_pers(acs_t *cache1, acs_t *cache2, acs_t *target, int (*cmp)(int, int))
{
	int i;

    if (cache1 == NULL || cache2 == NULL) {
        printf("@acs_intersect: acs is NULL\n");
        exit(1);
    }

    acs_init(target);

	if (cache1 == NULL || cache2 == NULL) {
        return;
    }
	for(i = 0; i < icache.num_set; i++) {
        ass_intersect_pers(cache1->sets[i], cache2->sets[i], target->sets[i], cmp);
	}
	//return cache_out;
}

// return the union of two abstract cache line states. Return a dumb
// state if both of the states are null
//acs_t* acs_union(acs_t *cache1, acs_t *cache2, int (*cmp)(int, int))
void acs_union(acs_t *cache1, acs_t *cache2, acs_t *target, int (*cmp)(int, int))
{
	int i;
	//acs_t *cache_out;

    if (cache1 == NULL || cache2 == NULL) {
        printf("@acs_union: acs is NULL\n");
        exit(1);
    }
    acs_init(target);
    /*
	if (cache1 == NULL && cache2 == NULL) {
		return acs_new();
    }
	if (cache1 == NULL) {
		return acs_copy(cache2);
    }
	if (cache2 == NULL) {
		return acs_copy(cache1);
    }
    */

	//cache_out = acs_new();
	//cache_out = (acs_t *)calloc(1, sizeof(acs_t));
	//cache_out->sets = (ass_t **)calloc(icache.num_set, sizeof(ass_t*));

	for (i = 0; i < icache.num_set; i++) {
        // LEAK!!
        //ass_free(cache_out->sets[i]);
		//cache_out->sets[i] = ass_union(cache1->sets[i], cache2->sets[i], cmp);
        ass_union(cache1->sets[i], cache2->sets[i], target->sets[i], cmp);
    }
	//return cache_out;
}

void acs_union_pers(acs_t *cache1, acs_t *cache2, acs_t *target, int (*cmp)(int, int))
{
	int i;
	//acs_t *cache_out;

    if (cache1 == NULL || cache2 == NULL) {
        printf("@acs_union: acs is NULL\n");
        exit(1);
    }
    acs_init_pers(target);

	for (i = 0; i < icache.num_set; i++) {
        ass_union_pers(cache1->sets[i], cache2->sets[i], target->sets[i], cmp);
    }
}

// compare two abstract cache states and return 0 if they are the same; otherwise, return 1
int acs_cmp(acs_t *cache1, acs_t *cache2)
{
	int i;
	for (i = 0; i < icache.num_set; i++) {
		if (ass_cmp(cache1->sets[i], cache2->sets[i])) {
			return 1;
        }
    }
	return 0;
}

// print out an abstract set state
void acs_print(acs_t* cache)
{
	int i;
	for (i = 0; i < icache.num_set; i++) {
		printf("set[%d]:", i);
		if (cache->sets[i]) {
			printf("\n");
			ass_print(cache->sets[i]);
		}
		else
			printf(" null\n");
	}
	//printf("\n");
}

// ##########must analysis##########

// join function for must analysis (intersection + maximal age)
//inline acs_t * must_join(acs_t *cache1, acs_t *cache2)
static inline void must_join(acs_t *cache1, acs_t *cache2, acs_t* target)
{
	//return acs_intersect(cache1, cache2, max);
	return acs_intersect(cache1, cache2, target, max);
}

// update function for must analysis,  input abstract cache state
// should not be a null pointer, and so is the output state
//acs_t* must_update(acs_t *cache, addr_t addr)
void must_update(acs_t *cache, addr_t addr, acs_t *target)
{
	int i, j;
	int h;
	addr_t m;
	int set_id;
	//acs_t *cache_out;
	ass_t *set_out;

	/*cache_out = acs_copy(cache);
	if (!cache_out) {
		fprintf(stderr, "Must Update: input abstract cache state is null for address 0x%llx!\n", addr);
		exit(-1);
	}*/

    acs_copy(cache, target);

	// mask the offset
	m = offset_mask(addr);
	set_id = set_index(m);
	// check if the memory block to find is in the abstract cache state
	// h has the line number which is >= 0 && < associativty
	h = acs_lookup(cache, m);
	// for must analysis, there will not be more than one block in the first line
	// if the hit line is the first line, nothing need to be changed
	if (h == 0) {
        return;
		//return cache_out;
    }
	// locate the set
	//set_out = cache_out->sets[set_id];
	set_out = target->sets[set_id];

	if (h > 0 && h < icache.assoc) {		//cache hit
		// l[i] = l[i], i = h+1, ..., icache.assoc-1, nothing need to be done

		// l[h] = l[h-1] + (l[h] − {m})
		// size(l[h]) == 1 => l[h] = l[h-1]
		if (set_out->lines[h]->num_block == 1) {
			/*
            als_t *line_out = als_copy(set_out->lines[h-1]);
			als_free(set_out->lines[h]);
			set_out->lines[h] = line_out;
            */
            if (set_out->lines[h]->num_block)
                free(set_out->lines[h]->blocks);
            set_out->lines[h]->num_block = set_out->lines[h-1]->num_block;
            set_out->lines[h]->blocks = (addr_t*)calloc(set_out->lines[h]->num_block, sizeof(addr_t));
            for (i = 0; i < set_out->lines[h]->num_block; i++)
                set_out->lines[h]->blocks[i] = set_out->lines[h-1]->blocks[i];
		}
		// size(l[h]) > 1 => size(l[h]) - 1 + size(l[h-1]) > 0
		else {
            // addr moves to the top, and all blocks in h-1 age by 1
            int num_merged_block = set_out->lines[h]->num_block-1;
            for (i = 0; i < set_out->lines[h-1]->num_block; i++) {
                if (als_lookup(set_out->lines[h], set_out->lines[h-1]->blocks[i]) == -1)
                    num_merged_block++;
            }
            addr_t* merged_blocks = (addr_t*)calloc(num_merged_block, sizeof(addr_t));

            j = 0;
            for (i = 0; i < set_out->lines[h]->num_block; i++) {
                if (m != set_out->lines[h]->blocks[i]) 
                    merged_blocks[j++] = set_out->lines[h]->blocks[i];
            }

            for (i = 0; i < set_out->lines[h-1]->num_block; i++) {
                if (als_lookup(set_out->lines[h], set_out->lines[h-1]->blocks[i]) == -1)
                    merged_blocks[j++] = set_out->lines[h-1]->blocks[i];
            }

            if (set_out->lines[h]->num_block)
                free(set_out->lines[h]->blocks);
            set_out->lines[h]->num_block = num_merged_block;
            set_out->lines[h]->blocks = merged_blocks;
            /*
			als_t *line_out = als_new();
			line_out->num_block = set_out->lines[h]->num_block-1 + set_out->lines[h-1]->num_block;
			line_out->blocks = (addr_t *)calloc(line_out->num_block, sizeof(addr_t));
			j = 0;
			for (i = 0; i < set_out->lines[h]->num_block; i++) {
				if (m != set_out->lines[h]->blocks[i]) {
					line_out->blocks[j] = set_out->lines[h]->blocks[i];
					j++;
				}
			}
			// size(l[h-1]) == 0 => skip this loop
			for (i = 0; i < set_out->lines[h-1]->num_block; i++)
				line_out->blocks[j+i] = set_out->lines[h-1]->blocks[i];
			als_free(set_out->lines[h]);
			set_out->lines[h]= line_out;
            */
		}
		// l[i] = l[i-1], i = 1, ..., h-1
		for (i = h-1; i >= 1; i--) {
            if (set_out->lines[i]->num_block)
                free(set_out->lines[i]->blocks);
            set_out->lines[i]->num_block = set_out->lines[i-1]->num_block;
            set_out->lines[i]->blocks = (addr_t*)calloc(set_out->lines[i]->num_block, sizeof(addr_t));
            for (j = 0; j < set_out->lines[i]->num_block; j++)
                set_out->lines[i]->blocks[j] = set_out->lines[i-1]->blocks[j];
		}
	} else {	//cache miss
		// l[i] = l[i-1], i = 1, ..., icache.assoc-1
		for (i = icache.assoc-1; i >= 1; i--) {
            if (set_out->lines[i]->num_block)
                free(set_out->lines[i]->blocks);
            set_out->lines[i]->num_block = set_out->lines[i-1]->num_block;
            set_out->lines[i]->blocks = (addr_t*)calloc(set_out->lines[i]->num_block, sizeof(addr_t));
            for (j = 0; j < set_out->lines[i]->num_block; j++)
                set_out->lines[i]->blocks[j] = set_out->lines[i-1]->blocks[j];
			//als_free(set_out->lines[i]);
			//set_out->lines[i] = als_copy(set_out->lines[i-1]);
		}
	}

    // l[0] = {m}
    if (set_out->lines[0]->num_block) {
        free(set_out->lines[0]->blocks);
    }
	set_out->lines[0]->num_block = 1;
    set_out->lines[0]->blocks = (addr_t*)calloc(1, sizeof(addr_t));
	//set_out->lines[0]->blocks = (addr_t *)realloc(set_out->lines[0]->blocks, sizeof(addr));
	set_out->lines[0]->blocks[0] = m;

	//return cache_out;
}

// ##########may analysis##########

// join function for may analysis (union + minimal age)
//inline acs_t *may_join(acs_t *cache1, acs_t *cache2)
static inline void may_join(acs_t *cache1, acs_t *cache2, acs_t *target)
{
	//return acs_union(cache1, cache2, min);
	return acs_union(cache1, cache2, target, min);
}

// update function for may analysis, input abstract cache state
// should not be a null pointer, and so is the output state
//acs_t* may_update(acs_t *cache, addr_t addr)
void may_update(acs_t *cache, addr_t addr, acs_t* target)
{
	int i, j;
	int h;
	addr_t m;
	int set_id;
	//acs_t *cache_out;
	ass_t *set_out;

	/*cache_out = acs_copy(cache);
	if (!cache_out) {
		fprintf(stderr, "May Update: input abstract cache state is null for address 0x%llx!\n", addr);
		exit(-1);
	}*/

    acs_copy(cache, target);

	// mask the offset
	m = offset_mask(addr);
	set_id = set_index(m);
	// check if the memory block to find is in the abstract cache state
	h = acs_lookup(cache, m);
	// locate the set
	//set_out = cache_out->sets[set_id];
	set_out = target->sets[set_id];

/*
    printf("addr %lld goes into set%d\n", m, set_id);
    printf("input\n");
    for (i = 0; i < icache.assoc; i++) {
        printf("line %d: ", i);
        for (j = 0; j < set_out->lines[i]->num_block; j++) {
            printf("%lld ", set_out->lines[i]->blocks[j]);
        }
        printf("\n");
    }
*/

	if (h >= 0 && h < icache.assoc) {		//cache hit
		// l[i] = l[i], i = h+2, ..., icache.assoc-1, nothing need to be done

		//l[h+1] = l[h+1] + (l[h] - {m})
		// if h == icache.assoc-1, nothing needs to be done, since l[h+1] will anyway be evicted later
		if (h < icache.assoc-1) {
			// size(l[h]) == 1 => l[h+1] = l[h+1], nothing needs to be changed
			if (set_out->lines[h]->num_block > 1) {
				// size(l[h]) > 1 => size(l[h]) - 1 + size(l[h+1]) > 0
                int num_merged_block = set_out->lines[h]->num_block-1;
                for (i = 0; i < set_out->lines[h+1]->num_block; i++) {
                    if (als_lookup(set_out->lines[h], set_out->lines[h+1]->blocks[i]) == -1)
                        num_merged_block++;
                }
                addr_t* merged_blocks = (addr_t*)calloc(num_merged_block, sizeof(addr_t));

                j = 0;
                for (i = 0; i < set_out->lines[h]->num_block; i++) {
                    if (m != set_out->lines[h]->blocks[i]) 
                        merged_blocks[j++] = set_out->lines[h]->blocks[i];
                }

                for (i = 0; i < set_out->lines[h+1]->num_block; i++) {
                    if (als_lookup(set_out->lines[h], set_out->lines[h+1]->blocks[i]) == -1)
                        merged_blocks[j++] = set_out->lines[h+1]->blocks[i];
                }

                if (set_out->lines[h+1]->num_block)
                    free(set_out->lines[h+1]->blocks);
                set_out->lines[h+1]->num_block = num_merged_block;
                set_out->lines[h+1]->blocks = merged_blocks;

                /*
				als_t *line_out = als_new();
				line_out->num_block = set_out->lines[h]->num_block - 1 + set_out->lines[h+1]->num_block;
				line_out->blocks = (addr_t *)calloc(line_out->num_block, sizeof(addr_t));
				j = 0;
				for (i = 0; i < set_out->lines[h]->num_block; i++) {
					if (m != set_out->lines[h]->blocks[i]) {
						line_out->blocks[j] = set_out->lines[h]->blocks[i];
						j++;
					}
				}
				// size(l[h+1]) == 0 => skip this loop
				for (i = 0; i < set_out->lines[h+1]->num_block; i++)
					line_out->blocks[j+i] = set_out->lines[h+1]->blocks[i];
				als_free(set_out->lines[h+1]);
				set_out->lines[h+1] = line_out;
                */
			}
		}
		// l[i] = l[i-1], i = 1, ..., h
		for (i = h; i >= 1; i--) {
            if (set_out->lines[i]->num_block)
                free(set_out->lines[i]->blocks);
            set_out->lines[i]->num_block = set_out->lines[i-1]->num_block;
            set_out->lines[i]->blocks = (addr_t*)calloc(set_out->lines[i]->num_block, sizeof(addr_t));
            for (j = 0; j < set_out->lines[i]->num_block; j++)
                set_out->lines[i]->blocks[j] = set_out->lines[i-1]->blocks[j];
            /*
			als_free(set_out->lines[i]);
			set_out->lines[i] = als_copy(set_out->lines[i-1]);
            */
		}
	} else {	//cache miss
		// l[i] = l[i-1], i = 1, ..., icache.assoc-1
		for (i = icache.assoc-1; i >= 1; i--) {
            if (set_out->lines[i]->num_block)
                free(set_out->lines[i]->blocks);
            set_out->lines[i]->num_block = set_out->lines[i-1]->num_block;
            set_out->lines[i]->blocks = (addr_t*)calloc(set_out->lines[i]->num_block, sizeof(addr_t));
            for (j = 0; j < set_out->lines[i]->num_block; j++)
                set_out->lines[i]->blocks[j] = set_out->lines[i-1]->blocks[j];
            /*
			als_free(set_out->lines[i]);
			set_out->lines[i] = als_copy(set_out->lines[i-1]);
            */
		}
	}

    // l[0] = {m}
    if (set_out->lines[0]->num_block) {
        free(set_out->lines[0]->blocks);
    }
	set_out->lines[0]->num_block = 1;
    set_out->lines[0]->blocks = (addr_t*)calloc(1, sizeof(addr_t));
	//set_out->lines[0]->blocks = (addr_t *)realloc(set_out->lines[0]->blocks, sizeof(addr));
	set_out->lines[0]->blocks[0] = m;

/*
    printf("result\n");
    for (i = 0; i < icache.assoc; i++) {
        printf("line %d: ", i);
        for (j = 0; j < set_out->lines[i]->num_block; j++) {
            printf("%lld ", set_out->lines[i]->blocks[j]);
        }
        printf("\n");
    }
*/
	//return cache_out;
}

// ##########persistence analysis##########

// join function for persistence analysis (union + maximal age (includes the virtual line))
//inline acs_t * pers_join(acs_t *acs_in1, acs_t *acs_in2)
static inline void pers_join(acs_t *acs_in1, acs_t *acs_in2, acs_t *target)
{
	//return acs_union(acs_in1, acs_in2, max);
    // may join

/*
    printf("-----------JOIN-------------\n");
    printf("set1\n");
    for (i = 0; i < icache.assoc+1; i++) {
        printf("line %d: ", i);
        for (j = 0; j < acs_in1->sets[15]->lines[i]->num_block_pers; j++) {
            printf("%lld ", acs_in1->sets[15]->lines[i]->blocks_pers[j]);
        }
        printf("\n");
    }
    printf("set2\n");
    for (i = 0; i < icache.assoc+1; i++) {
        printf("line %d: ", i);
        for (j = 0; j < acs_in2->sets[15]->lines[i]->num_block_pers; j++) {
            printf("%lld ", acs_in2->sets[15]->lines[i]->blocks_pers[j]);
        }
        printf("\n");
    }
*/

	acs_union(acs_in1, acs_in2, target, min);
	acs_union_pers(acs_in1, acs_in2, target, max);

/*
    printf("target\n");
    for (i = 0; i < icache.assoc+1; i++) {
        printf("line %d: ", i);
        for (j = 0; j < target->sets[15]->lines[i]->num_block_pers; j++) {
            printf("%lld ", target->sets[15]->lines[i]->blocks_pers[j]);
        }
        printf("\n");
    }
    printf("------------------------\n");
*/
}

// update function for persistence analysis (includes the virtual line), input abstract
// cache state should not be a null pointer, and so is the output state
//acs_t* pers_update(acs_t *cache, addr_t addr)
void pers_update(acs_t *cache, addr_t addr, acs_t* target)
{
	int i, j, k;
	int h;
	addr_t m;
	int set_id;
	ass_t *set_in, *set_out;

    m = offset_mask(addr);
    set_id = set_index(m);
    h = acs_lookup(cache, m);

    set_in = cache->sets[set_id];
    set_out = target->sets[set_id];

/*
    printf("addr %lld goes into set%d\n", m, set_id);
    printf("input\n");
    for (i = 0; i < icache.assoc+1; i++) {
        printf("line %d: ", i);
        for (j = 0; j < set_out->lines[i]->num_block_pers; j++) {
            printf("%lld ", set_out->lines[i]->blocks_pers[j]);
        }
        printf("\n");
    }
*/
    // check the may analysis state, and update persistence analysis state
    int bMayEvict = 0;
    if (h >= 0) {
        //if (addr >= 0x2580 && addr <= 0x3b18) {
        //    printf("%llx (set %d)\n", addr, set_id);
        //    ass_print(set_in);
        //}
        // count the number of blocks, other than m, in the set
        int n_nom = 0;
        for (i = 0; i < set_in->lines[h]->num_block; i++) {
            if (set_in->lines[h]->blocks[i] != m)
                n_nom++;
        }
        if (n_nom >= icache.assoc)
            bMayEvict = 1;
    }

    // Run may_update in parallel
    may_update(cache, addr, target);

    if (bMayEvict == 0) {
        addr_t* new_blocks_pers;
        int new_num_block_pers;

        // virtual line stays the same except that m is removed from it in case it's there
        new_blocks_pers = NULL;
        new_num_block_pers = 0;
        for (i = 0; i < set_out->lines[icache.assoc]->num_block_pers; i++) {
            if (set_out->lines[icache.assoc]->blocks_pers[i] != m)
                new_num_block_pers++;
        }
        if (new_num_block_pers != set_out->lines[icache.assoc]->num_block_pers) {
            new_blocks_pers = (addr_t*)calloc(new_num_block_pers, sizeof(addr_t));
            j = 0;
            for (i = 0; i < set_out->lines[icache.assoc]->num_block_pers; i++) {
                if (set_out->lines[icache.assoc]->blocks_pers[i] != m) {
                    new_blocks_pers[j++] = set_out->lines[icache.assoc]->blocks_pers[i];
                }
            }
            free(set_out->lines[icache.assoc]->blocks_pers);
            set_out->lines[icache.assoc]->blocks_pers = new_blocks_pers;
            set_out->lines[icache.assoc]->num_block_pers = new_num_block_pers;
        }

        // the last line (the line before virtual line) doesn't age
        new_blocks_pers = NULL;
        new_num_block_pers = 0;
        for (i = icache.assoc-1; i >= icache.assoc-2; i--) {
            for (j = 0; j < set_out->lines[i]->num_block_pers; j++) {
                if (set_out->lines[i]->blocks_pers[j] != m)
                    new_num_block_pers++;
            }
        }
        new_blocks_pers = (addr_t*)calloc(new_num_block_pers, sizeof(addr_t));
        k = 0;
        for (i = icache.assoc-1; i >= icache.assoc-2; i--) {
            for (j = 0; j < set_out->lines[i]->num_block_pers; j++) {
                if (set_out->lines[i]->blocks_pers[j] != m) {
                    new_blocks_pers[k++] = set_out->lines[i]->blocks_pers[j];
                }
            }
        }
        set_out->lines[icache.assoc-1]->num_block_pers = new_num_block_pers;
        set_out->lines[icache.assoc-1]->blocks_pers = new_blocks_pers;

        // all other lines age by one. If m is there, exclude it.
        for (i = icache.assoc-2; i >= 1; i--) {
            if (set_out->lines[i]->num_block_pers)
                free(set_out->lines[i]->blocks_pers);
            set_out->lines[i]->num_block_pers = 0;
            for (j = 0; j < set_out->lines[i-1]->num_block_pers; j++) {
                if (set_out->lines[i-1]->blocks_pers[j] != m)
                    set_out->lines[i]->num_block_pers++;
            }
            set_out->lines[i]->blocks_pers = (addr_t*)calloc(set_out->lines[i]->num_block_pers, sizeof(addr_t));
            k = 0;
            for (j = 0; j < set_out->lines[i-1]->num_block_pers; j++) {
                if (set_out->lines[i-1]->blocks_pers[j] != m)
                    set_out->lines[i]->blocks_pers[k++] = set_out->lines[i-1]->blocks_pers[j];
            }
        }
    }
    else {
        addr_t* new_blocks_pers;
        int new_num_block_pers;

        // the last line and the virtual line gets merged 
        new_blocks_pers = NULL;
        new_num_block_pers = 0;
        for (i = icache.assoc; i >= icache.assoc-1; i--) {
            for (j = 0; j < set_out->lines[i]->num_block_pers; j++) {
                if (set_out->lines[i]->blocks_pers[j] != m)
                    new_num_block_pers++;
            }
        }
        new_blocks_pers = (addr_t*)calloc(new_num_block_pers, sizeof(addr_t));
        k = 0;
        for (i = icache.assoc; i >= icache.assoc-1; i--) {
            for (j = 0; j < set_out->lines[i]->num_block_pers; j++) {
                if (set_out->lines[i]->blocks_pers[j] != m) {
                    new_blocks_pers[k++] = set_out->lines[i]->blocks_pers[j];
                }
            }
        }
        set_out->lines[icache.assoc]->num_block_pers = new_num_block_pers;
        set_out->lines[icache.assoc]->blocks_pers = new_blocks_pers;
        
        // all other lines age by one
        for (i = icache.assoc-1; i >= 1; i--) {
            if (set_out->lines[i]->num_block_pers)
                free(set_out->lines[i]->blocks_pers);
            set_out->lines[i]->num_block_pers = 0;
            for (j = 0; j < set_out->lines[i-1]->num_block_pers; j++) {
                if (set_out->lines[i-1]->blocks_pers[j] != m) 
                    set_out->lines[i]->num_block_pers++;
            }
            set_out->lines[i]->blocks_pers = (addr_t*)calloc(set_out->lines[i]->num_block_pers, sizeof(addr_t));
            k = 0;
            for (j = 0; j < set_out->lines[i-1]->num_block_pers; j++) {
                if (set_out->lines[i-1]->blocks_pers[j] != m) 
                    set_out->lines[i]->blocks_pers[k++] = set_out->lines[i-1]->blocks_pers[j];
            }
        }
    }
    if (set_out->lines[0]->num_block_pers) {
        free(set_out->lines[0]->blocks_pers);
    }
	set_out->lines[0]->num_block_pers = 1;
    set_out->lines[0]->blocks_pers = (addr_t*)calloc(1, sizeof(addr_t));
	set_out->lines[0]->blocks_pers[0] = m;

/*
    printf("result\n");
    for (i = 0; i < icache.assoc+1; i++) {
        printf("line %d: ", i);
        for (j = 0; j < set_out->lines[i]->num_block_pers; j++) {
            printf("%lld ", set_out->lines[i]->blocks_pers[j]);
        }
        printf("\n");
    }
*/
#if 0
	/*cache_out = acs_copy(cache);
	if (!cache_out) {
		fprintf(stderr, "Persistence Update: input abstract cache state is null for address 0x%llx!\n", addr);
		exit(-1);
	}*/
    acs_copy(cache, target);

	// mask the offset
	m = offset_mask(addr);
	set_id = set_index(m);
	// check if the memory block to find is in the abstract cache state
	h = acs_lookup(cache, m);
	// if the hit line is the first line, nothing need to be changed
	if (h == 0) {
        return;
		//return cache_out;
    }
	// locate the set
	//set_out = cache_out->sets[set_id];
	set_out = target->sets[set_id];

	// cache hit or if the memory block to find is in the virtual line
	if (h > 0 && h < icache.assoc) {
		// l[i] = l[i], i = h+1, ..., icache.assoc-1 (the last line is the virtual line in this case)

		// l[h] = l[h-1] + (l[h] − {m})
		// size(l[h]) == 1 => l[h] = l[h-1]
		if (set_out->lines[h]->num_block == 1) {
            als_copy(set_out->lines[h-1], set_out->lines[h]);
            /*
			als_t *line_out = als_copy(set_out->lines[h-1]);
			als_free(set_out->lines[h]);
			set_out->lines[h] = line_out;
            */
		}
		// size(l[h]) > 1 => size(l[h]) - 1 + size(l[h-1]) > 0
		else {
            int num_merged_block = set_out->lines[h]->num_block-1;
            for (i = 0; i < set_out->lines[h-1]->num_block; i++) {
                if (als_lookup(set_out->lines[h], set_out->lines[h-1]->blocks[i]) == -1)
                    num_merged_block++;
            }
            addr_t* merged_blocks = (addr_t*)calloc(num_merged_block, sizeof(addr_t));

            printf("Access: %lld from line %d in set %d. Line %d now contains: \t", m, h, set_id, h);
            j = 0;
            for (i = 0; i < set_out->lines[h]->num_block; i++) {
                if (m != set_out->lines[h]->blocks[i]) {
                    merged_blocks[j++] = set_out->lines[h]->blocks[i];
                    printf("%lld ", set_out->lines[h]->blocks[i]);
                }
            }

            for (i = 0; i < set_out->lines[h-1]->num_block; i++) {
                if (als_lookup(set_out->lines[h], set_out->lines[h-1]->blocks[i]) == -1) {
                    merged_blocks[j++] = set_out->lines[h-1]->blocks[i];
                    printf("%lld ", set_out->lines[h-1]->blocks[i]);
                }
            }
            printf("\n");

            if (set_out->lines[h]->num_block)
                free(set_out->lines[h]->blocks);
            set_out->lines[h]->num_block = num_merged_block;
            set_out->lines[h]->blocks = merged_blocks;

            /*
			als_t *line_out = als_new();
			line_out->num_block = set_out->lines[h]->num_block-1 + set_out->lines[h-1]->num_block;
			line_out->blocks = (addr_t *)calloc(line_out->num_block, sizeof(addr_t));
			j = 0;
			for (i = 0; i < set_out->lines[h]->num_block; i++) {
				if (m != set_out->lines[h]->blocks[i]) {
					line_out->blocks[j] = set_out->lines[h]->blocks[i];
					j++;
				}
			}
			// size(l[h-1]) == 0 => skip this loop
			for (i = 0; i < set_out->lines[h-1]->num_block; i++)
				line_out->blocks[j+i] = set_out->lines[h-1]->blocks[i];
			als_free(set_out->lines[h]);
			set_out->lines[h]= line_out;
            */
		}
		// l[i] = l[i-1], i = 1, ..., h-1
		for (i = h-1; i >=1; i--) {
            als_copy(set_out->lines[i-1], set_out->lines[i]);
            /*
			als_free(set_out->lines[i]);
			set_out->lines[i] = als_copy(set_out->lines[i-1]);
            */
		}
	}
	// the memory block is found neither in the cache nor in the virtual line
	else {
		// l[icache.assoc-1] = l[icache.assoc-1] + l[icache.assoc-2]
		// l[icache.assoc-1] is the virtual line and l[icache.assoc-2] is the last ALS in the ACS
		// since icache.assoc is 1 greater than the real associativity
        als_t *new_virtual_line = als_new();
        als_union(set_out->lines[icache.assoc-2], set_out->lines[icache.assoc-1], new_virtual_line);
        als_copy(new_virtual_line, set_out->lines[icache.assoc-1]);
        als_free(new_virtual_line);

        /*
		als_t *virtual_line_old = set_out->lines[icache.assoc-1];
		set_out->lines[icache.assoc-1] = als_union(set_out->lines[icache.assoc-2], virtual_line_old);
		if (virtual_line_old)
			als_free(virtual_line_old);
        */
		// l[i] = l[i-1], i = 1, ..., icache.assoc-2
		for (i = icache.assoc-2; i >= 1; i--) {
            als_copy(set_out->lines[i-1], set_out->lines[i]);
            /*
			als_free(set_out->lines[i]);
			set_out->lines[i] = als_copy(set_out->lines[i-1]);
            */
		}
	}

    // l[0] = {m}
    if (set_out->lines[0]->num_block) {
        free(set_out->lines[0]->blocks);
    }
	set_out->lines[0]->num_block = 1;
    set_out->lines[0]->blocks = (addr_t*)calloc(1, sizeof(addr_t));
	//set_out->lines[0]->blocks = (addr_t *)realloc(set_out->lines[0]->blocks, sizeof(addr));
	set_out->lines[0]->blocks[0] = m;

/*
    printf("---\n");
    for (i = 0; i < set_out->lines[4]->num_block; i++)
        printf("%lld ", set_out->lines[4]->blocks[i]);
    printf("\n");
*/
	//return cache_out;
#endif
}

// ##########abstract cache analysis wrap function##########

// abstract cache analysis
//int abst_anal(gcfg_node_t *bb,  acs_t * (*join)(acs_t *, acs_t *), acs_t* (*update)(acs_t *, addr_t))
int abst_anal(gcfg_node_t *bb,  void (*join)(acs_t *, acs_t *, acs_t *), void (*update)(acs_t *, addr_t, acs_t*))
{
	int i;
	//int h;
	addr_t addr;
	//acs_t *acs_in0, *acs_in1, *acs_in2;
	acs_t *acs_in0, *acs_in1, *acs_merged;

    acs_init(bb->inst_ref[0].acs_in);

	// Join all the incoming abstract cache states if there are more than one
	if (bb->num_in > 1) {
		acs_in0 = bb->in[0]->src->inst_ref[bb->in[0]->src->num_inst-1].acs_out;
		acs_in1 = bb->in[1]->src->inst_ref[bb->in[1]->src->num_inst-1].acs_out;

        acs_merged = acs_new();

#ifdef DEBUG_JOIN
        printf("-------------\n");
		printf("join: set 0\n");
		if (acs_in0)
			acs_print(acs_in0);
		else
			printf("null\n\n");
        printf("-------------\n");
		printf("join: set 1\n");
		if (acs_in1)
			acs_print(acs_in1);
		else 
			printf("null\n\n");
        printf("-------------\n");
#endif
        join(acs_in0, acs_in1, acs_merged);
#ifdef DEBUG_JOIN
		printf("join: result\n");
		acs_print(acs_merged);
        printf("-------------\n");
#endif
		// Keep joining if there are more than 2 incoming abstract cache states
		for (i = 2; i < bb->num_in; i++) {
            acs_in0 = acs_new();
            acs_copy(acs_merged, acs_in0);
			acs_in1 = bb->in[i]->src->inst_ref[bb->in[i]->src->num_inst - 1].acs_out;

            join(acs_in0, acs_in1, acs_merged);
            acs_free(acs_in0);

			//acs_in2 = join(acs_in0, acs_in1);
			// release the resource of intermediate result
			//acs_free(acs_in0);
			//acs_in0 = acs_in2;
		}
		// Initialize the incoming ACS of the entry instruction as the result of the join operations
		//acs_free(bb->inst_ref[0].acs_in);
		//bb->inst_ref[0].acs_in = acs_in0;
        acs_copy(acs_merged, bb->inst_ref[0].acs_in);
        acs_free(acs_merged);
	} else if (bb->num_in == 1) {
        acs_copy(bb->in[0]->src->inst_ref[bb->in[0]->src->num_inst-1].acs_out, bb->inst_ref[0].acs_in);
		//acs_free(bb->inst_ref[0].acs_in);
		//bb->inst_ref[0].acs_in = acs_copy(bb->in[0]->src->inst_ref[bb->in[0]->src->num_inst-1].acs_out);
	} /*else if(bb->num_in == 0) {
		//acs_free(bb->code[0].acs_in);
        if (bb->inst_ref[0].acs_in != NULL)
            acs_free(bb->inst_ref[0].acs_in);
		bb->inst_ref[0].acs_in = acs_new();
	}*/
	// Update ACS for each instruction within this basic block
	for (i = 0; i < bb->num_inst; i++) {
		addr = bb->inst_ref[i].addr;
#ifdef DEBUG_UPDATE
		printf("incoming abstract cache state:\n");
		acs_print(bb->inst_ref[i].acs_in);
		printf("instruction address: %lld\n", addr);
#endif
		// LEAK!!
        //acs_free(bb->inst_ref[i].acs_out);
        //bb->inst_ref[i].acs_out = update(bb->inst_ref[i].acs_in, addr);
        update(bb->inst_ref[i].acs_in, addr, bb->inst_ref[i].acs_out);
#ifdef DEBUG_UPDATE
		printf("\noutgoing abstract cache state:\n");
		acs_print(bb->inst_ref[i].acs_out);
#endif
		// The outgoing ACS of current instruction is the incoming ACS for next instruction
		if(i+1 < bb->num_inst) {
            //acs_copy(bb->inst_ref[i].acs_out, bb->inst_ref[i+1].acs_in);
            bb->inst_ref[i+1].acs_in = bb->inst_ref[i].acs_out;
            /*
			acs_free(bb->inst_ref[i+1].acs_in);
			bb->inst_ref[i+1].acs_in = acs_copy(bb->inst_ref[i].acs_out);
            */
		}
	}

	return 0;
}

// ##########control flow graph traversing##########

// traverse a control flow graph graph from starting node specified by u
//void cfg_visit(gcfg_node_t *u, acs_t * (*join)(acs_t *, acs_t *), acs_t* (*update)(acs_t *, addr_t))
void cfg_visit(gcfg_node_t *u, void (*join)(acs_t *, acs_t *, acs_t *), void (*update)(acs_t *, addr_t, acs_t*))
{
	gcfg_node_t *v;

	time = time + 1;
	u->d = time;

	//printf("node %d\n", u->id);
	abst_anal(u, join, update);
	u->color = GRAY;
	// not taken branch
	if (u->out_n) {
		v = u->out_n->dst;
		if (v->color == WHITE) {
			//v->pi = u;
			cfg_visit(v, join, update);
		}
	}
	// taken branches
	if (u->out_t) {
		v = u->out_t->dst;
		if (v->color == WHITE) {
			//v->pi = u;
			cfg_visit(v, join, update);
		}
	}
	u->color = BLACK;
	time = time + 1;
	u->f = time;
}

// reset the control flow graph for traversing
void cfg_reset(void) {
	long long int i;
	time = 0;
	for (i = 0; i < gcfg->num_node; i++) {
		gcfg->node[i]->color = WHITE;
		gcfg->node[i]->d = 0;
		gcfg->node[i]->f = 0;
	}
}

void acs_reset(void) {
    long long int i, j;
    for (i = 0; i < gcfg->num_node; i++) {
        acs_init(gcfg->node[i]->inst_ref[0].acs_in);
        acs_init(gcfg->node[i]->inst_ref[0].acs_out);

        for (j = 1; j < gcfg->node[i]->num_inst; j++) {
            acs_init(gcfg->node[i]->inst_ref[j].acs_out);
        }

        acs_init(gcfg->node[i]->inst_ref[0].acs_in_prev);
        acs_init(gcfg->node[i]->inst_ref[gcfg->node[i]->num_inst-1].acs_out_prev);

        /*
        for (j = 0; j < gcfg->node[i]->num_inst; j++) {
            acs_t* acs_in = gcfg->node[i]->inst_ref[j].acs_in;
            acs_t* acs_in_prev = gcfg->node[i]->inst_ref[j].acs_in_prev;
            acs_t* acs_out = gcfg->node[i]->inst_ref[j].acs_out;
            acs_t* acs_out_prev = gcfg->node[i]->inst_ref[j].acs_out_prev;

            acs_init(acs_in);
            acs_init_pers(acs_in);
            acs_init(acs_in_prev);
            acs_init_pers(acs_in_prev);
            acs_init(acs_out);
            acs_init_pers(acs_out);
            acs_init(acs_out_prev);
            acs_init_pers(acs_out_prev);
        }
        */
    }
}
// return 1 if the traversing of a control flow graph reaches a fixpoint
int cfg_converge(void)
{
	//long long int i, j;
	long long int i;
    /*
	addr_t addr;
	int set_id;
	ass_t *ass1, *ass2;
    */
	for (i = 0; i < gcfg->num_node; i++) {
		//for (j = 0; j < gcfg->node[i]->num_inst; j++) {
            acs_t* acs_in = gcfg->node[i]->inst_ref[0].acs_in;
            acs_t* acs_in_prev = gcfg->node[i]->inst_ref[0].acs_in_prev;

            if (acs_cmp(acs_in, acs_in_prev) != 0) {
                return 0;
            }

            acs_t* acs_out = gcfg->node[i]->inst_ref[gcfg->node[i]->num_inst-1].acs_out;
            acs_t* acs_out_prev = gcfg->node[i]->inst_ref[gcfg->node[i]->num_inst-1].acs_out_prev;

            if (acs_cmp(acs_out, acs_out_prev) != 0) {
                return 0;
            }
        //}
    }
    /*
			addr = gcfg->node[i]->inst_ref[j].addr;
			set_id = set_index(addr);
			if (gcfg->node[i]->inst_ref[j].acs_in_prev)
				ass1 = gcfg->node[i]->inst_ref[j].acs_in_prev->sets[set_id];
			else
				ass1 = NULL;
			if (gcfg->node[i]->inst_ref[j].acs_in)
				ass2 = gcfg->node[i]->inst_ref[j].acs_in->sets[set_id];
			else
				ass2 = NULL;
			if(ass_cmp(ass1, ass2)) {
				return 0;
			}
			if (gcfg->node[i]->inst_ref[j].acs_out_prev)
				ass1 = gcfg->node[i]->inst_ref[j].acs_out_prev->sets[set_id];
			else
				ass1 = NULL;
			if (gcfg->node[i]->inst_ref[j].acs_out)
				ass2 = gcfg->node[i]->inst_ref[j].acs_out->sets[set_id];
			else
				ass2 = NULL;
			if(ass_cmp(ass1, ass2)) {
				return 0;
			}
		}
	}
    */
	return 1;
}

// traverse the reachable nodes in a control flow graph from the entry node
int cfg_trav(int anal)
{
	//long long int i, j;
	long long int i;
    int num;
	//acs_t* (*join)(acs_t *, acs_t *);
	//acs_t* (*update)(acs_t *, addr_t);
	void (*join)(acs_t *, acs_t *, acs_t*);
	void (*update)(acs_t *, addr_t, acs_t*);

	num = 0;
	time = 0;
	// traverse reachable basic blocks from the entry of CFG
	if (anal == MUST) {
		join = must_join;
		update = must_update;
	} else if (anal == MAY) {
		join = may_join;
		update = may_update;
	} else {
		join = pers_join;
		update = pers_update;
	}

	// increase the associativity and use the extra line as the virtual line for persistence analysis

	// reset the control flow graph for traversing
	cfg_reset();
	cfg_visit(gcfg->node[0], join, update);
	do {
		cfg_reset();
        //printf("---------Iteration %d-----------\n", num);
		for (i = 0; i < gcfg->num_node; i++) {
            //printf("node %lld\n", i);
            acs_copy(gcfg->node[i]->inst_ref[0].acs_in, gcfg->node[i]->inst_ref[0].acs_in_prev);
            acs_copy(gcfg->node[i]->inst_ref[gcfg->node[i]->num_inst-1].acs_out, gcfg->node[i]->inst_ref[gcfg->node[i]->num_inst-1].acs_out_prev);
            /*
			for (j = 0; j < gcfg->node[i]->num_inst; j++) {
                acs_copy(gcfg->node[i]->inst_ref[j].acs_in, gcfg->node[i]->inst_ref[j].acs_in_prev);
                acs_copy(gcfg->node[i]->inst_ref[j].acs_out, gcfg->node[i]->inst_ref[j].acs_out_prev);
			}
            */
		}
		cfg_visit(gcfg->node[0], join, update);
	} while (!cfg_converge() && num++ < 100);

	//if (num >= 100){
	//	fprintf(stderr, "Not converge!\n");
	//	exit(-1);
	//}
	// restore the associativity
	return 0;
}

// ##########instruction memory reference categorization##########
// categorize the memory reference for each instruction
int ref_catg(int anal)
{
	long long int i, j;
	int set_id;
	addr_t m;

	// must analysis is used to find out the always-hit instructions
	if (anal == MUST) {
		for (i = 0; i < gcfg->num_node; i++) {
			for (j = 0; j < gcfg->node[i]->num_inst; j++) {
				if (gcfg->node[i]->inst_ref[j].ref_type == NC) {
					m = offset_mask(gcfg->node[i]->inst_ref[j].addr);
					set_id = set_index(m);
					if (gcfg->node[i]->inst_ref[j].acs_in) {
						if (ass_lookup(gcfg->node[i]->inst_ref[j].acs_in->sets[set_id], m) != -1) {
							gcfg->node[i]->inst_ref[j].ref_type = AH;
						}
                    }
				}
            }
        }
	}
	// may analysis is to find out the always-miss instructions
	else if (anal == MAY) {
		for (i = 0; i < gcfg->num_node; i++) {
			for (j = 0; j < gcfg->node[i]->num_inst; j++) {
				if (gcfg->node[i]->inst_ref[j].ref_type == NC) {
					m = offset_mask(gcfg->node[i]->inst_ref[j].addr);
					set_id = set_index(m);
					if (!gcfg->node[i]->inst_ref[j].acs_in || ass_lookup(gcfg->node[i]->inst_ref[j].acs_in->sets[set_id], m) == -1) {
						gcfg->node[i]->inst_ref[j].ref_type = AM;
					}
				}
            }
        }
	}
	// persistence analysis is to find out the first-hit instructions
	else {
		// increase cache associativity by 1 and use the last line as virtual line
		//icache.assoc++;
		for (i = 0; i < gcfg->num_node; i++) {
			for (j = 0; j < gcfg->node[i]->num_inst; j++) {
				if (gcfg->node[i]->inst_ref[j].ref_type == NC) {
					m = offset_mask(gcfg->node[i]->inst_ref[j].addr);
					set_id = set_index(m);
                    //if (gcfg->node[i]->inst_ref[j].addr >= 0x2580 && gcfg->node[i]->inst_ref[j].addr <= 0x3b18) {

                    //    printf("addr %llx --> ", gcfg->node[i]->inst_ref[j].addr);
				    //    if (gcfg->node[i]->inst_ref[j].ref_type != NC)
                    //        printf("%s\n", gcfg->node[i]->inst_ref[j].ref_type == AH ? "AH": gcfg->node[i]->inst_ref[j].ref_type == AM ? "AM" : "?");
                    //    else
                    //        printf("line number: %d\n", ass_lookup_pers(gcfg->node[i]->inst_ref[j].acs_in->sets[set_id], m));
                    //}
				//if (gcfg->node[i]->inst_ref[j].ref_type == NC) {
					if (gcfg->node[i]->inst_ref[j].acs_in) {
						// if the memory block to find is not in the virtual line
                        //if (ass_lookup_pers(gcfg->node[i]->inst_ref[j].acs_in->sets[set_id], m) != -1 &&
						if (als_lookup_pers(gcfg->node[i]->inst_ref[j].acs_in->sets[set_id]->lines[icache.assoc], m) == -1) {
							gcfg->node[i]->inst_ref[j].ref_type = NCP;
						}
                    }
				}
            }
        }
		// restore cache associativity
		//icache.assoc--;
	}
/*
	// reset all the abstract cache states for each intstruction
	for (i = 0; i < gcfg->num_node; i++) {
		for (j = 0; j < gcfg->node[i]->num_inst; j++) {
			acs_free(gcfg->node[i]->inst_ref[j].acs_in);
			acs_free(gcfg->node[i]->inst_ref[j].acs_out);
			acs_free(gcfg->node[i]->inst_ref[j].acs_in_prev);
			acs_free(gcfg->node[i]->inst_ref[j].acs_out_prev);

			gcfg->node[i]->inst_ref[j].acs_in = NULL;
			gcfg->node[i]->inst_ref[j].acs_out = NULL;
			gcfg->node[i]->inst_ref[j].acs_in_prev = NULL;
			gcfg->node[i]->inst_ref[j].acs_out_prev = NULL;
		}
    }
*/

	return 0;
}

// print the memory reference category for each instruction
void print_catg(void)
{
	long long int i, j, num = 0;
	for (i = 0; i < gcfg->num_node; i++)
		for (j = 0; j < gcfg->node[i]->num_inst; j++) {
			if (gcfg->node[i]->inst_ref[j].ref_type == AH)
				printf("inst[%-3lld] : always hit\n", num);
			else if (gcfg->node[i]->inst_ref[j].ref_type == AM)
				printf("inst[%-3lld] : always miss\n", num);
			else if (gcfg->node[i]->inst_ref[j].ref_type == NCP)
				printf("inst[%-3lld] : first miss\n", num);
			else
				printf("inst[%-3lld] : not categorized\n", num);
			num++;
		}
}

void init_whole_acs()
{
    int i, j;

   	for (i = 0; i < gcfg->num_node; i++) {
        gcfg->node[i]->inst_ref[0].acs_in = acs_new();
        gcfg->node[i]->inst_ref[0].acs_out = acs_new();

		//for (j = 0; j < gcfg->node[i]->num_inst; j++) {
		for (j = 1; j < gcfg->node[i]->num_inst; j++) {
            //gcfg->node[i]->inst_ref[j].acs_in = acs_new();
            gcfg->node[i]->inst_ref[j].acs_out = acs_new();
            //gcfg->node[i]->inst_ref[j].acs_in_prev = acs_new();
            //gcfg->node[i]->inst_ref[j].acs_out_prev = acs_new();
		}
        gcfg->node[i]->inst_ref[0].acs_in_prev = acs_new();
        gcfg->node[i]->inst_ref[gcfg->node[i]->num_inst-1].acs_out_prev = acs_new();
    }
}

void free_whole_acs()
{
    int i, j;

	for (i = 0; i < gcfg->num_node; i++) {
        acs_free(gcfg->node[i]->inst_ref[0].acs_in);
        acs_free(gcfg->node[i]->inst_ref[0].acs_out);
		//for (j = 0; j < gcfg->node[i]->num_inst; j++) {
		for (j = 1; j < gcfg->node[i]->num_inst; j++) {
			//acs_free(gcfg->node[i]->inst_ref[j].acs_in);
			acs_free(gcfg->node[i]->inst_ref[j].acs_out);
			//acs_free(gcfg->node[i]->inst_ref[j].acs_in_prev);
			//acs_free(gcfg->node[i]->inst_ref[j].acs_out_prev);
        }
        acs_free(gcfg->node[i]->inst_ref[0].acs_in_prev);
        acs_free(gcfg->node[i]->inst_ref[gcfg->node[i]->num_inst-1].acs_out_prev);
		for (j = 0; j < gcfg->node[i]->num_inst; j++) {
			gcfg->node[i]->inst_ref[j].acs_in = NULL;
			gcfg->node[i]->inst_ref[j].acs_out = NULL;
			gcfg->node[i]->inst_ref[j].acs_in_prev = NULL;
			gcfg->node[i]->inst_ref[j].acs_out_prev = NULL;
		}
    }
}

// entry function of instruction cache analysis
int icache_analysis(int capacity, int line_size, int assoc)
{
	long long int i, j;
	//long long int ah = 0, am = 0, ncp = 0, nc = 0;
	icache_init(capacity, line_size, assoc);

    init_whole_acs();

	cfg_trav(MUST);
	// collect information for always-hit references
	ref_catg(MUST);
    acs_reset();
	// traverse CFG for must analysis
	// traverse CFG for may analysis
	cfg_trav(MAY);
	// collect information for always-miss references
	ref_catg(MAY);
    acs_reset();
	// traverse CFG for must analysis
	// traverse CFG for persistence analysis
	cfg_trav(PERSISTENCE);
	// collect information for first-hit references
	ref_catg(PERSISTENCE);

    free_whole_acs();

	for (i = 0; i < gcfg->num_node; i++) {
		for (j = 0; j < gcfg->node[i]->num_inst; j++) {
			if (gcfg->node[i]->inst_ref[j].ref_type == AH)
				gcfg->node[i]->ah++;
			else if (gcfg->node[i]->inst_ref[j].ref_type == AM)
				gcfg->node[i]->am++;
			else if (gcfg->node[i]->inst_ref[j].ref_type == NCP)
				gcfg->node[i]->ncp++;
			else
				gcfg->node[i]->nc++;
		}
        //printf("%d am: %d, nc: %d\n", i, gcfg->node[i]->am, gcfg->node[i]->nc);
    }
	return 0;
}

