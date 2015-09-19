//
//  main.c
//  SPMCodeMappingForWCET
//
//  Created by Yooseong Kim on 1/30/13.
//  Copyright (c) 2013 Yooseong Kim. All rights reserved.
//

#include <unistd.h>
#include "CM.h"
#include "DFS.h"
#include "CFG_traversal.h"
#include "GCCFG.h"
#include "loop.h"
#include "CM_heuristic.h"
//#include "pf.h"
#include "util.h"

#include "y.tab.h"

#include "DMA.h"
#include "cache_analysis.h"

#include "CM_region_based.h"
#include "CM_region_free.h"
#include "split_wrapper.h"

int yyparse();

extern int yydebug;
extern FILE* yyin;

int SPMSIZE;

//int dbgFlag = 1;
int dbgFlag = 0;

int main(int argc, const char* argv[])
{   
   // input args parsing
    enum mode {CACHE, OPT_R, OPT_RF, FS, HEU, FIXED};
    enum mode runmode;

/*
    int bCacheOnly = 0;
    int bFixedOnly = 0;
    int bHeuristic = 0;
*/ 
    int bMapping1 = 0, bMappingN = 0;

    if (argc < 3)
    {
        printf("Usage: %s <inlined_cfg_file_name> <memsize> <or/orf/c/...>\n", argv[0]);
        printf("       %s g <cache_size> <cache_block_size> <associativity>\n", argv[0]);
        exit(1);
    }

    if (strcmp(argv[1], "g") == 0) {
        system("ica clean; ica make");
        char icastr[1024] = {0,};
        sprintf(icastr, "ica run --iconf %s %s %s", argv[2], argv[3], argv[4]);
        system(icastr);
        exit(1);
    }
    else {
        if (access(argv[1],R_OK) == -1) {
            printf("The file %s cannot be accessed. If you haven't generated an inlined CFG, please generate it first using the following command\n", argv[1]);
            printf("\t%s g <cache_size> <cache_block_size> <associativity>\n", argv[0]);
            exit(1);
        }
    }

    yydebug = 1;
    yyin = fopen(argv[1], "r");
    if (yyin == NULL)
    {
        printf("file %s cannot be found\n", argv[1]);
        return 0;
    }
    yyparse();
    
    initUnreachable();

    initIS();
    
    // find execution context change points
    findIS();

    // find loops, set loop bounds and remove back edges
    findLoops();

    /////////////////////////////////////////////////////////////////////////////
    adjustLoopBounds();

    findInitialLoadingPoints();

    //////////
    
    // find function sizes
    int maxFuncSize = findMaxFuncSize();
    int totalCodeSize = findTotalCodeSize();
 
    if (argc == 3)
    {
        if (strcmp(argv[2], "size") == 0)
        {
            printf("# func: %d, Total Code Size: %d, Max Func Size: %d\n", nFunc, totalCodeSize, maxFuncSize);
            printf("60%%: %d (%d is 80%% of it), 75%%: %d (%d is 80%% of it)\n", ((int)((float)totalCodeSize*0.6+15)/16)*16, ((int)((float)totalCodeSize*0.6*0.8+15)/16)*16, ((int)((float)totalCodeSize*0.75+15)/16)*16, ((int)((float)totalCodeSize*0.75*0.8+15)/16)*16);
            exit(1);
        }
        else if (strcmp(argv[2], "GCCFG") == 0)
        {
            outputGCCFG();
            exit(1);
        }
        else
            exit(1);
    }
    else if (argc >= 4)
    {
        SPMSIZE = atoi(argv[2]);
        printf("SPMSIZE: %d\n",SPMSIZE);

        if (strcmp(argv[3], "or") == 0)
            runmode = OPT_R;
        else if (strcmp(argv[3], "orf") == 0)
            runmode = OPT_RF;
        else if (strcmp(argv[3], "fs") == 0)
            runmode = FS;
        else if (strcmp(argv[3], "h") == 0)
            runmode = HEU;
        else if (strcmp(argv[3], "c") == 0)
            runmode = CACHE;
        else if (strcmp(argv[3], "f") == 0)
            runmode = FIXED;
        else
        {
            printf("not a valid option.\n");
            exit(1);
        }
        
        if (SPMSIZE < maxFuncSize && (runmode == OPT_R || runmode == OPT_RF || runmode == HEU) && argc == 4)
        {
#ifdef DEBUG
            printf("The SPM size (%d bytes) is smaller than the largeest function (%d bytes).\n", SPMSIZE, maxFuncSize);
#endif
            exit(1);
        }

    }

    switch (runmode)
    {
    case CACHE:
        cache_analysis(CACHE_MISS_LATENCY);
        break;
    case OPT_R:
        cm_region_optimal(NULL);
        break;
    case OPT_RF:
        cm_rf_optimal(NULL);
        break;
    case FS:
        cm_fs();
        break;
    case HEU:
        if (runHeuristic(SPMSIZE) == -1)
            break;
    case FIXED:
        wcet_analysis_fixed_input();
        break;
    }

    return 0;
}

