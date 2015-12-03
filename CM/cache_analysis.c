#include "CM.h"
#include "DFS.h"
#include "CFG_traversal.h"
#include "loop.h"
#include "util.h"

#include "gurobi_c.h"
#include "cache_analysis.h"
#include "DMA.h"

#include "math.h"

//#define CA_DEBUG

#define LOG2(num) (log(num)/log(2.0))
#define OFFSET_MASKOUT(addr) ((addr) & ~offset_mask)
#define OFFSET(addr) (int)((addr) & offset_mask)
#define INDEX(addr) (int)(((addr) & index_mask) >> offset_len)
#define TAG(addr) (int)(((addr) & tag_mask) >> (offset_len + index_len))

#define MIN(a,b) ((a) < (b) ? (a):(b))
#define MAX(a,b) ((a) > (b) ? (a):(b))

int cache_size = -1;
int cache_line_size;
int associativity;
int num_set;
int num_line;

int offset_len;
int index_len;
int tag_len;
long long int offset_mask;
long long int index_mask;
long long int tag_mask;

typedef enum _AnalysisMode {MUST, MAY, PERS} AnalysisMode;
typedef enum _MinMaxMode {Min, Max} MinMaxMode;

int bDebugPers = 0;

////// abstract cache state data types
//#define STATIC_MEM_ALLOCATION 1

#ifndef STATIC_MEM_ALLOCATION
#define INIT_BL_SIZE 10
#else 
#define STATIC_BL_SIZE 10
#endif

typedef struct _blocks_list {
#ifndef STATIC_MEM_ALLOCATION
    long long int *blks;
    int blSize; 
#else
    long long int blks[STATIC_BL_SIZE];
#endif
    int nBlk;
} blType;

typedef struct _abst_line {
    blType* blocks;
    blType* blocks_pers;
} alType;

typedef struct _cache_set {
    alType *lines;
} asType;

typedef struct _cache {
    asType *sets;
} acType;

static inline void bl_addBlock(blType *bl, long long int addr)
{
    if (bl) {
        int b;
        for (b = 0; b < bl->nBlk; b++) {
            if (bl->blks[b] == addr)
                return;
        }

#ifndef STATIC_MEM_ALLOCATION
        if (bl->nBlk == bl->blSize) {
            long long int *blks = (long long int*)realloc(bl->blks, sizeof(long long int) * (bl->blSize*2));
            if (blks == NULL) {
                printf("mem realloc error @bl_addBlock\n");
                exit(1);
            }
            bl->blks = blks;
            bl->blSize *= 2;
        }
#else
        if (bl->nBlk == STATIC_BL_SIZE) {
            printf("bl grows larger than the allocated size...\n");
            exit(1);
        }
#endif
        bl->blks[bl->nBlk++] = addr;
    }
    else {
        printf("blType* bl is NULL @bl_addBlock\n");
        exit(1);
    }
}

void bl_rmBlock(blType* bl, long long int addr)
{
    if (bl) {
        int b = 0;
        for (b = 0; b < bl->nBlk; b++) {
            if (bl->blks[b] == addr) {
                int b2;
                for (b2 = b+1; b2 < bl->nBlk; b2++)
                    bl->blks[b2-1] = bl->blks[b2];
                bl->nBlk--;
            }
        }
    }
    else {
        printf("blType* bl is NULL @bl_rmBlock\n");
        exit(1);
    }
}

void bl_copy(blType* src, blType* dst) 
{
    if (src == NULL || dst == NULL) {
        printf("src or dst is NULL @bl_copy\n");
        exit(1);
    }

    dst->nBlk = 0;
    if (src->nBlk) {
        int b;
        for (b = 0; b < src->nBlk; b++) 
            bl_addBlock(dst, src->blks[b]);
    }

/*
    dst->nBlk = src->nBlk;
    dst->blSize = src->blSize;
    if (dst->blks) 
        free(dst->blks);
    dst->blks = src->blks;
*/
}

void bl_merge(blType* src, blType* dst)
{
    if (src == NULL || dst == NULL) {
        printf("src or dst is NULL @bl_merge\n");
        exit(1);
    }

    int nBlk = 0;

    int b;
    for (b = 0; b < src->nBlk; b++)
        bl_addBlock(dst, src->blks[b]);
}

void bl_init(blType** bl)
{
/*
    if (*bl == NULL) {
        *bl = (blType*)calloc(1, sizeof(blType));
        if (*bl == NULL) {
            printf("mem alloc error with bl @bl_init\n");
            exit(1);
        }
#ifndef STATIC_MEM_ALLOCATION
        (*bl)->blks = (long long int*)calloc(INIT_BL_SIZE, sizeof(long long int));
        if ((*bl)->blks == NULL) {
            printf("mem alloc error with blks @bl_init\n");
            exit(1);
        }
        (*bl)->blSize = INIT_BL_SIZE;
#endif
    }
*/
    /*     
    if ((*bl)->blks) {
        //printf("%llx\n", (long long int)((*bl)->blks));
        free((*bl)->blks);
        (*bl)->blks = NULL;
    }
    (*bl)->blks = (long long int*)calloc(INIT_BL_SIZE, sizeof(long long int));
    if ((*bl)->blks == NULL) {
        printf("mem alloc error with blks @bl_init\n");
        exit(1);
    }
    (*bl)->blSize = INIT_BL_SIZE;
    */
    (*bl)->nBlk = 0;
} 

// each node has acNodeType*
// each instruction has (ac_in, ac_out) 
typedef struct _abstract_cache_per_Node {
    acType **ac_in;
    acType **ac_out;
    enum verdict {NC, AH, AM, FM} *v;
} acNodeType;

// acNodeType for all nodes
acNodeType* AC = NULL;

