// image_editor_mpi.c
// MPI parallel implementation for P5/P6 PNM images
// Supports: SELECT ALL, BENCH GAUSS_SOBEL, and SAVE
// Compilation: mpicc -O3 -march=native -std=c11 image_editor_mpi.c -lm -o editor_mpi
// Run: mpirun -np 8 ./editor_mpi < bench.in

#define _POSIX_C_SOURCE 200809L
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

typedef struct {
    int w, h, ch;
    int x1, y1, x2, y2;   // Selection coordinates [x1,x2) [y1,y2)
    int loaded;

    // MPI specific data
    int rank, size;
    int start_row;        // First global row owned by this rank
    int local_h;          // Number of global rows owned by this rank

    // Local buffers with HALO: (local_h + 2) rows
    // Row 0 = Top halo, Row local_h+1 = Bottom halo
    uint8_t *cur;
    uint8_t *next;
    size_t cap_bytes;     // Capacity of cur/next buffers

    // Metadata for scatter/gather (used only by Rank 0)
    int *counts;
    int *displs;
} MPIImage;

// Clamps double values to uint8_t range [0, 255]
static inline uint8_t clamp_u8_double(double v) {
    if (v < 0.0) return 0;
    if (v > 255.0) return 255;
    return (uint8_t)lrint(v);
}

// High-precision timer
static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

// ==== PNM Loader (Rank 0 only) ====
// Skips whitespace and comments to read the next token
static int read_token(FILE *f, char *buf, size_t n) {
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (isspace(c)) continue;
        if (c == '#') {
            while ((c = fgetc(f)) != EOF && c != '\n') {}
            continue;
        }
        ungetc(c, f);
        break;
    }
    if (c == EOF) return 0;
    size_t i = 0;
    while ((c = fgetc(f)) != EOF && !isspace(c)) {
        if (c == '#') {
            while ((c = fgetc(f)) != EOF && c != '\n') {}
            break;
        }
        if (i + 1 < n) buf[i++] = (char)c;
    }
    buf[i] = '\0';
    return i > 0;
}

static int load_pnm_rank0(uint8_t **out, int *w, int *h, int *ch, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    char tok[64];
    if (!read_token(f, tok, sizeof(tok))) { fclose(f); return 0; }

    int C = 0;
    if (strcmp(tok, "P5") == 0) C = 1;      // Grayscale
    else if (strcmp(tok, "P6") == 0) C = 3; // RGB
    else { fclose(f); return 0; }

    if (!read_token(f, tok, sizeof(tok))) { fclose(f); return 0; }
    int W = atoi(tok);
    if (!read_token(f, tok, sizeof(tok))) { fclose(f); return 0; }
    int H = atoi(tok);
    if (!read_token(f, tok, sizeof(tok))) { fclose(f); return 0; }
    int maxval = atoi(tok);

    if (W <= 0 || H <= 0 || maxval <= 0 || maxval > 255) { fclose(f); return 0; }

    size_t sz = (size_t)W * (size_t)H * (size_t)C;
    uint8_t *data = (uint8_t*)malloc(sz);
    if (!data) { fclose(f); return 0; }

    size_t got = fread(data, 1, sz, f);
    fclose(f);
    if (got != sz) { free(data); return 0; }

    *out = data;
    *w = W; *h = H; *ch = C;
    return 1;
}

// ==== MPI Image Management ====
static void mpi_img_free(MPIImage *img) {
    free(img->cur);  img->cur = NULL;
    free(img->next); img->next = NULL;
    img->cap_bytes = 0;
    img->loaded = 0;
    if (img->rank == 0) {
        free(img->counts); img->counts = NULL;
        free(img->displs); img->displs = NULL;
    }
}

static int ensure_local_buffers(MPIImage *img, size_t need) {
    if (img->cap_bytes >= need) return 1;
    uint8_t *a = (uint8_t*)realloc(img->cur, need);
    uint8_t *b = (uint8_t*)realloc(img->next, need);
    if (!a || !b) return 0;
    img->cur = a; img->next = b;
    img->cap_bytes = need;
    return 1;
}

// Block distribution of rows across processes
static void compute_row_partition(MPIImage *img) {
    int base = img->h / img->size;
    int rem  = img->h % img->size;
    img->local_h = base + (img->rank < rem ? 1 : 0);
    img->start_row = img->rank * base + (img->rank < rem ? img->rank : rem);
}

// Prepare displacement and counts for Scatterv/Gatherv
static void build_counts_displs_rank0(MPIImage *img) {
    if (img->rank != 0) return;
    img->counts = (int*)malloc(img->size * sizeof(int));
    img->displs = (int*)malloc(img->size * sizeof(int));
    int base = img->h / img->size, rem = img->h % img->size, disp = 0;
    for (int r = 0; r < img->size; r++) {
        int lh = base + (r < rem ? 1 : 0);
        img->counts[r] = lh * img->w * img->ch;
        img->displs[r] = disp;
        disp += img->counts[r];
    }
}

