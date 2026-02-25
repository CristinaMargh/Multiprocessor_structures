#include <stdio.h>
#include <math.h>

/**
 * Shuffle permutation: performs a circular left shift on the bits.
 * It identifies the number of bits 'k', masks bits outside the block,
 * and moves the Most Significant Bit (MSB) to the end.
 */
int shuffle(int i, int N) {
    int k = 0;
    while ((1 << k) < N) k++;
    return ((i << 1) & (N - 1)) | (i >> (k - 1));
}

/**
 * Function that displays the routing path of a (source, destination) pair 
 * through an Omega Network using destination-based routing.
 */
void traseuOmega(int src, int dest, int k) {
    int N = 1 << k;  // N = 2^k
    int i, etapa;
    int val = src;

    printf("\n=== Path for pair (Source=%d, Destination=%d) ===\n", src, dest);

    // Extract destination bits to decide the switch control at each stage
    int bits[16];
    for (i = 0; i < k; i++)
        bits[i] = (dest >> (k - i - 1)) & 1;

    // Iterate through each stage of the network
    for (etapa = 0; etapa < k; etapa++) {
        int bloc = val / 2;
        int intrare = val % 2;
        int control = bits[etapa]; // Control bit from destination address
        char *tip = (control == 0) ? "STRAIGHT" : "CROSSED";

        printf("\nStage %d:\n", etapa + 1);
        printf(" Block %d | Input %d | Control: %d (%s)\n", bloc, intrare, control, tip);

        // Apply switching logic based on the control bit
        if (control == 1)
            val = bloc * 2 + (1 - intrare); // Crossed connection
        else
            val = bloc * 2 + intrare;     // Straight connection

        // Apply the perfect shuffle permutation (inter-stage connection)
        int val_shuffle = shuffle(val, N);
        printf(" After connection -> %d, after shuffle -> %d\n", val, val_shuffle);
        val = val_shuffle;
    }

    printf("=== Final Output reached: %d ===\n", val);
}

int main() {
    int k = 3;       // Network dimension 2^k x 2^k (8x8)
    int m = 2;       // Number of routing pairs
    int pairs[2][2] = { {0, 3}, {5, 6} };

    printf("Omega Network Simulation: %d x %d\n", 1 << k, 1 << k);

    for (int i = 0; i < m; i++) {
        traseuOmega(pairs[i][0], pairs[i][1], k);
    }

    return 0;
}
