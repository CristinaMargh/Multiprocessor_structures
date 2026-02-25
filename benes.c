#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

/**
 * Struct representing a Benes Network.
 * N: Number of inputs/outputs.
 * k: log2(N).
 * stages: Total number of stages (2k - 1).
 * sw: 2D array [stage][switch_index] storing configuration (0=straight, 1=cross).
 */
typedef struct {
    int N, k, stages;
    uint8_t **sw;   
} Benes;

// ---------- Utilities ----------

/* Integer log2 for unsigned values */
static int ilog2u(unsigned x) {
    int k = -1;
    while (x) {
        x >>= 1;
        k++;
    }
    return k;
}

/* Safe malloc with error checking */
static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) {
        perror("malloc");
        exit(1);
    }
    return p;
}

/* Initialize Benes structure and allocate switch memory */
static void benes_init(Benes *b, int N) {
    b->N = N;
    b->k = ilog2u((unsigned)N);
    b->stages = 2 * b->k - 1;

    b->sw = (uint8_t **)xmalloc(sizeof(uint8_t *) * b->stages);
    for (int s = 0; s < b->stages; s++)
        b->sw[s] = (uint8_t *)calloc(N / 2, 1);
}

/* Free allocated memory for Benes structure */
static void benes_free(Benes *b) {
    for (int s = 0; s < b->stages; s++)
        free(b->sw[s]);
    free(b->sw);
}

// ---------- Recursive Routing (Lee-Paull Algorithm) ----------

static void route_rec(int N, const int *perm, int stage_off, int wire_off, Benes *b) {
    int k_local = ilog2u((unsigned)N);
    int s_first = stage_off;
    int s_last  = stage_off + 2 * k_local - 2;

    // Base case: 2x2 switch
    if (N == 2) {
        b->sw[s_first][wire_off / 2] = (perm[0] == 1) ? 1 : 0;
        return;
    }

    int m = N / 2;

    // 1) Find Output Partners: pairs of outputs sharing a switch
    int *first = xmalloc(sizeof(int) * m);
    int *second = xmalloc(sizeof(int) * m);
    for (int p = 0; p < m; p++) first[p] = second[p] = -1;

    for (int i = 0; i < N; i++) {
        int o = perm[i], pair = o >> 1;
        if (first[pair] == -1) first[pair] = i;
        else second[pair] = i;
    }

    int *opart = xmalloc(sizeof(int) * N);
    for (int p = 0; p < m; p++) {
        int a = first[p], c = second[p];
        opart[a] = c; opart[c] = a;
    }

    // 2) Coloring/Path tracing to decide upper vs lower subnetwork
    uint8_t *up_in = xmalloc(N);
    uint8_t *vis = xmalloc(N);
    memset(up_in, 0, N);
    memset(vis, 0, N);

    for (int start = 0; start < N; start++) {
        if (vis[start]) continue;
        int cur = start, col = 1; // col 1 = Upper, 0 = Lower
        while (!vis[cur]) {
            vis[cur] = 1;
            up_in[cur] = (uint8_t)col;
            int ip = cur ^ 1; // input partner
            if (!vis[ip]) {
                cur = ip;
                col ^= 1;
                continue;
            }
            cur = opart[cur]; // output partner path
            col ^= 1;
        }
    }
    free(vis);

    // 3) Map input decisions to output decisions through the permutation
    uint8_t *up_out = xmalloc(N);
    for (int i = 0; i < N; i++)
        up_out[perm[i]] = up_in[i];

    // Configure the first and last stages of the current Benes layer
    int base = wire_off / 2;
    for (int p = 0; p < m; p++)
        b->sw[s_first][base + p] = up_in[2 * p] ? 0 : 1;
    for (int q = 0; q < m; q++)
        b->sw[s_last][base + q] = up_out[2 * q] ? 0 : 1;

    // 4) Build permutations for subnetworks
    int *inU = xmalloc(sizeof(int) * m), *inL = xmalloc(sizeof(int) * m);
    int *outU = xmalloc(sizeof(int) * m), *outL = xmalloc(sizeof(int) * m);
    int cu = 0, cl = 0;

    for (int i = 0; i < N; i++)
        if (up_in[i]) inU[cu++] = i; else inL[cl++] = i;

    cu = cl = 0;
    for (int o = 0; o < N; o++)
        if (up_out[o]) outU[cu++] = o; else outL[cl++] = o;

    int *rankU = xmalloc(sizeof(int) * N), *rankL = xmalloc(sizeof(int) * N);
    for (int r = 0; r < m; r++) {
        rankU[outU[r]] = r;
        rankL[outL[r]] = r;
    }

    int *permU = xmalloc(sizeof(int) * m), *permL = xmalloc(sizeof(int) * m);
    for (int r = 0; r < m; r++)
        permU[r] = rankU[perm[inU[r]]];
    for (int r = 0; r < m; r++)
        permL[r] = rankL[perm[inL[r]]];

    // 5) Recurse on upper and lower sub-Benes networks
    route_rec(m, permU, stage_off + 1, wire_off, b);
    route_rec(m, permL, stage_off + 1, wire_off + m, b);

    // Cleanup local allocations
    free(first); free(second); free(opart);
    free(up_in); free(up_out);
    free(inU); free(inL); free(outU); free(outL);
    free(rankU); free(rankL);
    free(permU); free(permL);
}

