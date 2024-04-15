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

typedef struct {
    byte address; // This is the address in memory.
    byte value; // This is the value stored in cached memory.
    // State for you to implement MESI protocol.
    byte state;
} cache_line;

typedef struct {
    int type; // 0 is RD, 1 is WR
    byte address;
    byte value; // Only used for WR 
} decoded_inst;

/*
* This is a very basic C cache simulator.
* The input files for each "Core" must be named core_1.txt, core_2.txt, core_3.txt ... core_n.txt
* Input files consist of the following instructions:
* - RD <address>
* - WR <address> <val>
*/

byte memory[MEMORY_SIZE];
cache_line **caches; //pointer

omp_lock_t *bus_locks;

// Decode instruction lines
decoded_inst decode_inst_line(char *buffer) {
    decoded_inst inst;
    char inst_type[3];
    int addr, val = 0;
    sscanf(buffer, "%s %d %d", inst_type, &addr, &val);
    
    if (strcmp(inst_type, "RD") == 0) {
        inst.type = 0;
        inst.address = (byte)addr;
        inst.value = -1;
    } else if (strcmp(inst_type, "WR") == 0) {
        inst.type = 1;
        inst.address = (byte)addr;
        inst.value = (byte)val;
    }
    return inst;
}

int cache_lookup(cache_line *cache, byte address) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].address == address) {
            return i;
        }
    }
    return -1;
}

void update_cache_line(cache_line *cache, int cache_idx, byte address, byte value, byte state) {
    cache[cache_idx].address = address;
    cache[cache_idx].value = value;
    cache[cache_idx].state = state;
}

void handle_cache_hit(int thread_num, int cache_idx, decoded_inst inst) {
    cache_line *cache = caches[thread_num];
    
    if (cache[cache_idx].state == MODIFIED) {
        if (inst.type == 1) {
            cache[cache_idx].value = inst.value;
        }
        printf("Thread %d: %s %d: %d (cache hit, modified state)\n",
               thread_num, inst.type == 0 ? "RD" : "WR", inst.address, cache[cache_idx].value);
    } else if (cache[cache_idx].state == SHARED) {
        if (inst.type == 1) {
            omp_set_lock(&bus_locks[inst.address]);
            
            for (int i = 0; i < omp_get_num_procs(); i++) {
                if (i != thread_num) {
                    int other_cache_idx = cache_lookup(caches[i], inst.address);
                    if (other_cache_idx != -1) {
                        caches[i][other_cache_idx].state = INVALID;
                    }
                }
            }
            cache[cache_idx].state = MODIFIED;
            cache[cache_idx].value = inst.value;

            omp_unset_lock(&bus_locks[inst.address]);

            printf("Thread %d: WR %d: %d (cache hit, shared state)\n",
                   thread_num, inst.address, inst.value);
        } else {
            printf("Thread %d: RD %d: %d (cache hit, shared state)\n",
                   thread_num, inst.address, cache[cache_idx].value);
        }
    } else {
        if (inst.type == 0) {
            printf("Thread %d: RD %d: %d (cache hit, exclusive state)\n",
                   thread_num, inst.address, cache[cache_idx].value);
        } else {
            cache[cache_idx].value = inst.value;
            cache[cache_idx].state = MODIFIED;
            printf("Thread %d: WR %d: %d (cache hit, exclusive state)\n",
                   thread_num, inst.address, inst.value);
        }
    }
}

// This function implements the mock CPU loop that reads and writes data.
void cpu_thread(int thread_num) {
    char filename[15];
    sprintf(filename, "input_%d.txt", thread_num);
    FILE *inst_file = fopen(filename, "r");
    
    if (!inst_file) {
        perror("Failed to open input file");
        return;
    }
    
    cache_line *cache = caches[thread_num];
    char inst_line[20];
    
    while (fgets(inst_line, sizeof(inst_line), inst_file)) {
        decoded_inst inst = decode_inst_line(inst_line);
        int cache_idx = cache_lookup(cache, inst.address);
        
        if (inst.type == 0) {
            if (cache_idx != -1) {
                handle_cache_hit(thread_num, cache_idx, inst);
            } else {
                int replacement_idx = thread_num % CACHE_SIZE;
                printf("Thread %d: RD %d: %d (cache miss)\n", thread_num, inst.address, memory[inst.address]);
                update_cache_line(cache, replacement_idx, inst.address, memory[inst.address], SHARED);
            }
        }
        else if (inst.type == 1) {
            if (cache_idx != -1) {
                handle_cache_hit(thread_num, cache_idx, inst);
            } else {
                int replacement_idx = thread_num % CACHE_SIZE;
                printf("Thread %d: WR %d: %d (cache miss)\n", thread_num, inst.address, inst.value);
                
                if (replacement_idx != -1 && cache[replacement_idx].state == SHARED) {
                    omp_set_lock(&bus_locks[inst.address]);
                    
                    for (int i = 0; i < omp_get_num_procs(); i++) {
                        if (i != thread_num) {
                            int other_cache_idx = cache_lookup(caches[i], inst.address);
                            if (other_cache_idx != -1) {
                                caches[i][other_cache_idx].state = INVALID;
                            }
                        }
                    }
                    
                    omp_unset_lock(&bus_locks[inst.address]);
                }
                
                update_cache_line(cache, replacement_idx, inst.address, inst.value, MODIFIED);
                
                memory[inst.address] = inst.value;
            }
        }
    }
    fclose(inst_file);
}

int main(int argc, char *argv[]) {
    memset(memory, 0, sizeof(memory));
    
    int num_threads = (argc > 1) ? atoi(argv[1]) : 1;
    omp_set_num_threads(num_threads);

    bus_locks = (omp_lock_t *)malloc(MEMORY_SIZE * sizeof(omp_lock_t));
    for (int i = 0; i < MEMORY_SIZE; i++) {
        omp_init_lock(&bus_locks[i]);
    }
    
    caches = (cache_line **)malloc(num_threads * sizeof(cache_line *));
    for (int i = 0; i < num_threads; i++) {
        caches[i] = (cache_line *)malloc(CACHE_SIZE * sizeof(cache_line));
        for (int j = 0; j < CACHE_SIZE; j++) {
            caches[i][j].address = -1;
            caches[i][j].value = 0;
            caches[i][j].state = INVALID;
        }
    }
    
    #pragma omp parallel
    {
        int thread_num = omp_get_thread_num();
        cpu_thread(thread_num);
    }

    for (int i = 0; i < MEMORY_SIZE; i++) {
        omp_destroy_lock(&bus_locks[i]);
    }
    free(bus_locks);

    for (int i = 0; i < num_threads; i++) {
        free(caches[i]);
    }
    free(caches);

    return 0;
}