void ac_free(acType** ac)
{
    if (*ac) {
        if ((*ac)->sets) {
            int s; 
            for (s = 0; s < num_set; s++) {
                asType *as = &((*ac)->sets[s]);
                if (as->lines) {
                    int l;
                    for (l = 0; l < num_line; l++) {
                        alType *al = &(as->lines[l]);
                        if (al->blocks) {
#ifndef STATIC_MEM_ALLOCATION
                            if (al->blocks->blks)
                                free(al->blocks->blks);
                            al->blocks->blks = NULL;
#endif
                            free(al->blocks);
                        }
                        al->blocks = NULL;
                        if (al->blocks_pers) {
#ifndef STATIC_MEM_ALLOCATION
                            if (al->blocks_pers->blks)
                                free(al->blocks_pers->blks);
                            al->blocks_pers->blks = NULL;
#endif
                            free(al->blocks_pers);
                        }
                        al->blocks_pers = NULL;
                    }

                    free(as->lines);
                    as->lines = NULL;
                }
            }
            free((*ac)->sets);
            (*ac)->sets = NULL;
        }
        free(*ac);
        *ac = NULL;
    }
}

void AC_free()
{
    if (AC != NULL) {
        int n;
        for (n = 0; n < nNode; n++) {
            int i;
            for (i = 0; i < nodes[n]->S; i++) {
                ac_free(&(AC[n].ac_in[i]));
                ac_free(&(AC[n].ac_out[i]));
            }
            free(AC[n].ac_in);
            AC[n].ac_in = NULL;
            free(AC[n].ac_out);
            AC[n].ac_out = NULL;
            free(AC[n].v);
            AC[n].v = NULL;
        }

        free(AC);
        AC = NULL;
    }
}

void as_init(asType* as)
{
    if (as) {
        int l;
        for (l = 0; l < num_line; l++) {
            bl_init(&(as->lines[l].blocks));
            bl_init(&(as->lines[l].blocks_pers));
        }
    }
    else {
        printf("asType* as is NULL @as_init\n");
        exit(1);
    }
}

void ac_init(acType* ac)
{
    if (ac) {
        int s;
        for (s = 0; s < num_set; s++) {
            as_init(&(ac->sets[s]));
        }
    }
    else {
        // something's not right
        printf("acType* ac is NULL @ac_init\n");
        exit(1);
    }
}

acType* ac_new()
{
    acType* ac = (acType*)calloc(1, sizeof(acType));

    ac->sets = (asType*)calloc(num_set, sizeof(asType));

    int s;
    for (s = 0; s < num_set; s++) {
        asType* as = &(ac->sets[s]);
        as->lines = (alType*)calloc(num_line, sizeof(alType));
        int l;
        for (l = 0; l < num_line; l++) {
            as->lines[l].blocks = (blType*)calloc(1, sizeof(blType));
#ifndef STATIC_MEM_ALLOCATION
            as->lines[l].blocks->blks = (long long int*)calloc(INIT_BL_SIZE, sizeof(long long int));
            as->lines[l].blocks->blSize = INIT_BL_SIZE;
#endif
            as->lines[l].blocks->nBlk = 0;

            as->lines[l].blocks_pers = (blType*)calloc(1, sizeof(blType));
#ifndef STATIC_MEM_ALLOCATION
            as->lines[l].blocks_pers->blks = (long long int*)calloc(INIT_BL_SIZE, sizeof(long long int));
            as->lines[l].blocks_pers->blSize = INIT_BL_SIZE;
#endif
            as->lines[l].blocks_pers->nBlk = 0;
        }
    }

    return ac;
}

void AC_new()
{
    if (AC != NULL) {
        AC_free();
    }
    AC = (acNodeType*)calloc(nNode, sizeof(acNodeType));
    if (AC == NULL) {
        printf("mem allocation error @AC_new\n");
        exit(1);
    }

    int n;
    for (n = 0; n < nNode; n++) {
        AC[n].ac_in = (acType**)calloc(nodes[n]->S, sizeof(acType*));
        AC[n].ac_out = (acType**)calloc(nodes[n]->S, sizeof(acType*));
        AC[n].v = (enum verdict*)calloc(nodes[n]->S, sizeof(enum verdict));

        if (AC[n].ac_in == NULL || AC[n].ac_out == NULL || AC[n].v == NULL) {
            printf("mem allocation error @AC_new\n");
            exit(1);
        }

        int i;
        for (i = 0; i < nodes[n]->S; i++) {
            AC[n].ac_in[i] = ac_new();
            AC[n].ac_out[i] = ac_new();
        } 
    }
}

int al_lookup(alType* al, long long int addr, AnalysisMode aMode)
{
    long long int m = OFFSET_MASKOUT(addr);
    
    if (al) {
        if (aMode != PERS) {
            if (al->blocks) {
                blType* bl = al->blocks;
                int b;
                for (b = 0; b < bl->nBlk; b++) {
                    if (bl->blks[b] == m)
                        return 1;
                }
            }
            else {
                printf("al->blocks is NULL @al_lookup\n");
                exit(1);
            }
        }
        else {
            if (al->blocks_pers) {
                blType* bl = al->blocks_pers;
                int b;
                for (b = 0; b < bl->nBlk; b++) {
                    if (bl->blks[b] == m)
                        return 1;
                }
            }
            else {
                printf("al->blocks_pers is NULL @al_lookup\n");
                exit(1);
            }
        }
    }
    else {
        printf("al is NULL @al_lookup\n");
        exit(1);
    }

    return 0;
}

int as_lookup(asType* as, long long int addr, AnalysisMode aMode)
{
    int nLine;
    if (aMode != PERS)
        nLine = num_line-1;
    else
        nLine = num_line;

    int line_no = -1;
    if (as) {
        int l;
        for (l = 0; l < nLine; l++) {
            if (al_lookup(&(as->lines[l]), addr, aMode)) {
                if (line_no != -1) {
                    printf("Something's wrong. Addr %lld is present in multiple lines in a set @as_lookup\n", addr);
                }
                line_no = l;
            }
        }
    }
    else {
        printf("as is NULL @as_lookup\n");
        exit(1);
    }

    return line_no;
}

int ac_lookup(acType* ac, long long int addr, AnalysisMode aMode)
{
    if (ac) {
        int l = as_lookup(&(ac->sets[INDEX(addr)]), addr, aMode);
        return l;
    }
    else {
        printf("ac is NULL @ac_lookup\n");
        exit(1);
    }

    return -1; 
}

