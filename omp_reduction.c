#include <stdio.h>
#include <omp.h>

int main(int argc, char *argv[]) {
    int data = 5;
    int total_sum = 0;

    printf("MASTER (thread 0): broadcasting value %d to all workers...\n", data);

    // Parallel region starts here
    // reduction(+:total_sum) creates a private copy of total_sum for each thread,
    // and then aggregates them into the global variable at the end.
    #pragma omp parallel reduction(+:total_sum)
    {
        int tid = omp_get_thread_num();     // Current thread ID
        int numthreads = omp_get_num_threads(); // Total number of threads in the team
        
        // Local calculation: Each thread multiplies the 'broadcast' data by its ID + 1
        int partial_result = data * (tid + 1);

        // Printing worker activity (excluding thread 0 to highlight master/worker roles)
        if (tid != 0) {
            printf("WORKER %d (of %d) computed result %d\n",
                   tid, numthreads, partial_result);
        }

        // Accumulate partial result into the thread's local version of total_sum
        total_sum += partial_result;

        // Synchronization point: wait for all threads to finish their computation
        #pragma omp barrier

        // Only one thread (usually the master) executes this block
        #pragma omp single
        {
            printf("MASTER: total sum of results = %d\n", total_sum);
        }
    } // End of parallel region - global total_sum is now updated

    return 0;
}
