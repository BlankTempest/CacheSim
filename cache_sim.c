#include<stdlib.h>
#include<stdio.h>
#include<omp.h>
#include<string.h>
#include<ctype.h>

#define MEMORY_SIZE 24
#define CACHE_SIZE 2

typedef char byte;

#define INVALID 0
#define MODIFIED 1
#define EXCLUSIVE 2
#define SHARED 3

struct cache {
    byte address; // This is the address in memory.
    byte value; // This is the value stored in cached memory.
    // State for you to implement MESI protocol.
    byte state;
};

struct decoded_inst {
    int type; // 0 is RD, 1 is WR
    byte address;
    byte value; // Only used for WR 
};

typedef struct cache cache_line;
typedef struct decoded_inst decoded;

/*
* This is a very basic C cache simulator.
* The input files for each "Core" must be named core_1.txt, core_2.txt, core_3.txt ... core_n.txt
* Input files consist of the following instructions:
* - RD <address>
* - WR <address> <val>
*/

byte memory[MEMORY_SIZE];
cache_line cache[CACHE_SIZE];

void initialize_memory() {
    for (int i = 0; i < MEMORY_SIZE; i++) {
        memory[i] = 0;
    }
}

void initialize_cache() {
    for (int i = 0; i < CACHE_SIZE; i++) {
        cache[i].address = -1;
        cache[i].value = 0;
        cache[i].state = INVALID;
    }
}

// Decode instruction lines
decoded decoded_inst_line(char * buffer){
    decoded inst;
    char inst_type[2];
    sscanf(buffer, "%s", inst_type);
    if(!strcmp(inst_type, "RD")){
        inst.type = 0;
        int addr = 0;
        sscanf(buffer, "%s %d", inst_type, &addr);
        inst.value = -1;
        inst.address = addr;
    } else if(!strcmp(inst_type, "WR")){
        inst.type = 1;
        int addr = 0;
        int val = 0;
        sscanf(buffer, "%s %d %d", inst_type, &addr, &val);
        inst.address = addr;
        inst.value = val;
    }
    return inst;
}

void decode_inst_line(char *buffer, char *inst_type, int *addr, int *val) {
    if (sscanf(buffer, "%s %d %d", inst_type, addr, val) != 3) {
        sscanf(buffer, "%s %d", inst_type, addr);
        *val = 0;
    }
}

int coherence_check(int addr) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].address == addr) {
            return i;
        }
    }
    return -1;
}

void update_cache(int idx, int addr, int val, int inst_type) {
    #pragma omp critical
    {
        for (int i = 0; i < CACHE_SIZE; i++) {
            if (cache[i].address == addr && i != idx) {
                cache[i].state = INVALID;
            }
        }

        cache[idx].address = addr;
        cache[idx].value = val;

        if (inst_type == 0) { // read
            cache[idx].state = SHARED;
        } else { // write
            if (cache[idx].state == INVALID) {
                cache[idx].state = MODIFIED;
            } else {
                cache[idx].state = MODIFIED;
            }
        }
    }
}

void write_back_to_memory(int idx) {
    #pragma omp critical
    {
        if (cache[idx].state == MODIFIED) {
            memory[cache[idx].address] = cache[idx].value;
            cache[idx].state = SHARED;
        }
    }
}

// Helper function to print the cachelines
void print_cachelines(cache_line * c, int cache_size){
    for(int i = 0; i < cache_size; i++){
        cache_line cacheline = *(c+i);
        printf("Address: %d, State: %d, Value: %d\n", cacheline.address, cacheline.state, cacheline.value);
    }
}

// This function implements the mock CPU loop that reads and writes data.
void cpu_loop(int num_threads){
    // Initialize a CPU level cache that holds about 2 bytes of data.
    int cache_size = 2;
    cache_line * c = (cache_line *) malloc(sizeof(cache) * cache_size);
    
    // Read Input file
    FILE * inst_file = fopen("input_0.txt", "r");
    char inst_line[20];
    // Decode instructions and execute them.
    while (fgets(inst_line, sizeof(inst_line), inst_file)){
        decoded inst = decoded_inst_line(inst_line);
        /*
         * Cache Replacement Algorithm
         */
        int hash = inst.address%cache_size;
        cache_line cacheline = *(c+hash);
        /*
         * This is where you will implement the coherancy check.
         * For now, we will simply grab the latest data from memory.
         */
        if(cacheline.address != inst.address){
            // Flush current cacheline to memory
            *(memory + cacheline.address) = cacheline.value;
            // Assign new cacheline
            cacheline.address = inst.address;
            cacheline.state = -1;
            // This is where it reads value of the address from memory
            cacheline.value = *(memory + inst.address);
            if(inst.type == 1){
                cacheline.value = inst.value;
            }
            *(c+hash) = cacheline;
        }
        switch(inst.type){
            case 0:
                printf("Reading from address %d: %d\n", cacheline.address, cacheline.value);
                break;
            
            case 1:
                printf("Writing to address %d: %d\n", cacheline.address, cacheline.value);
                break;
        }
    }
    free(c);
}

void cpu_thread(int thread_num) {
    char filename[15];
    sprintf(filename, "input_%d.txt", thread_num);
    FILE *inst_file = fopen(filename, "r");
    char inst_line[20];
    while (fgets(inst_line, sizeof(inst_line), inst_file)) {
        char inst_type[3];
        int addr, val;
        decode_inst_line(inst_line, inst_type, &addr, &val);
        int cache_idx = coherence_check(addr);
        switch (inst_type[0]) {
            case 'R':
                if (cache_idx == -1) {
                    printf("Thread %d: RD %d: %d\n", thread_num, addr, memory[addr]);
                    update_cache(thread_num % CACHE_SIZE, addr, memory[addr], 0);
                } else {
                    write_back_to_memory(cache_idx);
                    printf("Thread %d: RD %d: %d\n", thread_num, addr, cache[cache_idx].value);
                }
                break;
            case 'W':
                if (cache_idx != -1) {
                    write_back_to_memory(cache_idx);
                }
                memory[addr] = val;
                printf("Thread %d: WR %d: %d\n", thread_num, addr, val);
                update_cache(thread_num % CACHE_SIZE, addr, val, 1);
                break;
        }
    }
    fclose(inst_file);
}


int main(int argc, char *argv[]) {
    initialize_memory();
    initialize_cache();

    int num_threads = argc > 1 ? atoi(argv[1]) : 1;
    omp_set_num_threads(num_threads);

    #pragma omp parallel
    {
        int thread_num = omp_get_thread_num();
        cpu_thread(thread_num);
    }

    return 0;
}
