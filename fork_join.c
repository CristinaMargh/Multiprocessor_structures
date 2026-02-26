#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

// Structure to pass data to each thread
// The main thread will create an instance for each worker thread.
typedef struct {
    int thread_id;      // Equivalent to omp_get_thread_num()
    int data_value;     // The broadcasted value
    int partial_result; // The result calculated by this specific thread
} thread_data_t;

#define NUM_THREADS 4
const int DATA_BROADCAST = 5;

// Function executed by each Pthread
void *worker_function(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;

    int tid = data->thread_id;
    int data_val = data->data_value;

    // Calculate partial result: multiplying the broadcast value by (ID + 1)
    int partial_result = data_val * (tid + 1);
    data->partial_result = partial_result;

    // Output result (only for WORKERs where tid != 0 to simulate Master/Worker logic)
    if (tid != 0) {
        printf("WORKER %d (of %d) computed result %d\n",
               tid, NUM_THREADS, partial_result);
    }

    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    pthread_t threads[NUM_THREADS];
    thread_data_t thread_data[NUM_THREADS];
    int rc;
    int total_sum = 0;

    printf("MASTER (thread 0): broadcasting value %d to all workers...\n", DATA_BROADCAST);

    // Forking phase: Creating threads
    for (int i = 0; i < NUM_THREADS; ++i) {
        // Initialize data for thread 'i'
        thread_data[i].thread_id = i;
        thread_data[i].data_value = DATA_BROADCAST;
        thread_data[i].partial_result = 0;

        // Create the thread and pass the specific data structure
        rc = pthread_create(&threads[i], NULL, worker_function, (void *)&thread_data[i]);
        
        if (rc) {
            fprintf(stderr, "ERROR; return code from pthread_create() is %d\n", rc);
            return 1;
        }
    }

    // Joining phase: Wait for all threads to finish and collect results
    for (int i = 0; i < NUM_THREADS; ++i) {
        rc = pthread_join(threads[i], NULL);
        
        if (rc) {
            fprintf(stderr, "ERROR; return code from pthread_join() is %d\n", rc);
            return 1;
        }

        // Reduction: Accumulate the partial result from each thread into the total sum
        total_sum += thread_data[i].partial_result;
    }

    // Final output performed by the MASTER thread
    printf("MASTER: total sum of results = %d\n", total_sum);

    return 0;
}
