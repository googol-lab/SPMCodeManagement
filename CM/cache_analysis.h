#ifndef CACHE_ANALYSIS_H
#define CACHE_ANALYSIS_H

int init_cache_analysis(int size, int line_size, int associativity);
int cache_analysis();

void cache_wcet_analysis(int LATENCY);

#endif
