// image_editor_omp.c
// OpenMP parallel pentru P5/P6 (PGM/PPM binar) + selectie + filtre 3x3 + Sobel + Equalize + BENCH
// gcc -O3 -march=native -std=c11 -fopenmp image_editor_omp.c -lm -o editor_omp

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

#ifdef _OPENMP
#include <omp.h>
#endif

typedef struct {
    int w, h;           // columns, rows
    int ch;             // 1 (P5) sau 3 (P6)
    uint8_t *data;      // size = w*h*ch
    uint8_t *tmp;       // buffer temporar (reutilizat)
    size_t tmp_cap;
    // selectie [x1,x2) [y1,y2)
    int x1, y1, x2, y2;
    int loaded;
} Image;

static inline uint8_t clamp_u8_int(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}
static inline uint8_t clamp_u8_double(double v) {
    if (v < 0.0) return 0;
    if (v > 255.0) return 255;
    return (uint8_t)lrint(v);
}
static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void img_free(Image *img) {
    free(img->data); img->data = NULL;
    free(img->tmp);  img->tmp  = NULL;
    img->tmp_cap = 0;
    img->loaded = 0;
    img->w = img->h = img->ch = 0;
    img->x1 = img->y1 = img->x2 = img->y2 = 0;
}

static int img_ensure_tmp(Image *img, size_t need) {
    if (img->tmp_cap >= need) return 1;
    uint8_t *p = (uint8_t*)realloc(img->tmp, need);
    if (!p) return 0;
    img->tmp = p;
    img->tmp_cap = need;
    return 1;
}

// Citeste urmatorul token din header (sare peste whitespace + comentarii #...).
static int read_token(FILE *f, char *buf, size_t n) {
    int c;
    // skip whitespace + comments
    while ((c = fgetc(f)) != EOF) {
        if (isspace(c)) continue;
        if (c == '#') { // comment line
            while ((c = fgetc(f)) != EOF && c != '\n') {}
            continue;
        }
        ungetc(c, f);
        break;
    }
    if (c == EOF) return 0;
    size_t i = 0;
    while ((c = fgetc(f)) != EOF && !isspace(c)) {
        if (c == '#') { // comment begins immediately after token
            while ((c = fgetc(f)) != EOF && c != '\n') {}
            break;
        }
        if (i + 1 < n) buf[i++] = (char)c;
    }
    buf[i] = '\0';
    return i > 0;
}

static int img_load_pnm(Image *img, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char tok[64];
    if (!read_token(f, tok, sizeof(tok))) { fclose(f); return 0; }
    int ch = 0;
    if (strcmp(tok, "P5") == 0) ch = 1;
    else if (strcmp(tok, "P6") == 0) ch = 3;
    else { fclose(f); return 0; }

    if (!read_token(f, tok, sizeof(tok))) { fclose(f); return 0; }
    int w = atoi(tok);
    if (!read_token(f, tok, sizeof(tok))) { fclose(f); return 0; }
    int h = atoi(tok);
    if (!read_token(f, tok, sizeof(tok))) { fclose(f); return 0; }
    int maxval = atoi(tok);
    if (w <= 0 || h <= 0 || maxval <= 0 || maxval > 255) { fclose(f); return 0; }

    size_t sz = (size_t)w * (size_t)h * (size_t)ch;
    uint8_t *data = (uint8_t*)malloc(sz);
    if (!data) { fclose(f); return 0; }

    size_t got = fread(data, 1, sz, f);
    fclose(f);
    if (got != sz) { free(data); return 0; }

    img_free(img);
    img->data = data;
    img->w = w; img->h = h; img->ch = ch;
    img->loaded = 1;
    img->x1 = 0; img->y1 = 0; img->x2 = w; img->y2 = h;
    return 1;
}

static int img_save_pnm(const Image *img, const char *path) {
    if (!img->loaded) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, (img->ch == 1) ? "P5\n" : "P6\n");
    fprintf(f, "%d %d\n255\n", img->w, img->h);
    size_t sz = (size_t)img->w * (size_t)img->h * (size_t)img->ch;
    size_t wr = fwrite(img->data, 1, sz, f);
    fclose(f);
    return wr == sz;
}

static void img_select_all(Image *img) {
    if (!img->loaded) { printf("No image loaded\n"); return; }
    img->x1 = 0; img->y1 = 0; img->x2 = img->w; img->y2 = img->h;
    printf("Selected ALL\n");
}

