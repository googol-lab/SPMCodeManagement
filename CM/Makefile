GUROBIPATH=/Library/gurobi651/mac64
GUROBIVER=gurobi65
CC=clang
PYTHONPATH=/usr/include/python2.7

cm: gencfg/gencfg y.tab.c main.c DFS.c CFG_traversal.c loop.c cache_analysis.c CM_region_based.c CM_region_free.c GCCFG.c CM_heuristic.c util.c split.c split_wrapper.c
	$(CC) -O3 -m64 -L$(GUROBIPATH)/lib -l$(GUROBIVER) -I/Library/gurobi600/mac64/include -lpthread -lm lex.yy.c y.tab.c main.c DFS.c CFG_traversal.c loop.c DMA.c cache_analysis.c CM_region_based.c CM_region_free.c GCCFG.c CM_heuristic.c util.c split.c split_wrapper.c CM_bblevel.c -o bin/cm
y.tab.c: CFG.y CFG.h lex.yy.c ; yacc -d CFG.y
lex.yy.c: CFG.l ; lex CFG.l

gencfg/gencfg: gencfg/py_wrap.c gencfg/readcode.c gencfg/cfg.c gencfg/main.c
	$(CC) -I$(PYTHONPATH) gencfg/py_wrap.c gencfg/readcode.c gencfg/cfg.c gencfg/main.c -lpython2.7 -lm -Wall -Wextra -o gencfg/gencfg

clean: ; rm bin/cm gencfg/gencfg