static void route(int N, const int *perm, Benes *b) {
    route_rec(N, perm, 0, 0, b);
}

// ---------- Simulator (Verification) ----------

static void sim_rec(const Benes *b, int N, int stage_off, int wire_off, int *w) {
    int k_local = ilog2u((unsigned)N);
    int s_first = stage_off;
    int s_last  = stage_off + 2 * k_local - 2;

    int base = wire_off / 2;
    // Stage 1 switch logic
    for (int p = 0; p < N / 2; ++p) {
        int a = wire_off + 2 * p, c = a + 1;
        if (b->sw[s_first][base + p]) {
            int t = w[a]; w[a] = w[c]; w[c] = t;
        }
    }

    if (N > 2) {
        int m = N / 2;
        // Unshuffle to subnetworks
        int *upper = xmalloc(sizeof(int) * m), *lower = xmalloc(sizeof(int) * m);
        int cu = 0, cl = 0;
        for (int p = 0; p < N / 2; ++p) {
            int a = wire_off + 2 * p, c = a + 1;
            if (b->sw[s_first][base + p] == 0) {
                upper[cu++] = w[a]; lower[cl++] = w[c];
            } else {
                lower[cl++] = w[a]; upper[cu++] = w[c];
            }
        }
        for (int i = 0; i < m; i++) w[wire_off + i] = upper[i];
        for (int i = 0; i < m; i++) w[wire_off + m + i] = lower[i];
        free(upper); free(lower);

        sim_rec(b, m, stage_off + 1, wire_off, w);
        sim_rec(b, m, stage_off + 1, wire_off + m, w);

        // Shuffle back
        int *tmp = xmalloc(sizeof(int) * N);
        for (int p = 0; p < m; p++) {
            tmp[2 * p] = w[wire_off + p];
            tmp[2 * p + 1] = w[wire_off + m + p];
        }
        for (int i = 0; i < N; i++) w[wire_off + i] = tmp[i];
        free(tmp);
    }

    // Last stage switch logic
    for (int p = 0; p < N / 2; ++p) {
        int a = wire_off + 2 * p, c = a + 1;
        if (b->sw[s_last][base + p]) {
            int t = w[a]; w[a] = w[c]; w[c] = t;
        }
    }
}

static int verify(const Benes *b, const int *perm) {
    int N = b->N;
    int *w = xmalloc(sizeof(int) * N);
    for (int i = 0; i < N; i++) w[i] = i;

    sim_rec(b, N, 0, 0, w);

    int ok = 1;
    for (int i = 0; i < N; i++)
        if (w[perm[i]] != i) { ok = 0; break; }

    free(w);
    return ok;
}

// ---------- Main & CLI Parsing ----------

static int parse_perm(const char *s, int **out) {
    int cap = 128, n = 0;
    int *v = xmalloc(sizeof(int) * cap);
    const char *p = s;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        char *e;
        long val = strtol(p, &e, 10);
        if (p == e) { if (*p == ',') { p++; continue; } break; }
        if (n == cap) { cap *= 2; v = realloc(v, sizeof(int) * cap); }
        v[n++] = (int)val;
        p = e;
        if (*p == ',') p++;
    }
    *out = v;
    return n;
}

int main(int argc, char **argv) {
    int k = -1, *perm = NULL, N = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-k") && i + 1 < argc)
            k = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-perm") && i + 1 < argc)
            N = parse_perm(argv[++i], &perm);
    }

    if (k < 0) k = 3;
    if (!perm) {
        N = 1 << k;
        perm = xmalloc(sizeof(int) * N);
        for (int i = 0; i < N; i++) perm[i] = i; // Identity
        fprintf(stderr, "(Default) Using identity permutation N=%d\n", N);
    } else if (N != (1 << k)) {
        fprintf(stderr, "Error: -k defines N=%d but -perm contains %d items.\n", 1 << k, N);
        return 1;
    }

    Benes b;
    benes_init(&b, N);
    route(N, perm, &b);

    // Print switch configuration for each stage
    for (int s = 0; s < b.stages; s++) {
        printf("stage %d:", s);
        for (int i = 0; i < N / 2; i++)
            printf(" %d", b.sw[s][i]);
        printf("\n");
    }

    printf("Verification: %s\n", verify(&b, perm) ? "OK" : "FAILED");

    benes_free(&b);
    free(perm);
    return 0;
}