static void img_select_rect(Image *img, int x1, int y1, int x2, int y2) {
    if (!img->loaded) { printf("No image loaded\n"); return; }
    if (x1 > x2) { int t=x1; x1=x2; x2=t; }
    if (y1 > y2) { int t=y1; y1=y2; y2=t; }
    if (x1 < 0 || y1 < 0 || x2 > img->w || y2 > img->h || x1 == x2 || y1 == y2) {
        printf("Invalid set of coordinates\n");
        return;
    }
    img->x1 = x1; img->y1 = y1; img->x2 = x2; img->y2 = y2;
    printf("Selected %d %d %d %d\n", x1, y1, x2, y2);
}

static void img_crop(Image *img) {
    if (!img->loaded) { printf("No image loaded\n"); return; }
    int nw = img->x2 - img->x1;
    int nh = img->y2 - img->y1;
    size_t nsz = (size_t)nw * (size_t)nh * (size_t)img->ch;
    uint8_t *nd = (uint8_t*)malloc(nsz);
    if (!nd) { fprintf(stderr, "malloc failed\n"); return; }

    for (int y = 0; y < nh; y++) {
        const uint8_t *src = img->data + ((size_t)(img->y1 + y) * img->w + img->x1) * img->ch;
        uint8_t *dst = nd + ((size_t)y * nw) * img->ch;
        memcpy(dst, src, (size_t)nw * img->ch);
    }
    free(img->data);
    img->data = nd;
    img->w = nw; img->h = nh;
    img_select_all(img);
    printf("Image cropped\n");
}

// ======= OPENMP: Convolutie 3x3 paralela =======
static void apply_conv3x3(Image *img, const double K[3][3], const char *msg) {
    if (!img->loaded) { printf("No image loaded\n"); return; }

    int x1 = img->x1, y1 = img->y1, x2 = img->x2, y2 = img->y2;
    if (x1 == 0) x1++;
    if (y1 == 0) y1++;
    if (x2 == img->w) x2--;
    if (y2 == img->h) y2--;
    if (x2 - x1 <= 0 || y2 - y1 <= 0) { printf("%s done\n", msg); return; }

    size_t sz = (size_t)img->w * (size_t)img->h * (size_t)img->ch;
    if (!img_ensure_tmp(img, sz)) { fprintf(stderr, "malloc failed\n"); return; }

    memcpy(img->tmp, img->data, sz);

    // fiecare (y,x,c) scrie in tmp la index unic -> thread-safe
    #pragma omp parallel for collapse(2) schedule(static)
    for (int y = y1; y < y2; y++) {
        for (int x = x1; x < x2; x++) {
            size_t base = ((size_t)y * img->w + x) * (size_t)img->ch;
            for (int c = 0; c < img->ch; c++) {
                double sum = 0.0;
                for (int ky = -1; ky <= 1; ky++) {
                    for (int kx = -1; kx <= 1; kx++) {
                        size_t idx = ((size_t)(y + ky) * img->w + (x + kx)) * (size_t)img->ch + (size_t)c;
                        sum += K[ky + 1][kx + 1] * (double)img->data[idx];
                    }
                }
                img->tmp[base + (size_t)c] = clamp_u8_double(sum);
            }
        }
    }

    uint8_t *t = img->data; img->data = img->tmp; img->tmp = t;
    printf("%s done\n", msg);
}

// ======= OPENMP: Sobel paralel =======
static void apply_sobel(Image *img) {
    if (!img->loaded) { printf("No image loaded\n"); return; }

    int x1 = img->x1, y1 = img->y1, x2 = img->x2, y2 = img->y2;
    if (x1 == 0) x1++;
    if (y1 == 0) y1++;
    if (x2 == img->w) x2--;
    if (y2 == img->h) y2--;
    if (x2 - x1 <= 0 || y2 - y1 <= 0) { printf("APPLY SOBEL done\n"); return; }

    static const int Gx[3][3] = {{-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1}};
    static const int Gy[3][3] = {{ 1, 2, 1}, { 0, 0, 0}, {-1,-2,-1}};

    size_t sz = (size_t)img->w * (size_t)img->h * (size_t)img->ch;
    if (!img_ensure_tmp(img, sz)) { fprintf(stderr, "malloc failed\n"); return; }

    memcpy(img->tmp, img->data, sz);

    #pragma omp parallel for collapse(2) schedule(static)
    for (int y = y1; y < y2; y++) {
        for (int x = x1; x < x2; x++) {
            size_t base = ((size_t)y * img->w + x) * (size_t)img->ch;
            for (int c = 0; c < img->ch; c++) {
                int sx = 0, sy = 0;
                for (int ky = -1; ky <= 1; ky++) {
                    for (int kx = -1; kx <= 1; kx++) {
                        size_t idx = ((size_t)(y + ky) * img->w + (x + kx)) * (size_t)img->ch + (size_t)c;
                        int v = (int)img->data[idx];
                        sx += v * Gx[ky + 1][kx + 1];
                        sy += v * Gy[ky + 1][kx + 1];
                    }
                }
                double mag = sqrt((double)sx * (double)sx + (double)sy * (double)sy);
                img->tmp[base + (size_t)c] = clamp_u8_double(mag);
            }
        }
    }

    uint8_t *t = img->data; img->data = img->tmp; img->tmp = t;
    printf("APPLY SOBEL done\n");
}

