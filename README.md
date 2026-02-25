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
