#ifndef REGION_BASED_H
#define REGION_BASED_H

enum solverOption {SILENT, VERBOSE};

long long int cm_region_optimal(long long int* fCost);
long long int wcet_analysis_fixed_input(enum solverOption svo);

#endif
