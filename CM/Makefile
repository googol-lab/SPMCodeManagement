PYTHONPATH=/usr/include/python2.7

cm: ica/icache y.tab.c main.c DFS.c CFG_traversal.c loop.c cache_analysis.c CM_region_based.c CM_region_free.c GCCFG.c CM_heuristic.c util.c
	clang -m64 -L/Library/gurobi600/mac64/lib -lgurobi60 -I/Library/gurobi600/mac64/include -lpthread -lm lex.yy.c y.tab.c main.c DFS.c CFG_traversal.c loop.c DMA.c cache_analysis.c CM_region_based.c CM_region_free.c GCCFG.c CM_heuristic.c util.c -o bin/cm
y.tab.c: CFG.y CFG.h lex.yy.c ; yacc -d CFG.y
lex.yy.c: CFG.l ; lex CFG.l

ica/icache: ica/py_wrap.c ica/readcode.c ica/cfg.c ica/main.c ica/icache.c
	gcc -I$(PYTHONPATH) ica/py_wrap.c ica/readcode.c ica/cfg.c ica/main.c ica/icache.c -lpython2.7 -lm -Wall -Wextra -o ica/icache

clean: ; rm bin/cm ica/icache