/*
void al_copy(alType* src, alType* dst, AnalysisMode aMode)
{
    if (src == NULL || dst == NULL) {
        printf("src and/or dst is NULL @al_copy\n");
        exit(1);
    }
    
    bl_init(&(dst->blocks));

    int b = 0;
    for (b = 0; b < src->blocks->nBlk; b++) {
        bl_addBlock(dst->blocks, src->blocks->blks[b]);
    }
    if (aMode == PERS) {
        bl_init(&(dst->blocks_pers));

        for (b = 0; b < src->blocks_pers->nBlk; b++) {
            bl_addBlock(dst->blocks_pers, src->blocks_pers->blks[b]);
        }
    }
}
*/

void as_copy(asType* src, asType* dst, AnalysisMode aMode)
{
    if (src == NULL || dst == NULL) {
        printf("src and/or dst is NULL @as_copy\n");
        exit(1);
    }

    int l;
    for (l = 0; l < num_line-1; l++) {
        alType* al_src = &(src->lines[l]);
        alType* al_dst = &(dst->lines[l]);

        bl_init(&(al_dst->blocks));

        int b;
        for (b = 0; b < al_src->blocks->nBlk; b++)
            bl_addBlock(al_dst->blocks, al_src->blocks->blks[b]);
    }

    if (aMode == PERS) {
        for (l = 0; l < num_line; l++) {
            alType* al_src = &(src->lines[l]);
            alType* al_dst = &(dst->lines[l]);

            bl_init(&(al_dst->blocks_pers));

            int b;
            for (b = 0; b < al_src->blocks_pers->nBlk; b++)
                bl_addBlock(al_dst->blocks_pers, al_src->blocks_pers->blks[b]);
        }
    }
}

void ac_copy(acType* src, acType* dst, AnalysisMode aMode)
{
    if (src == NULL || dst == NULL) {
        printf("src or dst is NULL @ac_copy\n");
        exit(1);
    }

    int s;
    for (s = 0; s < num_set; s++) {
        as_copy(&(src->sets[s]), &(dst->sets[s]), aMode);
    }
}

void as_intersect(asType* src1, asType* src2, asType* dst, AnalysisMode aMode, MinMaxMode minmax)
{
    if (src1 == NULL || src2 == NULL || dst == NULL) {
        printf("src1, src2, and/or dst is NULL @as_intersect\n");
        exit(1);
    }

    //as_init(dst);

    int l, b, nLine;

    if (aMode != PERS)
        nLine = num_line-1;
    else
        nLine = num_line;

    for (l = 0; l < nLine; l++) {
        blType* bl;
        if (aMode != PERS)
            bl = src1->lines[l].blocks;
        else
            bl = src1->lines[l].blocks_pers;

        if (bl == NULL) {
            printf("bl is NULL @as_intersect\n");
            exit(1);
        }

        for (b = 0; b < bl->nBlk; b++) {
            long long int addr = bl->blks[b];

            int line1 = l;
            int line2 = as_lookup(src2, addr, aMode);

            if (line2 != -1) {
                int lineDST = (minmax == Min) ? MIN(line1, line2):MAX(line1, line2);

                if (aMode != PERS)
                    bl_addBlock(dst->lines[lineDST].blocks, addr);
                else
                    bl_addBlock(dst->lines[lineDST].blocks_pers, addr);
            }
        }
    }
}

void ac_intersect(acType* src1, acType* src2, acType* dst, AnalysisMode aMode, MinMaxMode minmax)
{
    if (src1 == NULL || src2 == NULL || dst == NULL) {
        printf("src1, src2, and/or dst is NULL @ac_intersect\n");
        exit(1);
    }

    //ac_init(dst);

    int s;
    for (s = 0; s < num_set; s++) 
        as_intersect(&(src1->sets[s]), &(src2->sets[s]), &(dst->sets[s]), aMode, minmax);
}

void as_union(asType* src1, asType* src2, asType* dst, AnalysisMode aMode, MinMaxMode minmax)
{
    if (src1 == NULL || src2 == NULL || dst == NULL) {
        printf("src1, src2, and/or dst is NULL @as_union\n");
        exit(1);
    }

    //as_init(dst);

    int l, b, nLine;

    if (aMode != PERS)
        nLine = num_line-1;
    else
        nLine = num_line;

    for (l = 0; l < nLine; l++) {
        blType* bl;

        // Look at blocks in src1
        if (aMode != PERS)
            bl = src1->lines[l].blocks;
        else
            bl = src1->lines[l].blocks_pers;

        if (bl == NULL) {
            printf("bl is NULL @as_union\n");
            exit(1);
        }

        for (b = 0; b < bl->nBlk; b++) {
            long long int addr = bl->blks[b];

            int line1 = l;
            int line2 = as_lookup(src2, addr, aMode);

            int lineDST = l;
            if (line2 != -1)
                lineDST = (minmax == Min) ? MIN(line1, line2):MAX(line1, line2);

            if (aMode != PERS)
                bl_addBlock(dst->lines[lineDST].blocks, addr);
            else
                bl_addBlock(dst->lines[lineDST].blocks_pers, addr);
        }

        // Look at blocks in src2
        if (aMode != PERS)
            bl = src2->lines[l].blocks;
        else
            bl = src2->lines[l].blocks_pers;

        if (bl == NULL) {
            printf("bl is NULL @as_union\n");
            exit(1);
        }

        for (b = 0; b < bl->nBlk; b++) {
            long long int addr = bl->blks[b];

            if (as_lookup(src1, addr, aMode) == -1) {
                int lineDST = l;

                if (aMode != PERS)
                    bl_addBlock(dst->lines[lineDST].blocks, addr);
                else
                    bl_addBlock(dst->lines[lineDST].blocks_pers, addr);
            }
        }
    }
}

void ac_show(acType* ac, AnalysisMode aMode) {
    if (ac) {
        int nLine;
        if (aMode != PERS)
            nLine = num_line-1;
        else
            nLine = num_line;

        int s;
        for (s = 0; s < num_set; s++) {
            printf("set %d\n", s); 
            int l;
            for (l = 0; l < nLine; l++) {
                printf("\tline %d [ ", l); 
                int b;
                for (b = 0; b < ac->sets[s].lines[l].blocks->nBlk; b++) {
                    printf("0x%llx (%lld) ", ac->sets[s].lines[l].blocks->blks[b], ac->sets[s].lines[l].blocks->blks[b]);
                }
                if (aMode == PERS) {
                    printf("], PERS [ ");
                    for (b = 0; b < ac->sets[s].lines[l].blocks_pers->nBlk; b++) {
                        printf("0x%llx (%lld) ", ac->sets[s].lines[l].blocks_pers->blks[b], ac->sets[s].lines[l].blocks_pers->blks[b]);
                    }
                }
                printf("]\n"); 
            }
        }
    }
}

