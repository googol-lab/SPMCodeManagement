%{
    #include "CFG.h"
    #include "util.h"
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <limits.h>
    
    void yyerror(const char *str) { fprintf(stderr, "error: %s at\n", str); exit(1); }
    int yyparse(void);
    int yylex(void);
    int yywrap() { return 1; }
    int yydebug;
    
    BBType* rootNode = NULL;
    //int maxEC = 0;
    int nNode = 0;
    int nEdge = 0;
    int nCSNode = 0;
    BBListEntry* BBPool = NULL;
    int nFunc = 0;

    funcType* functions = NULL;

    BBType** nodes = NULL;

    BBListEntry* VcNodes = NULL;

    void newFunc(int size);
    void newBB(char *name, unsigned int addr, int EC, int FS, int AH, int AM, int FM);
    void newEdge(char *from, char *to);
    void finalize();
%}

%union {
    int iVal;
    char* sVal;
}

%token <iVal> Number
%token <sVal> Name
%token Address Funcs_begin Digraph Lbrace Rbrace Semicolon Ec Sz Equal Lbracket Rbracket Arrow Nodes_begin Edges_begin Comma Ah Am Fm Colon

%left Arrow

%%

Cfg:
Digraph Name Lbrace Func_list Node_list Edge_list Rbrace
;

Func_list:
Funcs_begin Funcs
;

Funcs:
Funcs Func
|   /* NULL */
;

Func:
Number Semicolon
{ newFunc($1); /*printf("%d\n", $1);*/ }
;

Node_list:
Nodes_begin Nodes
;

Nodes:
Nodes Node
|   /* NULL */
;

Node:
Number Lbracket Address Equal Number Comma Ec Equal Number Comma Sz Equal Number Comma Ah Equal Number Comma Am Equal Number Comma Fm Equal Number Rbracket Semicolon
{ char name[10]; sprintf(name, "%d", $1); newBB(name, $5, $9, $13, $17, $21, $25); /*printf("NODE:%s\n", name);*/ }
;

Edge_list:
Edges_begin Edges
{ finalize(); }
;

Edges:
Edges Edge
|   /* NULL */
;

Edge:
Number Arrow Number Semicolon
{ char name1[10], name2[10]; sprintf(name1, "%d", $1); sprintf(name2, "%d", $3); newEdge(name1, name2); /*printf("EDGE:%s -> %s\n", name1, name2);*/ }
;
%%

void newFunc(int size)
{
    funcType* new_functions = (funcType*)realloc(functions, sizeof(funcType)*(nFunc+1));
    if (new_functions == NULL) {
        printf("mem alloc error @ newFunc\n");
        exit(1);
    }

    functions = new_functions;
    functions[nFunc].size = size;

    functions[nFunc].nAddrRange = 1;
    functions[nFunc].addrRange = (addrRangeType*)malloc(sizeof(addrRangeType));
    if (functions[nFunc].addrRange == NULL) {
        printf("mem alloc error @ newFunc\n");
        exit(1);
    }
    functions[nFunc].addrRange[0].startAddr = INT_MAX;
    functions[nFunc].addrRange[0].size = 0;


    functions[nFunc].parent = -1;
    functions[nFunc].nChildren = 0;
    functions[nFunc].childrenIDs = NULL;
    functions[nFunc].nOccurrence = 0;
    functions[nFunc].entryPoints = NULL;
    functions[nFunc].exitPoints = NULL;
    nFunc++;
}

BBType* findBBFromList(char* name, BBListEntry* list)
{
    BBListEntry* entry = list;
    
    while (entry)
    {
        if (strcmp(name, entry->BB->name) == 0)
            break;
        entry = entry->next;
    }
    
    if (entry)
        return entry->BB;
    else
    {
        printf("@findBBFromList: cannot find BB with name '%s' in list %lld\n", name, (unsigned long long int)list);
        return NULL;
    }
}

