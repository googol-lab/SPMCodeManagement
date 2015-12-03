#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "datatype.h"

//#define DEBUG

extern prog_t prog;

addr_t* get_data_addrs(void);

//int minAddr;

static inline int is_cond(unsigned int inst) {
	//check if bit[31:29] is not 0b111
	return inst >> 29 != 0x7;
}

static inline int is_branch(unsigned int inst) {
	//check if bit[27:24] is 0b1010
	return inst << 4 >> 28 == 0xa;
}

static inline int is_call(unsigned int inst) {
	// check if bit [27:24] is 0b1011
	return inst << 4 >> 28 == 0xb;
}

static inline int is_ret(unsigned int inst) {
	// mov PC, LR
	// bit[27:21] is 0b0001101, bit[11:4] is 0b0000000, bit[15:12](Rd) is PC(R15), bit[3:0](Rm) is LR(R14)
	int case0 = inst << 4 >> 25 == 0xd && inst << 20 >> 24 == 0x0 && inst << 16 >> 28 == 0xf && (inst & 0xf) == 0xe;
	// pop {register_list, pc}
	// bit[27:16] is 0b100010111101, bit[15] = 0b1
	int case1 = inst << 4 >> 20 == 0x8bd && (inst & 0x8000) == 0x8000;
	// bx LR
	// bit[27:6] is 0b0001001011111111111100, bit[3:0](Rm) is LR(R14)
	int case2 = inst << 4 >> 10 == 0x4bffc && (inst & 0xf) == 0xe;
	// ldr PC,[<Rn>{,#+/-<imm12>}] or ldr PC,[<Rn>],+/-<Rm>{, <shift>}
	// bit[27:25] is 0b010 or 0b011, bit[15:12] is PC(R15)
	int case3 = (inst << 4 >> 29 == 0x2 || inst << 4 >> 29 == 0x3) && inst << 16 >> 28 == 0xf;
	return case0 || case1 || case2 || case3;
}

static inline int is_nop(unsigned int inst) {
    // mov r0, r0
    return (inst == 0xe1a00000);
}

static inline addr_t get_target(uint32_t inst, addr_t PC) {
	// target = PC + 8 + signed relative offset
	return (long long int)PC + 8 + (long long int)((int)inst << 8 >> 6);
}

int print_type(inst_type_t type)
{
	printf("type: ");
	switch (type) {
	case 1:
		printf("conditional branch\n");
		break;
	case 2:
		printf("unconditional branch\n");
		break;
	case 3:
		printf("conditional function call\n");
		break;
	case 4:
		printf("unconditional function call\n");
		break;
	case 5:
		printf("conditional return\n");
		break;
	case 6:
		printf("unconditional return\n");
		break;
    case 7:
        printf("nop\n");
        break;
	default:
		printf("unrecognized type\n");
		break;
	}
	return 0;
}

int decode_inst(inst_t *de_inst, uint32_t inst){
	if (is_branch(inst)) {
		if (is_cond(inst))
			de_inst->type = INST_COND_BRANCH;
		else
			de_inst->type = INST_UNCOND_BRANCH;

        	addr_t target = get_target(inst, de_inst->addr + 4);
		de_inst->target = target - 4;
	} else if (is_call(inst)) {
		if (is_cond(inst))
			de_inst->type = INST_COND_CALL;
		else
			de_inst->type = INST_UNCOND_CALL;

                addr_t target = (int)get_target(inst, de_inst->addr + 4);
                //printf("%lld %lld\n", de_inst->addr, target);
                if (target < 0)
                {
                    if (is_cond(inst))
                        de_inst->type = INST_COND_BRANCH;
                    else
                        de_inst->type = INST_UNCOND_BRANCH;

                    de_inst->target = de_inst->addr+4;
                }
                else
		    de_inst->target = target - 4;
	} else if (is_ret(inst)) {
		if (is_cond(inst))
			de_inst->type = INST_COND_RET;
		else
			de_inst->type = INST_UNCOND_RET;
	} else if (is_nop(inst)) {
		de_inst->type = INST_NOP;
    } else
		de_inst->type = INST_SEQ;
#ifdef DEBUG
	if (de_inst->type != INST_SEQ) {
		printf("index: 0x%x\n", de_inst->addr);
		printf("instruction: 0x%x\n", inst);
		print_type(de_inst->type);
		printf("target: 0x%x\n", de_inst->target);
		printf("\n");
	}
#endif
	return 0;
}

// read the binary of the program code to be analysed
int read_code(void)
{
	long long int i;
	long long int cur, num_data;
	long long int num_word;
	FILE *fp;
	uint32_t inst;
	addr_t *data_addrs;

    //minAddr = get_min_addr();
	char filename[64] = {0, };
	strcpy(filename, "_build/");
	strcat(filename, prog.fname);
	strcat(filename, ".bin");
	fp = fopen(filename, "rb");
    if (fp == NULL)
    {
        printf("file doesn't exist\n");
        exit(1);
    }

	// get the number of words which include both instructions and data
	num_word = 0;
	while(fread((void*)&inst, 4, 1, fp))
		num_word++;
	fseek(fp, 0, SEEK_SET);
	// get the addresses of data if there is any
	data_addrs = get_data_addrs();
	// the number of the words of data
	num_data = data_addrs[0];
    int nPadding = 0;
    if (num_data)
    {
        if (data_addrs[1] == 0)
        {
            int lastAddr = 0;
            for (i = 2; i < num_data; i++)
            {
                if (data_addrs[i] == lastAddr+4)
                    lastAddr += 4;
                else
                    break;
            }
            nPadding = (lastAddr+4)/4;
        }
    }

	/*
	printf("data_addr:\n", prog.ent_addr);
	for (i = 0; i < data_addr[0]; i++)
		printf("%u\n", data_addr[1+i]);
	*/
	// find out the branch instructions
	// TO BE FIX: this version only processes branch instructions
	prog.code = (inst_t *) calloc(num_word, sizeof(inst_t));
	prog.num_inst = 0;
	prog.code_size = 0;
	//for (i = 0, j = 0, cur = 0; fread((void*)&inst, 2, 1, fp) && fread((void *)((char *)&inst + 2), 2, 1, fp); i++) {
    for (i = 0; i < nPadding; i++)
        fread((void*)&inst, 4, 1, fp);

	// both the binary and the local machine are little-endian
	for (i = 0, cur = 0; fread((void*)&inst, 4, 1, fp); i++) {
		//addr_t addr = (i * 4) + (minAddr ? minAddr-4:0);
		addr_t addr = ((i+nPadding) * 4);

		prog.code[i].addr = addr;
        prog.code[i].binary = inst;

		// skip current word if it is data
		if (cur < num_data) {
			if (addr == data_addrs[1+nPadding+cur]) {
				cur++;
                prog.code[i].type = INST_DATA;
				//continue;
			}
            else
		        decode_inst(&prog.code[i], inst);
		}
        else
		    decode_inst(&prog.code[i], inst);
		prog.num_inst++;
		prog.code_size += 4;
	}
	//prog.code = (inst_t *)realloc(prog.code, prog.num_inst * sizeof(inst_t));
	fclose(fp);
	free(data_addrs);
	return 0;
}