void ac_union(acType* src1, acType* src2, acType* dst, AnalysisMode aMode, MinMaxMode minmax)
{
    if (src1 == NULL || src2 == NULL || dst == NULL) {
        printf("src1, src2, and/or dst is NULL @ac_union\n");
        exit(1);
    }

    //ac_init(dst);

    int s;
    for (s = 0; s < num_set; s++) 
        as_union(&(src1->sets[s]), &(src2->sets[s]), &(dst->sets[s]), aMode, minmax);
}

int as_cmp(asType* set1, asType* set2, AnalysisMode aMode)
{
    if (set1 == NULL || set2 == NULL)
        return 1;

    int l, b;
    int nLine;
    if (aMode != PERS)
        nLine = num_line-1;
    else
        nLine = num_line;

    for (l = 0; l < nLine; l++) {
        alType* al1 = &(set1->lines[l]);
        alType* al2 = &(set2->lines[l]);

        if (aMode != PERS) {
            if (al1->blocks->nBlk != al2->blocks->nBlk)
                return 1;

            for (b = 0; b < al1->blocks->nBlk; b++) {
                if (al_lookup(al2, al1->blocks->blks[b], MUST) == -1) // something not PERS
                    return 1;
            }
        }
        else {
            if (al1->blocks_pers->nBlk != al2->blocks_pers->nBlk)
                return 1;

            for (b = 0; b < al1->blocks_pers->nBlk; b++) {
                if (al_lookup(al2, al1->blocks_pers->blks[b], PERS) == -1) 
                    return 1;
            }
        }
    }

    return 0;
}

int ac_cmp(acType* ac1, acType* ac2, AnalysisMode aMode)
{
    if (ac1 == NULL || ac2 == NULL) {
        printf("ac is NULL @ac_cmp\n");
        exit(1);
    }

    int s;
    for (s = 0; s < num_set; s++) {
        if (as_cmp(&(ac1->sets[s]), &(ac2->sets[s]), aMode)) {
            return 1;
        }
    }

    return 0;
}

void join(acType* ac1, acType* ac2, acType* dst, AnalysisMode aMode)
{
    if (ac1 == NULL || ac2 == NULL || dst == NULL) {
        printf("ac1, ac2, and/or dst is NULL @join\n");
        exit(1);
    }

    ac_init(dst);

    switch (aMode) {
        case MUST:
            ac_intersect(ac1, ac2, dst, aMode, Max);
            break;
        case MAY:
            ac_union(ac1, ac2, dst, aMode, Min);
            break;
        case PERS:
            ac_union(ac1, ac2, dst, PERS, Max);
            ac_union(ac1, ac2, dst, MAY, Min); // PERS runs along with MAY
            break;
    }
}

