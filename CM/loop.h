#ifndef LOOP_H
#define LOOP_H

typedef struct _loop
{
    long long int start;
    long long int end;
    int lb;
    int EC;
} loop;

//extern loop* loopList;
//extern int nLoop;

int getLB(int loopIdx);
int isLoopAinLoopB(int a, int b);
void adjustLoopBounds();
void assignIterCounts();
void findLoops();
void moveLoopEdges();

#endif
