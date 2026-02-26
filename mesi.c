#include <stdio.h>
#include <string.h>

/* Maximum number of processors supported */
#define MAX_P 32

int main(void) {
    int N, K;
    
    // Read number of processors (N) and number of operations (K)
    if (scanf("%d", &N) != 1) return 0;
    if (scanf("%d", &K) != 1) return 0;

    if (N > MAX_P) {
        fprintf(stderr, "Too many processors (max %d)\n", MAX_P);
        return 1;
    }

    /* Initialize cache states: I = Invalid, S = Shared, E = Exclusive, M = Modified */
    char state[MAX_P];
    for (int i = 0; i < N; ++i) state[i] = 'I';

    /* Table Header Output */
    printf("t\tAction\t");
    for (int i = 0; i < N; ++i)
        printf("StateP%d\t", i + 1);
    printf("Bus\tSource\n");

    /* Initial state (t0) - All caches start at Invalid */
    printf("t0\tinitial\t");
    for (int i = 0; i < N; ++i)
        printf("%c\t", state[i]);
    printf("-\t-\n");

    /* Process each memory operation (Read/Write) */
    for (int step = 0; step < K; ++step) {
        char op[16];
        if (scanf("%s", op) != 1) break;

        int pid;            /* Processor index (0-based) */
        char kind[3];       /* Operation type: Rd (Read) or Wr (Write) */
        
        // Parse operation format: e.g., "P1Rd" -> pid=1, kind="Rd"
        if (sscanf(op, "P%d%2s", &pid, kind) != 2) {
            fprintf(stderr, "Invalid operation: %s\n", op);
            return 1;
        }
        pid--;              /* Convert P1 to index 0 */

        int isRead = (strcmp(kind, "Rd") == 0);
        char bus[16] = "-";
        char src[16] = "-";

        /* Print current time step and action */
        printf("t%d\t%s\t", step + 1, op);

        if (isRead) {
            /* ----------- Processor Read (PrRd) ----------- */
            if (state[pid] == 'I') {
                // Cache Miss: Must issue Bus Read
                strcpy(bus, "BusRd");
                int provider = -1;
                int hasCopy = 0;

                // Check if any other cache has the data
                for (int i = 0; i < N; ++i) {
                    if (i == pid) continue;
                    if (state[i] != 'I') {
                        hasCopy = 1;
                        if (provider == -1) provider = i;
                    }
                }

                if (!hasCopy) {
                    /* No other cache has it: load from Memory, state becomes Exclusive */
                    strcpy(src, "Mem");
                    state[pid] = 'E';
                } else {
                    /* Data exists in another cache: state becomes Shared */
                    for (int i = 0; i < N; ++i) {
                        if (i == pid) continue;
                        // Transition M or E to S because a new reader appeared
                        if (state[i] == 'M' || state[i] == 'E')
                            state[i] = 'S';
                    }
                    state[pid] = 'S';
                    snprintf(src, sizeof(src), "Cache%d", provider + 1);
                }
            }
            // If state is M, E, or S: Read Hit (no bus action needed)
        } else {
            /* ----------- Processor Write (PrWr) ----------- */
            if (state[pid] == 'M') {
                /* Write Hit: Already Modified, no bus action */
            } else if (state[pid] == 'E') {
                /* Silent Upgrade: Exclusive to Modified, no bus activity required */
                state[pid] = 'M';
            } else if (state[pid] == 'S') {
                /* Write Upgrade: Issue BusRdX to invalidate other Shared copies */
                strcpy(bus, "BusRdX");
                for (int i = 0; i < N; ++i) {
                    if (i != pid && state[i] == 'S')
                        state[i] = 'I';
                }
                state[pid] = 'M';
                snprintf(src, sizeof(src), "Cache%d", pid + 1);
            } else { /* state[pid] == 'I' */
                /* Write Miss: Issue BusRdX to get data and invalidate others */
                strcpy(bus, "BusRdX");
                int provider = -1;
                int hasCopy = 0;

                for (int i = 0; i < N; ++i) {
                    if (i == pid) continue;
                    if (state[i] != 'I') {
                        hasCopy = 1;
                        if (provider == -1) provider = i;
                    }
                }

                if (!hasCopy) {
                    strcpy(src, "Mem");
                } else {
                    snprintf(src, sizeof(src), "Cache%d", provider + 1);
                    // Invalidate everyone else
                    for (int i = 0; i < N; ++i) {
                        if (i != pid && state[i] != 'I')
                            state[i] = 'I';
                    }
                }
                state[pid] = 'M';
            }
        }

        /* Print final states for all processors after this step */
        for (int i = 0; i < N; ++i)
            printf("%c\t", state[i]);
        printf("%s\t%s\n", bus, src);
    }

    return 0;
}
