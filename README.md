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