void update(acType* src, long long int addr, acType* dst, AnalysisMode aMode)
{
    if (src == NULL || dst == NULL) {
        printf("src and/or dst is NULL @update\n");
        exit(1);
    }

    //ac_init(dst);

    ac_copy(src, dst, aMode);
    if (aMode == PERS)
        ac_copy(src, dst, MAY); // PERS runs along with MAY

    long long int x = OFFSET_MASKOUT(addr);
    int set = INDEX(addr);

    // PERS runs along with MAY and looks up blocks, rather than blocks_pers to evaluate bMayEvict
    int h;
    if (aMode != PERS)
        h = ac_lookup(src, x, aMode);
    else
        h = ac_lookup(src, x, MAY);
    
    alType* lines = dst->sets[set].lines;

    // See the equations in the paper to understand what's happening here
    // The following directly implements the equations 
    if (aMode == MUST) {
        // Hit 
        if (h == 0) {
            // Do nothing
        }
        else if (h > 0) {
            // line[h] = line[h-1] + line[h] - x
            bl_merge(lines[h-1].blocks, lines[h].blocks);
            bl_rmBlock(lines[h].blocks, x);

            // let every line with age younger than h age by one
            // line[i] = line[i-1] (0 < i < h)
            int l;
            for (l = h-1; l > 0; l--)
                bl_copy(lines[l-1].blocks, lines[l].blocks);

            // line 0 contains m only
            lines[0].blocks->nBlk = 0; // can't do bl_init(&(lines[0].blocks)) because it frees lines[0].blocks->blk which is copied to lines[1].blocks->blk
            bl_addBlock(lines[0].blocks, x);
        }
        // Cache miss
        else {
            // let every line age by one
            // line[i] = line[i-1]
            int l;
            for (l = associativity-1; l >= 1; l--)
                bl_copy(lines[l-1].blocks, lines[l].blocks);

            // line 0 contains m only
            lines[0].blocks->nBlk = 0; // can't do bl_init(&(lines[0].blocks)) because it frees lines[0].blocks->blk which is copied to lines[1].blocks->blk
            bl_addBlock(lines[0].blocks, x);
        }
    }
    else { // MAY or PERS (PERS runs along with MAY)
        // Hit
        // hit at age num_line-1 (=associativity) won't do anything. line[h] will be evicted anyway
        if (h >= 0 && h < associativity) {
            // line[h+1] = line[h+1] + line[h] - x
            bl_merge(lines[h].blocks, lines[h+1].blocks);
            bl_rmBlock(lines[h+1].blocks, x);

            // let every line with age of h or younger by one
            // line[i] = line[i-1] (0 < i <= h)
            int l;
            for (l = h; l > 0; l--)
                bl_copy(lines[l-1].blocks, lines[l].blocks);

            // line 0 contains m only
            lines[0].blocks->nBlk = 0; // can't do bl_init(&(lines[0].blocks)) because it frees lines[0].blocks->blk which is copied to lines[1].blocks->blk
            bl_addBlock(lines[0].blocks, x);
        }
        else {
            // let every line age by one
            // line[i] = line[i-1]
            int l;
            for (l = associativity-1; l >= 1; l--)
                bl_copy(lines[l-1].blocks, lines[l].blocks);

            // line 0 contains m only
            lines[0].blocks->nBlk = 0; // can't do bl_init(&(lines[0].blocks)) because it frees lines[0].blocks->blk which is copied to lines[1].blocks->blk
            bl_addBlock(lines[0].blocks, x);
        }
    }
    if (aMode == PERS) {
        // PERS (PERS runs along with MAY)
        int bMayEvict = 0;
        if (h != -1) {
            // the number of blocks other than x
            // (x is in the line because we just looked it up, so the number is just nBlk - 1)
            if (src->sets[set].lines[h].blocks->nBlk-1 >= associativity)
                bMayEvict = 1;
        }

        static long long prev_addr = -1;
        int bDebugPersLocal = 0;
        if (prev_addr != x) {
            prev_addr = x;
            if (bDebugPers == 1)
                bDebugPersLocal = 1;
        }

        if (bDebugPersLocal) {
            printf("addr %lld, MayEvict : %d\n", x, bMayEvict); 

            int l;
            for (l = associativity-1; l >= 0; l--) {
                printf("line %d: num_block: %d [ ", l, src->sets[set].lines[l].blocks->nBlk);
                int b;
                for (b = 0; b < src->sets[set].lines[l].blocks->nBlk; b++) {
                    printf("%lld ", src->sets[set].lines[l].blocks->blks[b]);
                }
                printf("]\n");
            }
        }

        if (bMayEvict) {
            // line[A+1] = line[A] + line[A+1] - x 
            bl_merge(lines[associativity-1].blocks_pers, lines[associativity].blocks_pers);
            bl_rmBlock(lines[associativity].blocks_pers, x);

            if (bDebugPersLocal) {
                printf("line %d: num_block_pers: %d [ ", associativity, lines[associativity].blocks_pers->nBlk);
                int b;
                for (b = 0; b < lines[associativity].blocks_pers->nBlk; b++)
                    printf("%lld ", lines[associativity].blocks_pers->blks[b]);
                printf("]\n");
            }

            // line[i] = line[i-1] - x (0 < i <= A)
            int l;
            for (l = associativity-1; l > 0; l--) {
                bl_copy(lines[l-1].blocks_pers, lines[l].blocks_pers);
                bl_rmBlock(lines[l].blocks_pers, x);

                if (bDebugPersLocal) {
                    printf("line %d: num_block_pers: %d [ ", l, lines[l].blocks_pers->nBlk);
                    int b;
                    for (b = 0; b < lines[l].blocks_pers->nBlk; b++)
                        printf("%lld ", lines[l].blocks_pers->blks[b]);
                    printf("]\n");
                }
            }
        }
        else {
            // line[A+1] = line[A+1] - x
            bl_rmBlock(lines[associativity].blocks_pers, x);

            if (bDebugPersLocal) {
                printf("line %d: num_block_pers: %d [ ", associativity, lines[associativity].blocks_pers->nBlk);
                int b;
                for (b = 0; b < lines[associativity].blocks_pers->nBlk; b++)
                    printf("%lld ", lines[associativity].blocks_pers->blks[b]);
                printf("]\n");
            }

            // line[A] = line[A] + line[A-1] - x
            bl_merge(lines[associativity-2].blocks_pers, lines[associativity-1].blocks_pers);
            bl_rmBlock(lines[associativity-1].blocks_pers, x);

            if (bDebugPersLocal) {
                printf("line %d: num_block_pers: %d [ ", associativity-1, lines[associativity-1].blocks_pers->nBlk);
                int b;
                for (b = 0; b < lines[associativity-1].blocks_pers->nBlk; b++)
                    printf("%lld ", lines[associativity-1].blocks_pers->blks[b]);
                printf("]\n");
            }

            // line[i] = line[i-1] - x (0 < i < A)
            int l;
            for (l = associativity-2; l > 0; l--) {
                bl_copy(lines[l-1].blocks_pers, lines[l].blocks_pers);
                bl_rmBlock(lines[l].blocks_pers, x);

                if (bDebugPersLocal) {
                    printf("line %d: num_block_pers: %d [ ", l, lines[l].blocks_pers->nBlk);
                    int b;
                    for (b = 0; b < lines[l].blocks_pers->nBlk; b++)
                        printf("%lld ", lines[l].blocks_pers->blks[b]);
                    printf("]\n");
                }
            }
        }
        
        // line 0 contains m only
        lines[0].blocks_pers->nBlk = 0; // can't do bl_init(&(lines[0].blocks_pers)) because it frees lines[0].blocks_pers->blk which is copied to lines[1].blocks_pers->blk
        bl_addBlock(lines[0].blocks_pers, x);

        if (bDebugPersLocal)
            printf("line 0: num_block_pers: 1 [ %lld ]\n", x);
    }

}

int init_cache_analysis(int size, int line_size, int assoc)
{
    if (size <= 0 || line_size <= 0 || assoc <= 0)
        return -1;
    printf("cache size: %d, line_size: %d, associativity: %d\n", size, line_size, assoc);

/*
    if (ceilf(LOG2(size)) != LOG2(size) || 
        ceilf(LOG2(line_size)) != LOG2(line_size) || 
        ceilf(LOG2(assoc)) != LOG2(assoc) ||
        ceilf(LOG2(size/line_size/assoc)) != LOG2(size/line_size/assoc)) {
        printf("cache size, line size, associativity, and the number of sets should be a power of 2\n");
        return -1;
    }
*/
    cache_size = size;
    cache_line_size = line_size;
    associativity = assoc;
    num_set = size/line_size/assoc;
    num_line = associativity+1; // +1 for persistence analysis

    offset_len = LOG2(cache_line_size);
    index_len = LOG2(num_set);
    tag_len = sizeof(long long int) * 8 - offset_len - index_len;

    offset_mask = ~(ULLONG_MAX >> offset_len << offset_len);
    index_mask = ~((ULLONG_MAX >> (offset_len + index_len) << (offset_len + index_len)) | offset_mask);
    tag_mask = ~(offset_mask | index_mask);

    return 0;
}

void AC_reset()
{
    int n;
    for (n = 0; n < nNode; n++) {
        int i;
        for (i = 0; i < nodes[n]->S; i++) {
            ac_init(AC[n].ac_in[i]);
            ac_init(AC[n].ac_out[i]);
        }
    }
}

