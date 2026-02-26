# Multiprocessor_structures
Multiprocessor structures laboratories contains:
# Omega Network Routing Simulator

This program simulates data packet routing within an Omega Multistage Interconnection Network (MIN). It calculates the specific switch blocks, control signals (Straight or Crossed), and bit-shuffle permutations required to route source-destination pairs across the network's stages using destination-based routing logic.

### Key Features
* **Shuffle Permutation:** Implements the circular left shift required for inter-stage wiring.
* **Stage Tracking:** Displays the specific block and input used at every stage of the routing process.
* **Switch Control:** Determines the connection type (STRAIGHT or CROSSED) based on the destination's bit pattern.

### How to Run
1. Save the code as `omega_network.c`.
2. Compile using: `gcc omega_network.c -o omega_sim -lm`.
3. Execute: `./omega_sim`.

# Benes Network Simulator

This program simulates a **Benes Network**, a rearrangeable non-blocking multistage interconnection network. It uses the recursive **Lee-Paull algorithm** to determine switch configurations (Straight or Cross) required to satisfy any arbitrary permutation of $N$ inputs.

##  Features
- **Recursive Routing:** Dynamically builds a $(2\log_2 N - 1)$ stage network.
- **Lee-Paull Algorithm:** Efficiently colors paths to split traffic between upper and lower subnetworks.
- **Integrated Verification:** Simulates data flow through the configured switches to confirm the permutation is correctly routed.

# Pthread Broadcast and Reduction Example

This project demonstrates a basic **Fork-Join** parallel programming pattern using the `pthread` library in C. It simulates a Master-Worker architecture where data is broadcasted to multiple threads, processed, and then aggregated (reduced).


## Features
* **Data Broadcasting**: A constant value is distributed to all worker threads via a shared structure.
* **Parallel Processing**: Each thread performs a calculation based on its unique `thread_id`.
* **Reduction**: The Master thread aggregates partial results from all workers using `pthread_join`.

## How it Works
1. **Initialization**: The Master thread defines a set of structures to hold thread-specific data.
2. **Creation (Fork)**: `pthread_create` spawns 4 threads. Each thread receives its ID and the broadcasted value.
3. **Execution**: Each thread calculates $Result = Value \times (ID + 1)$.
4. **Synchronization (Join)**: The Master thread waits for all threads to complete.
5. **Aggregation**: The Master thread sums up all `partial_result` values and prints the final total.

## Compilation and Execution

To compile the program, you need to link the `lpthread` library.

# MESI Cache Coherence Simulator

This program simulates the **MESI protocol** state machine in a multi-processor environment with a shared bus. It tracks cache line transitions for multiple processors as they perform Read and Write operations.

## MESI States
- **M (Modified)**: Cache line is present only in the current cache and has been modified (dirty).
- **E (Exclusive)**: Cache line is present only in the current cache but is clean (matches memory).
- **S (Shared)**: Cache line may be stored in other caches and is clean.
- **I (Invalid)**: Cache line is invalid/not present.

## Input Format
The program reads from standard input:
1. `N`: Number of processors.
2. `K`: Number of operations.
3. List of operations in the format `P<id><type>` (e.g., `P1Rd` for Processor 1 Read, `P2Wr` for Processor 2 Write).

# MPI Image Editor (Distributed Convolution) : Final project

This application is a distributed image processor designed to handle PNM images (P5/P6) across a cluster or multi-core system using the **Message Passing Interface (MPI)**.

## Architecture
- **Rank 0 (Master)**: Handles I/O operations, command parsing from `stdin`, and coordinates the workers.
- **Workers**: Perform parallel computation on assigned horizontal strips of the image.
- **Halo Exchange**: To compute 3x3 convolutions at the borders of each strip, processes exchange "halo" rows with their neighbors using `MPI_Sendrecv`.

## Supported Operations
1. **LOAD <file>**: Rank 0 reads the file and scatters the rows across all processes.
2. **SELECT ALL**: Resets the processing area to the full image dimensions.
3. **BENCH <iters> GAUSS_SOBEL**: Runs a benchmark sequence consisting of a Gaussian Blur followed by a Sobel Edge Detection filter.
4. **SAVE <file>**: Gathers all image strips back to Rank 0 and writes the final PNM file.
5. **EXIT**: Terminates all MPI processes.

## Performance Optimization
* **Block Distribution**: Rows are distributed evenly to balance the computational load.
* **Double Buffering**: Uses `cur` and `next` buffers to avoid data race conditions during stencil calculations.
* **Native Tuning**: Compiled with `-O3 -march=native` for SIMD auto-vectorization.

## Compilation
```bash
mpicc -O3 -march=native -std=c11 image_editor_mpi.c -lm -o editor_mpi