// Duplicates top/bottom borders for the very first and last rows of the image
static void fill_boundary_halo(MPIImage *img) {
    int rb = img->w * img->ch;
    if (img->local_h <= 0) return;
    if (img->rank == 0) 
        memcpy(img->cur, img->cur + rb, rb);
    if (img->rank == img->size - 1) 
        memcpy(img->cur + (size_t)(img->local_h + 1) * rb, img->cur + (size_t)img->local_h * rb, rb);
}

// Swaps boundary rows between adjacent MPI processes
static void exchange_halo(MPIImage *img) {
    int rb = img->w * img->ch;
    MPI_Status st;
    int up = img->rank - 1, down = img->rank + 1;

    if (up >= 0) {
        MPI_Sendrecv(img->cur + (size_t)rb, rb, MPI_BYTE, up, 10,
                     img->cur, rb, MPI_BYTE, up, 11, MPI_COMM_WORLD, &st);
    }
    if (down < img->size) {
        MPI_Sendrecv(img->cur + (size_t)img->local_h * rb, rb, MPI_BYTE, down, 11,
                     img->cur + (size_t)(img->local_h + 1) * rb, rb, MPI_BYTE, down, 10, MPI_COMM_WORLD, &st);
    }
    fill_boundary_halo(img);
}

// ==== MPI Data Transfer ====
static int mpi_load_scatter(MPIImage *img, const char *path) {
    uint8_t *full = NULL;
    int ok = 1;
    if (img->rank == 0) ok = load_pnm_rank0(&full, &img->w, &img->h, &img->ch, path);
    MPI_Bcast(&ok, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (!ok) return 0;

    MPI_Bcast(&img->w, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&img->h, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&img->ch, 1, MPI_INT, 0, MPI_COMM_WORLD);

    compute_row_partition(img);
    size_t rb = (size_t)img->w * img->ch;
    if (!ensure_local_buffers(img, (img->local_h + 2) * rb)) return 0;

    build_counts_displs_rank0(img);
    MPI_Scatterv(full, img->counts, img->displs, MPI_BYTE,
                 img->cur + rb, img->local_h * img->w * img->ch, MPI_BYTE, 0, MPI_COMM_WORLD);

    if (img->rank == 0) free(full);
    img->x1 = 0; img->y1 = 0; img->x2 = img->w; img->y2 = img->h;
    img->loaded = 1;
    exchange_halo(img);
    return 1;
}

static int mpi_save_gather(const MPIImage *img, const char *path) {
    if (!img->loaded) return 0;
    uint8_t *full = NULL;
    if (img->rank == 0) full = malloc((size_t)img->w * img->h * img->ch);

    MPI_Gatherv(img->cur + (img->w * img->ch), img->local_h * img->w * img->ch, MPI_BYTE,
                full, img->counts, img->displs, MPI_BYTE, 0, MPI_COMM_WORLD);

    if (img->rank == 0) {
        FILE *f = fopen(path, "wb");
        if (!f) { free(full); return 0; }
        fprintf(f, (img->ch == 1) ? "P5\n" : "P6\n");
        fprintf(f, "%d %d\n255\n", img->w, img->h);
        fwrite(full, 1, (size_t)img->w * img->h * img->ch, f);
        fclose(f); free(full);
    }
    return 1;
}

// ==== Image Processing Kernels ====
static void mpi_apply_conv3x3(MPIImage *img, const double K[3][3], const char *msg) {
    if (!img->loaded) return;
    int x1 = img->x1, y1 = img->y1, x2 = img->x2, y2 = img->y2;
    // Handle global edges
    if (x1 == 0) x1++; if (y1 == 0) y1++; if (x2 == img->w) x2--; if (y2 == img->h) y2--;
    
    size_t rb = (size_t)img->w * img->ch;
    memcpy(img->next, img->cur, (img->local_h + 2) * rb);

    for (int ly = 1; ly <= img->local_h; ly++) {
        int gy = img->start_row + (ly - 1);
        if (gy < y1 || gy >= y2) continue;
        for (int x = x1; x < x2; x++) {
            for (int c = 0; c < img->ch; c++) {
                double sum = 0.0;
                for (int ky = -1; ky <= 1; ky++)
                    for (int kx = -1; kx <= 1; kx++)
                        sum += K[ky+1][kx+1] * img->cur[(ly+ky)*rb + (x+kx)*img->ch + c];
                img->next[ly*rb + x*img->ch + c] = clamp_u8_double(sum);
            }
        }
    }
    uint8_t *t = img->cur; img->cur = img->next; img->next = t;
    exchange_halo(img);
    if (img->rank == 0) printf("%s done\n", msg);
}

static void mpi_apply_sobel(MPIImage *img) {
    if (!img->loaded) return;
    int x1 = img->x1, y1 = img->y1, x2 = img->x2, y2 = img->y2;
    if (x1 == 0) x1++; if (y1 == 0) y1++; if (x2 == img->w) x2--; if (y2 == img->h) y2--;

    static const int Gx[3][3] = {{-1,0,1},{-2,0,2},{-1,0,1}}, Gy[3][3] = {{1,2,1},{0,0,0},{-1,-2,-1}};
    size_t rb = (size_t)img->w * img->ch;
    memcpy(img->next, img->cur, (img->local_h + 2) * rb);

    for (int ly = 1; ly <= img->local_h; ly++) {
        int gy = img->start_row + (ly - 1);
        if (gy < y1 || gy >= y2) continue;
        for (int x = x1; x < x2; x++) {
            for (int c = 0; c < img->ch; c++) {
                int sx = 0, sy = 0;
                for (int ky = -1; ky <= 1; ky++) {
                    for (int kx = -1; kx <= 1; kx++) {
                        int v = img->cur[(ly+ky)*rb + (x+kx)*img->ch + c];
                        sx += v * Gx[ky+1][kx+1]; sy += v * Gy[ky+1][kx+1];
                    }
                }
                img->next[ly*rb + x*img->ch + c] = clamp_u8_double(sqrt(sx*sx + sy*sy));
            }
        }
    }
    uint8_t *t = img->cur; img->cur = img->next; img->next = t;
    exchange_halo(img);
    if (img->rank == 0) printf("APPLY SOBEL done\n");
}

static void mpi_bench(MPIImage *img, int iters, const char *what) {
    static const double K_GAUSS[3][3] = {{1./16,2./16,1./16},{2./16,4./16,2./16},{1./16,2./16,1./16}};
    if (!img->loaded || strcmp(what, "GAUSS_SOBEL") != 0) { if (img->rank == 0) printf("Invalid/No image\n"); return; }

    MPI_Barrier(MPI_COMM_WORLD);
    double t0 = now_sec();
    for (int i = 0; i < iters; i++) {
        mpi_apply_conv3x3(img, K_GAUSS, "APPLY GAUSSIAN_BLUR");
        mpi_apply_sobel(img);
    }
    MPI_Barrier(MPI_COMM_WORLD);
    if (img->rank == 0) printf("BENCH %s iters=%d time=%.6f sec\n", what, iters, now_sec() - t0);
}

// ==== Command Processing ====
typedef enum { CMD_INVALID, CMD_LOAD, CMD_SAVE, CMD_SELECT_ALL, CMD_BENCH, CMD_EXIT } CmdType;
typedef struct { int type, iters; char arg1[256]; } Cmd;

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);
    MPIImage img; memset(&img, 0, sizeof(img));
    MPI_Comm_rank(MPI_COMM_WORLD, &img.rank);
    MPI_Comm_size(MPI_COMM_WORLD, &img.size);

    while (1) {
        Cmd cmd = {0};
        if (img.rank == 0) {
            char token[64];
            if (scanf("%63s", token) != 1) cmd.type = CMD_EXIT;
            else if (strcmp(token, "LOAD") == 0) { cmd.type = CMD_LOAD; scanf("%255s", cmd.arg1); }
            else if (strcmp(token, "SAVE") == 0) { cmd.type = CMD_SAVE; scanf("%255s", cmd.arg1); }
            else if (strcmp(token, "SELECT") == 0) { scanf("%63s", token); cmd.type = (strcmp(token, "ALL")==0) ? CMD_SELECT_ALL : CMD_INVALID; }
            else if (strcmp(token, "BENCH") == 0) { cmd.type = CMD_BENCH; scanf("%d %255s", &cmd.iters, cmd.arg1); }
            else if (strcmp(token, "EXIT") == 0) cmd.type = CMD_EXIT;
        }
        MPI_Bcast(&cmd, sizeof(Cmd), MPI_BYTE, 0, MPI_COMM_WORLD);
        if (cmd.type == CMD_EXIT) break;

        if (cmd.type == CMD_LOAD) mpi_load_scatter(&img, cmd.arg1);
        else if (cmd.type == CMD_SELECT_ALL && img.loaded) { img.x1 = 0; img.y1 = 0; img.x2 = img.w; img.y2 = img.h; }
        else if (cmd.type == CMD_BENCH) mpi_bench(&img, cmd.iters, cmd.arg1);
        else if (cmd.type == CMD_SAVE) mpi_save_gather(&img, cmd.arg1);
    }

    mpi_img_free(&img);
    MPI_Finalize();
    return 0;
}