void perform_analysis_bb(BBType* node, AnalysisMode aMode)
{
    ///////////////////////////////////////////////////
    // 1. ac_in of the first intruction 
    // join AC from all predecessors
    acType* ac_pred_merged = NULL;
    acType* ac_loopTail_merged = NULL;
    if (node->predList) {
        ac_pred_merged = ac_new();

        // only one predecessor. no join, just copy
        if (node->predList->next == NULL) {
            BBType* predNode = node->predList->BB;

            ac_copy(AC[predNode->ID].ac_out[predNode->S-1], ac_pred_merged, aMode);
        }
        else {
            // Merge first two predecessors
            // ac_merged = merged ac of the first two predecessors
            BBListEntry* predEntry = node->predList;
            BBType* predNode1 = predEntry->BB;
            BBType* predNode2 = predEntry->next->BB;

            join(AC[predNode1->ID].ac_out[predNode1->S-1], AC[predNode2->ID].ac_out[predNode2->S-1], ac_pred_merged, aMode);

            // Merge ac_merged with ac_out of each of the predNodes
            predEntry = predEntry->next->next;
            while (predEntry) {
                acType* ac_merged_new = ac_new();
                BBType* predNode = predEntry->BB;

                join(ac_pred_merged, AC[predNode->ID].ac_out[predNode->S-1], ac_merged_new, aMode);

                // ac_merged is updated to ac_merged_new
                // it remains as the merged ac for all previous predecessors
                ac_free(&ac_pred_merged);
                ac_pred_merged = ac_merged_new;

                predEntry = predEntry->next;
            }
        }
    }
    if (node->bLoopHead) {
        ac_loopTail_merged = ac_new();

        if (node->loopTailList->next == NULL) {
            // only one loop tail
            BBType* loopTail = node->loopTailList->BB;

            ac_copy(AC[loopTail->ID].ac_out[loopTail->S-1], ac_loopTail_merged, aMode);
        }
        else {
            // Merge first two loop tails
            // ac_merged = merged ac of the first two loop tails
            BBListEntry* loopTailEntry = node->loopTailList;
            BBType* loopTail1 = loopTailEntry->BB;
            BBType* loopTail2 = loopTailEntry->next->BB;

            join(AC[loopTail1->ID].ac_out[loopTail1->S-1], AC[loopTail2->ID].ac_out[loopTail2->S-1], ac_loopTail_merged, aMode);

            // Merge ac_merged with ac_out of each of the loop tails 
            loopTailEntry = loopTailEntry->next->next;
            while (loopTailEntry) {
                acType* ac_merged_new = ac_new();
                BBType* loopTail = loopTailEntry->BB;

                join(ac_loopTail_merged, AC[loopTail->ID].ac_out[loopTail->S-1], ac_merged_new, aMode);

                // ac_merged is updated to ac_merged_new
                // it remains as the merged ac for all previous loop tails
                ac_free(&ac_loopTail_merged);
                ac_loopTail_merged = ac_merged_new;

                loopTailEntry = loopTailEntry->next;
            }
        }
    }

    // final merge with ac_in of the first instruction in the node
    if (ac_pred_merged) {
        if (ac_loopTail_merged) {
            acType* final_ac = ac_new();
            join(ac_pred_merged, ac_loopTail_merged, final_ac, aMode);
            ac_free(&ac_pred_merged);
            ac_free(&ac_loopTail_merged);
            ac_copy(final_ac, AC[node->ID].ac_in[0], aMode);
            ac_free(&final_ac);
        }
        else {
            ac_copy(ac_pred_merged, AC[node->ID].ac_in[0], aMode);
            ac_free(&ac_pred_merged);
        }
    }
    else {
        if (ac_loopTail_merged) {
            ac_copy(ac_loopTail_merged, AC[node->ID].ac_in[0], aMode);
            ac_free(&ac_loopTail_merged);
        }
        else {
            // no predecessor. maybe the source node
        }
    }

    ///////////////////////////////////////////////////
    // Rest of the instructions in the basic block
    // Update. Pass AC from the first instruction to the next instruction
    int i;
    for (i = 0; i < node->S; i++) {
        long long int addr = node->addr + i*4;

        // mark the cache access from the current instruction (update of the ac_out of current instruction)
        if (node->bLoaded != 1)
            update(AC[node->ID].ac_in[i], addr, AC[node->ID].ac_out[i], aMode);
        else 
            ac_copy(AC[node->ID].ac_in[i], AC[node->ID].ac_out[i], aMode);
    
        if (i+1 < node->S)
            ac_copy(AC[node->ID].ac_out[i], AC[node->ID].ac_in[i+1], aMode);
    }
}

void perform_analysis(AnalysisMode aMode)
{
    INITSTACK(nNode)

    int bUpdated;

    acType** ac_prev = (acType**)calloc(nNode, sizeof(acType*));
    int n;
    for (n = 0; n < nNode; n++)
        ac_prev[n] = ac_new();

    int nIterationCount = 0;
 
    do {
        bUpdated = 0;

        initVisited();

        BBType* node;
        pushBB(rootNode, &stack);
        while ((node = popBB(&stack))) {
            if (node->bVisited == 1)
                continue;
            node->bVisited = 1;

            perform_analysis_bb(node, aMode);

            if (ac_cmp(AC[node->ID].ac_in[0], ac_prev[node->ID], aMode)) {
                //printf("there's a change at ac_in of node %d\n", node->ID);
                bUpdated = 1;
            }
            ac_copy(AC[node->ID].ac_in[0], ac_prev[node->ID], aMode);

            BBListEntry* succEntry = node->succList;
            while (succEntry) {
                pushBB(succEntry->BB, &stack);
                succEntry = succEntry->next;
            }
        }

        nIterationCount++;
    } while (bUpdated == 1 && nIterationCount < 50);
    printf("Iteration Count: %d\n", nIterationCount);

    for (n = 0; n < nNode; n++) {
        ac_free(&(ac_prev[n]));
    }
    free(ac_prev);

    // Access categorization
    for (n = 0; n < nNode; n++) {
        BBType* node = nodes[n];
        int i;
       for (i = 0; i < node->S; i++) {
            if (AC[node->ID].v[i] != NC) 
                continue;

            long long int addr = node->addr + i*4;

            switch (aMode) {
            case MUST:
                if (ac_lookup(AC[node->ID].ac_in[i], addr, aMode) != -1) {
                    AC[node->ID].v[i] = AH;
                }
                break;
            case MAY:
                if (ac_lookup(AC[node->ID].ac_in[i], addr, aMode) == -1) {
                    AC[node->ID].v[i] = AM;
                }
                break;
            case PERS: {
                int h = ac_lookup(AC[node->ID].ac_in[i], addr, aMode);
                if (h != -1 && h != associativity) { // this is the virtual line
                    AC[node->ID].v[i] = FM;
                }
                break;
                }
            }
        }
    }

    FREESTACK
}

