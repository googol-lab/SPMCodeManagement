import os
import subprocess
import string

builddir = "_build/"
toolchain = "arm-none-eabi-"

def get_data_addrs():
    head, tail = os.path.split(os.getcwd())
    disasm = builddir + tail + ".asm"
    data_addr = []
#    minAddr = -1
    with open(disasm, "rb") as f:
        for line in f:
            item_list = line.split("\t")
            if len(item_list) >= 4:
#                if minAddr == -1:
#                    minAddr = int(item_list[0].split(":")[0], 16)
                if (item_list[2] == ".word"):
                    data_addr.append(int(item_list[0].split(":")[0], 16)-65536)
    data_addr = list(set(data_addr))
    data_addr.sort()
#	for i in range(len(data_addr)):
#		print data_addr[i]
    return data_addr

def is_hex(s):
    try:
        int(s, 16)
        return True
    except ValueError:
        return False

def get_min_addr():
    head, tail = os.path.split(os.getcwd())
    elf_name = builddir + tail + ".elf"
    command = toolchain + "nm " + elf_name + " | grep -i \" t \""
    output = subprocess.check_output(command, shell=True)
    names = output.split()
    func_addr = []
    first_func_name = ''
    minAddr = int(names[0], 16)
    for i in range(len(names)/3):
        addr = int(names[3*i], 16)
        if addr <= minAddr:
            first_func_name = names[3*i+2]
            minAddr = addr
    return minAddr
 
def get_symb_addrs():
    head, tail = os.path.split(os.getcwd())
    elf_name = builddir + tail + ".elf"
    command = toolchain + "nm " + elf_name + " | grep -i \" t \""
    output = subprocess.check_output(command, shell=True)
    names = output.split()
    func_addr = []
    first_func_name = ''
    minAddr = int(names[0], 16)
    for i in range(len(names)/3):
        addr = int(names[3*i], 16)
        if addr <= minAddr:
            first_func_name = names[3*i+2]
            minAddr = addr
 
#    if minAddr >= 8 :
#        minAddr -= 4

    for i in range(len(names)/3):
#        addr = int(names[3*i], 16) - minAddr
        addr = int(names[3*i], 16) - 65536
        func_addr.append(addr)  
        if names[3*i+2] == "main":
            ent_addr = addr
    func_addr = list(set(func_addr))	
    func_addr.sort()
    func_addr.insert(0, ent_addr)
#	for i in range(len(func_addr)):
#		print func_addr[i]

    elf_name = builddir + tail + ".4gem5"
    command = toolchain + "nm " + elf_name + " | grep -i \" t \""
    output = subprocess.check_output(command, shell=True)
    names = output.split()
    nNoName = 0
    i = 0
    while True:
        if (3*i >= (len(names) + nNoName)):
            break
        if (is_hex(names[3*i+2-nNoName])):
            nNoName += 1

        if names[3*i+2-nNoName] == first_func_name:
            func_addr.insert(0, int(names[3*i-nNoName], 16))

        i += 1

    return func_addr


#if __name__ == "__main__":
#	get_ent_addr()
#	get_data_addr()
#	get_func_addr()



    