static void histogram(const Image *img, int xstars, int bins) {
    if (!img->loaded) { printf("No image loaded\n"); return; }
    if (img->ch != 1) { printf("Black and white image needed\n"); return; }
    if (bins <= 0 || 256 % bins != 0) { printf("Invalid command\n"); return; }

    int fr[256] = {0};
    for (int i = 0; i < img->w * img->h; i++) fr[img->data[i]]++;

    int group = 256 / bins;
    int maxbin = 0;
    for (int b = 0; b < bins; b++) {
        int s = 0;
        for (int k = 0; k < group; k++) s += fr[b * group + k];
        if (s > maxbin) maxbin = s;
    }
    for (int b = 0; b < bins; b++) {
        int s = 0;
        for (int k = 0; k < group; k++) s += fr[b * group + k];
        int stars = (maxbin == 0) ? 0 : (int)lrint((double)s / (double)maxbin * (double)xstars);
        printf("%d\t|\t", stars);
        for (int i = 0; i < stars; i++) putchar('*');
        putchar('\n');
    }
}

// ======= OPENMP: Equalize cu histograma paralela (optional, dar util) =======
static void equalize(Image *img) {
    if (!img->loaded) { printf("No image loaded\n"); return; }
    if (img->ch != 1) { printf("Black and white image needed\n"); return; }

    size_t area = (size_t)img->w * (size_t)img->h;
    int fr[256] = {0};

    // histograma paralela: fiecare thread are fr_local[256], apoi reduce
    #pragma omp parallel
    {
        int local[256] = {0};

        #pragma omp for schedule(static)
        for (size_t i = 0; i < area; i++) {
            local[ img->data[i] ]++;
        }

        #pragma omp critical
        {
            for (int k = 0; k < 256; k++) fr[k] += local[k];
        }
    }

    int cdf[256];
    int run = 0;
    for (int i = 0; i < 256; i++) { run += fr[i]; cdf[i] = run; }

    #pragma omp parallel for schedule(static)
    for (size_t i = 0; i < area; i++) {
        int v = img->data[i];
        double nv = 255.0 * (double)cdf[v] / (double)area;
        img->data[i] = clamp_u8_double(nv);
    }

    printf("Equalize done\n");
}

static void bench(Image *img, int iters, const char *what) {
    if (!img->loaded) { printf("No image loaded\n"); return; }
    if (iters <= 0) { printf("Invalid command\n"); return; }

    static const double K_EDGE[3][3] = {
        {-1,-1,-1},{-1, 8,-1},{-1,-1,-1}
    };
    static const double K_SHARP[3][3] = {
        { 0,-1, 0},{-1, 5,-1},{ 0,-1, 0}
    };
    static const double K_BLUR[3][3] = {
        {1.0/9,1.0/9,1.0/9},{1.0/9,1.0/9,1.0/9},{1.0/9,1.0/9,1.0/9}
    };
    static const double K_GAUSS[3][3] = {
        {1.0/16,2.0/16,1.0/16},{2.0/16,4.0/16,2.0/16},{1.0/16,2.0/16,1.0/16}
    };

    double t0 = now_sec();
    for (int i = 0; i < iters; i++) {
        if (strcmp(what, "SOBEL") == 0) apply_sobel(img);
        else if (strcmp(what, "GAUSS_SOBEL") == 0) {
            apply_conv3x3(img, K_GAUSS, "APPLY GAUSSIAN_BLUR");
            apply_sobel(img);
        } else if (strcmp(what, "PIPE") == 0) {
            apply_conv3x3(img, K_GAUSS, "APPLY GAUSSIAN_BLUR");
            apply_sobel(img);
            apply_conv3x3(img, K_SHARP, "APPLY SHARPEN");
        } else if (strcmp(what, "EDGE") == 0) apply_conv3x3(img, K_EDGE, "APPLY EDGE");
        else if (strcmp(what, "SHARPEN") == 0) apply_conv3x3(img, K_SHARP, "APPLY SHARPEN");
        else if (strcmp(what, "BLUR") == 0) apply_conv3x3(img, K_BLUR, "APPLY BLUR");
        else if (strcmp(what, "GAUSSIAN_BLUR") == 0) apply_conv3x3(img, K_GAUSS, "APPLY GAUSSIAN_BLUR");
        else { printf("Invalid command\n"); return; }
    }
    double t1 = now_sec();
    printf("BENCH %s iters=%d time=%.6f sec\n", what, iters, t1 - t0);
}