int cache_analysis()
{
    if (cache_size == -1) {
        printf("cache anaysis is not initialized\n");
        return -1;
    }

    AC_new();

    AC_reset();
    perform_analysis(MUST);
    printf("--- MUST analysis is done ---\n");

    AC_reset();
    perform_analysis(MAY);
    printf("--- MAY analysis is done ---\n");

    AC_reset();
    perform_analysis(PERS);
    printf("--- PERSISTENCE analysis is done ---\n");

    // COLLECTING RESULTS
    int n;
    for (n = 0; n < nNode; n++) {
        BBType* node = nodes[n];
        enum verdict *v = AC[n].v;

        int nAH = 0, nAM = 0, nFM = 0;
        int i;
        for (i = 0; i < node->S; i++) {
            switch(v[i]) {
            case AH:
                nAH++;
                break;
            case FM:
                nFM++;
                break;
            case AM:
            case NC:
                nAM++;
            }
        }

#ifdef CA_DEBUG
        int bDiff = 0;
        if (node->CACHE_AH != nAH) {
            printf("Node %d - AH: %d (new) vs %d (old)\n", n, nAH, node->CACHE_AH);
            bDiff = 1;
        }
        if (node->CACHE_AM != nAM) {
            printf("Node %d - AM: %d (new) vs %d (old)\n", n, nAM, node->CACHE_AM);
            bDiff = 1;
        }
        if (node->CACHE_FM != nFM) {
            printf("Node %d - FM: %d (new) vs %d (old)\n", n, nFM, node->CACHE_FM);
            bDiff = 1;
        }
        if (bDiff)
            printf("--------------------------------------\n");
#endif
        node->CACHE_AH = nAH;
        node->CACHE_AM = nAM;
        node->CACHE_FM = nFM;
    }

    AC_free();
    return 0; 
}