void newBB(char* name, unsigned int addr, int EC, int FS, int AH, int AM, int FM)
{
    BBType* bb = (BBType*)malloc(sizeof(BBType));
    bb->name = strdup(name);
    bb->ID = nNode++;
    
    bb->addr = addr;

    bb->bLiteralPool = 1;

    // keep the record of the smallest addr
    if (addr <= functions[EC].addrRange[0].startAddr) {
        functions[EC].addrRange[0].startAddr = addr;

        int nOcc = functions[EC].nOccurrence;
        BBType** new_entryPoints = (BBType**)realloc(functions[EC].entryPoints, sizeof(BBType*)*(nOcc+1));
        BBType** new_exitPoints = (BBType**)realloc(functions[EC].exitPoints, sizeof(BBType*)*(nOcc+1));
        
        if (new_entryPoints == NULL || new_exitPoints == NULL) {
            printf("mem alloc error @ newBB\n");
            exit(1);
        }

        functions[EC].entryPoints = new_entryPoints;
        functions[EC].entryPoints[nOcc] = bb;

        functions[EC].exitPoints = new_exitPoints;

        functions[EC].nOccurrence++;
    }

    if (addr+FS*4 > functions[EC].addrRange[0].startAddr + functions[EC].addrRange[0].size)
        functions[EC].addrRange[0].size += FS*4;

    if (addr+FS*4 == functions[EC].addrRange[0].startAddr + functions[EC].addrRange[0].size)
        functions[EC].exitPoints[functions[EC].nOccurrence-1] = bb;

    bb->EC = EC;
    //if (EC > maxEC) 
    //    maxEC = EC;
    bb->S = FS; /* word size */
    
    bb->CACHE_AH = AH;
    bb->CACHE_AM = AM;
    bb->CACHE_FM = FM;
    
    bb->CS = 0;
    bb->callee = NULL;
    bb->RT = 0;

    bb->bLoopHead = 0;
    bb->bLoopTail = 0;
    bb->bPreLoop = 0;

    //bb->loopTail = NULL;
    bb->loopTailList = NULL;
    bb->loopHead = NULL;
    bb->preLoopList = NULL;

    //bb->L = -1;
    bb->Vc = 0;
    //bb->nT = 0;
#ifdef BBLEVEL_CM
    bb->bLoaded = 0;
#endif

    //bb->bTerminal = 0;
    //bb->succInLoop = NULL;

    bb->N = 0;
    bb->bUnreachable = 1;
    
    bb->ISsize = 0;
    bb->IS = NULL;
    bb->bFirst = 0;

    bb->bHeuristicTried = 0;

    bb->bVisited = 0;
    bb->bVisitedForFindingLoop = 0;
    
    bb->predList = NULL;
    bb->succList = NULL;
    
    if (rootNode == NULL)
    rootNode = bb;
    
    addBBToList(bb, &BBPool);

    BBType** newNodes = (BBType**)realloc(nodes, sizeof(BBType*) * nNode);
    if (newNodes == NULL) {
        printf("mem alloc error @ newBB\n");
        exit(1);
    }

    nodes = newNodes;
    nodes[nNode-1] = bb;
}

// deallocate BB pool list
void finalize()
{    
    BBListEntry* entry = BBPool;
    while (entry)
    {
        free(entry->BB->name);
        entry->BB->name = NULL;

        BBListEntry* temp = entry->next;
        free(entry);
        entry = temp;
    }
}

void newEdge(char *from, char *to)
{
    BBType* fromBB = findBBFromList(from, BBPool);
    BBType* toBB = findBBFromList(to, BBPool);
    
    nEdge++;
    
    addBBToList(toBB, &fromBB->succList);
    addBBToList(fromBB, &toBB->predList);
    
    if (fromBB->EC != toBB->EC)
    {
        fromBB->Vc = 1;
        if (toBB->addr != functions[toBB->EC].addrRange[0].startAddr) {
            fromBB->RT = 1;
            //toBB->RT = 1;
        }
        fromBB->callee = toBB;
        addBBToList(fromBB, &VcNodes);
        toBB->CS = 1;
        //toBB->caller = fromBB->EC;
        nCSNode++;
    }
    fromBB->bLiteralPool = 0;
    toBB->bLiteralPool = 0;
}