int main(void) {
    Image img = (Image){0};
    char cmd[64];

    while (scanf("%63s", cmd) == 1) {
        if (strcmp(cmd, "LOAD") == 0) {
            char path[256];
            scanf("%255s", path);
            if (img_load_pnm(&img, path)) printf("Loaded %s\n", path);
            else printf("Failed to load %s\n", path);

        } else if (strcmp(cmd, "SAVE") == 0) {
            char path[256];
            scanf("%255s", path);
            if (!img.loaded) { printf("No image loaded\n"); continue; }
            if (img_save_pnm(&img, path)) printf("Saved %s\n", path);
            else printf("Failed to save %s\n", path);

        } else if (strcmp(cmd, "SELECT") == 0) {
            char next[64];
            scanf("%63s", next);
            if (strcmp(next, "ALL") == 0) img_select_all(&img);
            else {
                int x1 = atoi(next), y1, x2, y2;
                if (scanf("%d %d %d", &y1, &x2, &y2) != 3) { printf("Invalid command\n"); continue; }
                img_select_rect(&img, x1, y1, x2, y2);
            }

        } else if (strcmp(cmd, "CROP") == 0) {
            img_crop(&img);

        } else if (strcmp(cmd, "HISTOGRAM") == 0) {
            int xstars, bins;
            if (scanf("%d %d", &xstars, &bins) != 2) { printf("Invalid command\n"); continue; }
            histogram(&img, xstars, bins);

        } else if (strcmp(cmd, "EQUALIZE") == 0) {
            equalize(&img);

        } else if (strcmp(cmd, "APPLY") == 0) {
            char what[64];
            scanf("%63s", what);

            static const double K_EDGE[3][3] = { {-1,-1,-1},{-1, 8,-1},{-1,-1,-1} };
            static const double K_SHARP[3][3]= { { 0,-1, 0},{-1, 5,-1},{ 0,-1, 0} };
            static const double K_BLUR[3][3] = { {1.0/9,1.0/9,1.0/9},{1.0/9,1.0/9,1.0/9},{1.0/9,1.0/9,1.0/9} };
            static const double K_GAUSS[3][3]= { {1.0/16,2.0/16,1.0/16},{2.0/16,4.0/16,2.0/16},{1.0/16,2.0/16,1.0/16} };

            if (!img.loaded) { printf("No image loaded\n"); continue; }
            if (img.ch == 1) { printf("Easy, Charlie Chaplin\n"); continue; }

            if (strcmp(what, "EDGE") == 0) apply_conv3x3(&img, K_EDGE, "APPLY EDGE");
            else if (strcmp(what, "SHARPEN") == 0) apply_conv3x3(&img, K_SHARP, "APPLY SHARPEN");
            else if (strcmp(what, "BLUR") == 0) apply_conv3x3(&img, K_BLUR, "APPLY BLUR");
            else if (strcmp(what, "GAUSSIAN_BLUR") == 0) apply_conv3x3(&img, K_GAUSS, "APPLY GAUSSIAN_BLUR");
            else printf("APPLY parameter invalid\n");

        } else if (strcmp(cmd, "APPLY_SOBEL") == 0) {
            apply_sobel(&img);

        } else if (strcmp(cmd, "BENCH") == 0) {
            int iters; char what[64];
            if (scanf("%d %63s", &iters, what) != 2) { printf("Invalid command\n"); continue; }
            bench(&img, iters, what);

        } else if (strcmp(cmd, "EXIT") == 0) {
            break;

        } else {
            printf("Invalid command\n");
        }
    }

    img_free(&img);
    return 0;
}
