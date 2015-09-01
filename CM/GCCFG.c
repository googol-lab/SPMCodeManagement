#include "CM.h"
#include "DFS.h"
#include "CFG_traversal.h"
#include "util.h"
#include "GCCFG.h"

typedef struct _loopInfo {
    int ID;
    long long int startAddr;
    long long int endAddr;
    struct _loopInfo *next;
} loopInfo;

loopInfo* loopList;

void outputGCCFG()
{
    GCCFGNode* GCCFGnodes = NULL;
    GCCFGNode* curNode = NULL;
    GCCFGEdge* GCCFGedges = NULL;
    GCCFGEdge* curEdge = NULL;

    initVisited();
 
    //BBType* node;
    
    INITSTACK(nNode)

    int* funcAppeared = (int*)malloc(sizeof(int)*nFunc);
    int i, j;
    for (i = 0; i < nFunc; i++)
        funcAppeared[i] = 0;
    
    GCCFGnodes = curNode = (GCCFGNode*)malloc(sizeof(GCCFGNode));
    sprintf(curNode->name, "F%d", rootNode->EC);
    curNode->type = 1;
    curNode->size = functions[rootNode->EC].size;

    loopInfo* loopList = NULL;

    for (i = 0; i < nNode; i++) {
        BBType* node = nodes[i];

        if (node->bLoopHead) {
            // find the loop tail
            BBListEntry* loopTailEntry = node->loopTailList;
            BBType* loopTail = loopTailEntry->BB;
            while (loopTailEntry) {
                if (loopTailEntry->BB->addr > loopTail->addr) 
                    loopTail = loopTailEntry->BB;
                loopTailEntry = loopTailEntry->next;
            }

            // check if any basic block in this loop has a function call
            int bContainCall = 0;
            for (j = node->ID; j <= loopTail->ID; j++) {
                if (nodes[j]->CS) {
                    bContainCall = 1;
                    break;
                }
            }

            // if so, make a loop node for this loop and connect it to the parent
            if (bContainCall) {
                // create a node
                GCCFGNode* newNode = (GCCFGNode*)malloc(sizeof(GCCFGNode));
                sprintf(newNode->name, "L%d", node->ID);
                newNode->type = 0;
                newNode->size = 0;
                newNode->next = NULL;
                curNode->next = newNode;
                curNode = newNode;

                // find if there is a parent loop 
                int parentLoop = -1;
                loopInfo* loopEntry = loopList;
                while (loopEntry) {
                    if (loopEntry->startAddr < node->addr && loopEntry->endAddr > loopTail->addr) {
                        parentLoop = loopEntry->ID;
                        break;
                    }
                    loopEntry = loopEntry->next;
                } 
                
                // edge
                GCCFGEdge* newEdge = (GCCFGEdge*)malloc(sizeof(GCCFGEdge));
                if (parentLoop == -1) {
                    sprintf(newEdge->caller, "F%d", node->EC);
                }
                else {
                    sprintf(newEdge->caller, "L%d", parentLoop);
                }
                sprintf(newEdge->callee, "L%d", node->ID);
                newEdge->niter = node->N;
                newEdge->next = NULL;

                if (GCCFGedges == NULL)
                    GCCFGedges = curEdge = newEdge;
                else {
                    GCCFGEdge* edge = GCCFGedges;
                    int bDuplicate = 0;
                    while (edge) {
                        if (strcmp(edge->caller, newEdge->caller) == 0 &&
                            strcmp(edge->callee, newEdge->callee) == 0) {
                            bDuplicate = 1;
                            break;
                        }
                        edge = edge->next;
                    }
                    if (bDuplicate == 0) {
                        curEdge->next = newEdge;
                        curEdge = newEdge;
                    }
                    else
                        free(newEdge);
                }

                // create a loopinfo node
                loopInfo* newLoop = (loopInfo*)malloc(sizeof(loopInfo));
                newLoop->ID = node->ID;
                newLoop->startAddr = node->addr;
                newLoop->endAddr = loopTail->addr;
                newLoop->next = NULL;
                if (loopList) {
                    loopInfo* loopEntry = loopList;
                    while (loopEntry->next)
                        loopEntry = loopEntry->next;
                    loopEntry->next = newLoop;
                }
                else
                    loopList = newLoop;
            }
        }
        if (node->CS && isNodeRT(node) == 0) {
            // create a node if appeared for the first time
            if (funcAppeared[node->EC] == 0) {
                GCCFGNode* newNode = (GCCFGNode*)malloc(sizeof(GCCFGNode));
                sprintf(newNode->name, "F%d", node->EC);
                newNode->type = 1;
                newNode->size = functions[node->EC].size;
                newNode->next = NULL;
                curNode->next = newNode;
                curNode = newNode;
                funcAppeared[node->EC] = 1;
            }

            // find if there is a parent loop 
            int parentLoop = -1;
            loopInfo* loopEntry = loopList;
            BBType* callerNode = node->predList->BB;
            int smallestSize = INT_MAX;
            while (loopEntry) {
                if (loopEntry->startAddr < callerNode->addr && loopEntry->endAddr > callerNode->addr) {
                    int loopSize = loopEntry->endAddr - loopEntry->startAddr;
                    if (loopSize < smallestSize)
                        parentLoop = loopEntry->ID;
                }
                loopEntry = loopEntry->next;
            } 
                
            // edge
            GCCFGEdge* newEdge = (GCCFGEdge*)malloc(sizeof(GCCFGEdge));
            if (parentLoop == -1) {
                sprintf(newEdge->caller, "F%d", callerNode->EC);
            }
            else {
                sprintf(newEdge->caller, "L%d", parentLoop);
            }
            sprintf(newEdge->callee, "F%d", node->EC);
            newEdge->niter = getNCall(node);
            newEdge->next = NULL;

            if (GCCFGedges == NULL)
                GCCFGedges = curEdge = newEdge;
            else {
                GCCFGEdge* edge = GCCFGedges;
                int bDuplicate = 0;
                while (edge) {
                    if (strcmp(edge->caller, newEdge->caller) == 0 &&
                        strcmp(edge->callee, newEdge->callee) == 0) {
                        bDuplicate = 1;
                        break;
                    }
                    edge = edge->next;
                }
                if (bDuplicate == 0) {
                    curEdge->next = newEdge;
                    curEdge = newEdge;
                }
                else
                    free(newEdge);
            }
        }
    }

    FILE *fp;
    fp = fopen("output.txt", "w");

    fprintf(fp, "number of functions\n%d\n", nFunc);

    fprintf(fp, "node\tsize\n");
    curNode = GCCFGnodes;
    while (curNode)
    {
        fprintf(fp, "%s\t%d\t%d\n", curNode->name, curNode->size, curNode->type);
        curNode = curNode->next;
    }

    fprintf(fp, "< caller\tcallee >\n");
    curEdge = GCCFGedges;
    while (curEdge)
    {
        fprintf(fp, "%s\t%s\t%d\n", curEdge->caller, curEdge->callee, curEdge->niter);
        curEdge = curEdge->next;
    }

    fclose(fp);
    
    curNode = GCCFGnodes;
    while (curNode)
    {
        GCCFGNode* next = curNode->next;
        free(curNode);
        curNode = next;
    }
    curEdge = GCCFGedges;
    while (curEdge)
    {
        GCCFGEdge* next = curEdge->next;
        free(curEdge);
        curEdge = next;
    }
    FREESTACK
}