void cache_wcet_analysis(int LATENCY)
{
    BBType* node;

    INITSTACK(nNode)
    
    GRBenv* env = NULL;
    GRBmodel *model = NULL;
    int error;
    
    double objVal;
    int status;

    int nIdx;
    int v, f, g;
    int nCall;

    FILE* fp = fopen("wcet_cache.lp", "w");
    if (fp == NULL)
    {
        printf("cannot create the output file\n");
        exit(1);
    }
    
    // Objective function
    fprintf(fp, "Minimize\n");
    fprintf(fp, "W0\n");
    
    fprintf(fp, "Subject to\n");

    initVisited();
    pushBB(rootNode, &stack);
    while ((node = popBB(&stack))) {
        if (node->bVisited == 1)
            continue;
        node->bVisited = 1;

        BBListEntry* succEntry = node->succList;
        while (succEntry) {
            pushBB(succEntry->BB, &stack);
            succEntry = succEntry->next;
        }

        // Wv's
        if (node->succList == NULL)
            fprintf(fp, "W%d - L%d = %d\n", node->ID, node->ID, node->N*node->S);
        else {
            if (node->succList->next == NULL) {
                BBType* succNode = node->succList->BB;

                fprintf(fp, "W%d - W%d - L%d = %d\n", node->ID, succNode->ID, node->ID, node->N*node->S);
            } else {
                fprintf(fp, "W%d - MX%d - L%d = %d\n", node->ID, node->ID, node->ID, node->N*node->S);

                BBListEntry* succEntry = node->succList;
                while (succEntry) {
                    fprintf(fp, "MX%d - W%d >= 0\n", node->ID, succEntry->BB->ID);
                    fprintf(fp, "W%d - MX%d - %d B%d_%d >= -%d\n", succEntry->BB->ID, node->ID, INT_MAX/2, succEntry->BB->ID, node->ID, INT_MAX/2);
                    succEntry = succEntry->next;
                }
                succEntry = node->succList;
                while (succEntry->next) {
                    fprintf(fp, "B%d_%d + ", succEntry->BB->ID, node->ID);
                    succEntry = succEntry->next;
                }
                fprintf(fp, "B%d_%d = 1\n", succEntry->BB->ID, node->ID);
            } // else (node->succList->next != NULL)
        } // else (node->SuccList != NULL)

        // Lv's
#ifndef LVZERO
        if (node->N) 
            fprintf(fp, "L%d = %d\n", node->ID, LATENCY*(node->CACHE_FM + node->N*node->CACHE_AM));
#else  
        fprintf(fp, "L%d = 0\n", node->ID);
#endif
    } // while
    
    fprintf(fp, "General\n");

    initVisited();
    pushBB(rootNode, &stack);
    while ((node = popBB(&stack))) {
        if (node->bVisited == 1)
            continue;
        node->bVisited = 1;

        if (node->succList) {
            if (node->succList->next != NULL)
                fprintf(fp, "MX%d\n", node->ID);
        }
        fprintf(fp, "W%d\n", node->ID);
        fprintf(fp, "L%d\n", node->ID);
 
        BBListEntry* succEntry = node->succList;
        while (succEntry) {
            pushBB(succEntry->BB, &stack);
            succEntry = succEntry->next;
        }
    }
    
    fprintf(fp, "Binary\n");

    initVisited();
    
    pushBB(rootNode, &stack);
    while ((node = popBB(&stack))) {
        if (node->bVisited == 1)
            continue;
        node->bVisited = 1;

        if (node->succList) {
            if (node->succList->next != NULL) {
                BBListEntry* succEntry = node->succList;
                while (succEntry) {
                    fprintf(fp, "B%d_%d\n", succEntry->BB->ID, node->ID);
                    succEntry = succEntry->next;
                }
            }
        } 

        BBListEntry* succEntry = node->succList;
        while (succEntry) {
            pushBB(succEntry->BB, &stack);
            succEntry = succEntry->next;
        }
    }
    
    fprintf(fp, "End\n");
    fclose(fp);

    ///////// SOLVER
    error = GRBloadenv(&env, "wcet_cache.log");
    if (error || env == NULL) {
        fprintf(stderr, "Error: could not create environment\n");
        exit(1);
    }
    GRBsetintparam(env, GRB_INT_PAR_OUTPUTFLAG, 1);
    //GRBsetintparam(env, GRB_INT_PAR_MIPFOCUS, 3);
    //GRBsetintparam(env, GRB_INT_PAR_NORMADJUST, 3);
    //GRBsetintparam(env, GRB_INT_PAR_PRESOLVE, 2);
    //GRBsetdblparam(env, GRB_DBL_PAR_PERTURBVALUE, 0.01);
    //GRBsetdblparam(env, GRB_DBL_PAR_MIPGAPABS, 100000);
    //GRBsetdblparam(env, GRB_DBL_PAR_TIMELIMIT, 1200);
    GRBsetdblparam(env, GRB_DBL_PAR_TIMELIMIT, 600);

    error = GRBreadmodel(env, "wcet_cache.lp", &model);
    if (error)  quit(error, env);

    error = GRBoptimize(model);
    if (error)
         quit(error, env);
    
    error = GRBgetintattr(model, GRB_INT_ATTR_STATUS, &status);
    if (error)  quit(error, env);
    
    objVal = 0;
    if (status == GRB_OPTIMAL) {
        int varIdx;
        static char *W0 = "W0";
        error = GRBgetvarbyname(model, W0, &varIdx);
        if (error)  quit(error, env);
        error = GRBgetdblattrelement(model, GRB_DBL_ATTR_X, varIdx, &objVal);
 
        int* dist = (int*)calloc(nNode, sizeof(int));
        BBType** tpoSortedNodes = (BBType**)malloc(sizeof(BBType*)*nNode);
        int tpoStartIdx = 0;

        initVisited();

        tpoStartIdx = TPOvisit(rootNode, tpoSortedNodes, 0);

        int maxDist = 0;
        BBType* maxDistNode;
        dist[rootNode->ID] = rootNode->N * rootNode->S;

        int i;
        for (i = tpoStartIdx-2; i >= 0; i--) {
            node = tpoSortedNodes[i];

            int maxCost = -1;

            BBListEntry* predEntry = node->predList;
            while (predEntry) {
                BBType* predNode = predEntry->BB;
                if (dist[predNode->ID] > maxCost) {
                    maxCost = dist[predNode->ID];
                }
                predEntry = predEntry->next;
            }

            dist[node->ID] = maxCost + (node->N * node->S);
#ifndef LVZERO
            dist[node->ID] += LATENCY*(node->CACHE_FM + node->N*node->CACHE_AM);
#endif
            if (dist[node->ID] > maxDist) {
                maxDist = dist[node->ID];
                maxDistNode = node;
            }
        }

        initVisited();

        int C = 0, W = 0;
        int *fCost = (int*)calloc(sizeof(int), nFunc);
        int nAM = 0, nFM = 0;

        pushBB(maxDistNode, &stack);
        while ((node = popBB(&stack))) {
            if (node->bVisited == 1)
                continue;
            node->bVisited = 1;
        
            int distMax = -1;
            BBType *maxDistPredNode;
            BBListEntry* predEntry = node->predList;
            while (predEntry) {
                BBType *predNode = predEntry->BB;
                if (dist[predNode->ID] >= distMax) {
                    maxDistPredNode = predNode;
                    distMax = dist[predNode->ID];
                }

                predEntry = predEntry->next;
            }

            int varIdxNode;
            static char varNameNode[32];
            memset(varNameNode, 0, 32);
            sprintf(varNameNode, "W%d", node->ID);
            error = GRBgetvarbyname(model, varNameNode, &varIdxNode);
            if (error)  quit(error, env);
                
            double wNode;
            error = GRBgetdblattrelement(model, GRB_DBL_ATTR_X, varIdxNode, &wNode);

            sprintf(varNameNode, "L%d", node->ID);
            error = GRBgetvarbyname(model, varNameNode, &varIdxNode);
               
            double lNode;
            error = GRBgetdblattrelement(model, GRB_DBL_ATTR_X, varIdxNode, &lNode);

            C += node->N*node->S;
#ifndef LVZERO
            if (node->N > 0)
                W += LATENCY*(node->CACHE_FM + node->N*node->CACHE_AM);
#endif
            if (dbgFlag)
                printf("node %d (N %d EC %d), W = %d = %d * (%d (FM)+ %d (AM))\n", node->ID, node->N, node->EC, LATENCY*(node->CACHE_FM + node->N*node->CACHE_AM), LATENCY, node->CACHE_FM, node->N*node->CACHE_AM);

            if (node->N > 1)
                nAM += node->CACHE_AM;
            else
                nFM += node->CACHE_AM;

            nFM += node->CACHE_FM;

            fCost[node->EC] += LATENCY*(node->CACHE_FM + node->N*node->CACHE_AM);

            if ((long long int)(wNode+0.5) != C+W) {
                printf("W value doesn't match at node %d. Estimated : %d vs. Calculated: %lld\n", node->ID, C+W, (long long int)(wNode+0.5));
            }

            pushBB(maxDistPredNode, &stack);
        }
 
        if ((long long int)objVal != C+W) {
            printf("Costs do not match. %lld vs %d\n", (long long int)objVal, C+W);
        }
        printf("WCET: %lld (C: %d (%.2f%%), W: %d (%.2f%%))\n", (long long int)objVal, C, C/objVal*100, W, W/objVal*100);
        free(tpoSortedNodes);
        free(dist);

        for (i = 0; i < nFunc; i++) {
            printf("Function %d: total W = %d\n", i, fCost[i]);
        }
        free(fCost);
        printf("Total AM:%d FM:%d\n", nAM, nFM);
    }

    if (error)
    {
        printf("ERROR: %s\n", GRBgeterrormsg(env));
        exit(1);
    }

    GRBfreemodel(model);
    GRBfreeenv(env);

    FREESTACK
}

