#include "CM.h"
#include "DFS.h"
#include "CFG_traversal.h"
#include "loop.h"
#include "util.h"

#include "gurobi_c.h"
#include "cache_analysis.h"
#include "DMA.h"

void cache_analysis(int LATENCY)
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
        BBType** tpoSortedNodes = (BBType**)malloc(sizeof(BBType)*nNode);
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

