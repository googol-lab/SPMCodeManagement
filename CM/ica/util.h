#ifndef _UTIL_H_
#define _UTIL_H_

#define CHECK_MEM(p) \
	if ((p) == NULL) { \
		fprintf(stderr, "out of memory (file %s, line %d)\n", __FILE__, __LINE__); \
		exit(1); \
	}

typedef struct stack_frame stack_frame;

struct stack_frame{
	gcfg_node_t *lr;
	stack_frame *next;
};

stack_frame *sp = NULL;

void push(stack_frame *frame)
{
	frame->next = sp;
	sp = frame;

}

stack_frame* pop(void)
{
	if (!sp)
		return NULL;
	stack_frame *frame = sp;
	sp = sp->next;
	return frame;
}


#endif
