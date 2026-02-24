#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#ifdef _OPENMP
#include <omp.h>
#endif

typedef struct {
    int r, g, b;
    int count;
} Color;

static int cmp_color(const void *a, const void *b) {
    return ((const Color*)b)->count - ((const Color*)a)->count;
}

static void die(const char *msg) {
    perror(msg);
    exit(1);
}

static int color_dist(const Color *a, const Color *b) {
    int dr = abs(a->r - b->r);
    int dg = abs(a->g - b->g);
    int db = abs(a->b - b->b);
    return dr + dg + db
         + abs(dr - dg)
         + abs(dr - db)
         + abs(dg - db);
}

// iterate over already selected colors and return the min distance to c
static double min_dist(const Color *c, Color *sel, int n) {
    double min = 1e30;
    for (int i = 0; i < n; i++) {
        double d = color_dist(c, &sel[i]);
        if (d < min) min = d;
    }
    return min;
}

static void png_error_fn(png_structp, png_const_charp error_msg) {
    fprintf(stderr, "PNG error: %s\n", error_msg);
    die("png error");
}

static void png_warning_fn(png_structp, png_const_charp warning_msg) {
    fprintf(stderr, "PNG warning: %s\n", warning_msg);
}

int main(int argc, char **argv) {
    int bit_depth = 8;
    int max_colors = 256;
    int skip = 0;
    int preselect = 1;

    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        if (!strcmp(argv[i], "-b") && i + 1 < argc) {
            bit_depth = atoi(argv[++i]);
            if (bit_depth < 1 || bit_depth > 8) {
                fprintf(stderr, "expected bit depth [1, 8]\n");
                return 1;
            }
        } else if (!strcmp(argv[i], "-n") && i + 1 < argc) {
            max_colors = atoi(argv[++i]);
            if (max_colors < 0) {
                fprintf(stderr, "expected max_entreis >= 0\n");
                return 1;
            }
        } else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            skip = atoi(argv[++i]);
            if (skip < 0) {
                fprintf(stderr, "expected skip >= 0\n");
                return 1;
            }
        } else if (!strcmp(argv[i], "-p") && i + 1 < argc) {
            preselect = atoi(argv[++i]);
            if (preselect < 0) {
                fprintf(stderr, "expected preselect >= 0\n");
                return 1;
            }
        } else {
            break;
        }
        i++;
    }

    if (argc - i != 2) {
        fprintf(stderr,
            "usage: %s [-b bits (default: 8)] [-n max_entries (default: 256, 0=inf)] [-s skip (for >0, cyan entries are pre-pended)] [-p preselected_colours (default: 1)] input.png output.pal\n",
            argv[0]);
        return 1;
    }

    const char *in_path  = argv[i];
    const char *out_path = argv[i + 1];

    FILE *fp = fopen(in_path, "rb");
    if (!fp) {
        fprintf(stderr, "Failed to open input file: %s\n", in_path);
        die("open input");
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, 
                                             NULL, png_error_fn, png_warning_fn);
    png_infop info  = png_create_info_struct(png);
    if (!png || !info) die("png init");

    png_init_io(png, fp);
    png_read_info(png, info);

    int w = png_get_image_width(png, info);
    int h = png_get_image_height(png, info);
    if (w * h > 10 * 1024 * 1024) {
        fprintf(stderr, "provided image has %d pixels, this may take a while...\n", w * h);
    }
    int color = png_get_color_type(png, info);
    int depth = png_get_bit_depth(png, info);

    if (depth == 16) png_set_strip_16(png);
    if (color == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);

    png_read_update_info(png, info);
    int channels = png_get_channels(png, info);

    size_t row_bytes = png_get_rowbytes(png, info);
    png_bytep *rows = malloc(h * sizeof(png_bytep));
    if (!rows) die("malloc rows");
    for (int y = 0; y < h; y++) {
        rows[y] = malloc(row_bytes);
        if (!rows[y]) die("malloc row");
        png_read_row(png, rows[y], NULL);
    }

    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

#ifdef _OPENMP
    int num_threads = omp_get_max_threads();
    fprintf(stderr, "using %d threads\n", num_threads);
#else
    int num_threads = 1;
#endif

    Color **thread_tables = malloc(num_threads * sizeof(Color*));
    size_t *thread_sizes = calloc(num_threads, sizeof(size_t));
    size_t *thread_caps = malloc(num_threads * sizeof(size_t));
    
    for (int t = 0; t < num_threads; t++) {
        thread_caps[t] = 1024;
        thread_tables[t] = malloc(thread_caps[t] * sizeof(Color));
        if (!thread_tables[t]) die("malloc thread table");
    }

#ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic, 16)
#endif
    for (int y = 0; y < h; y++) {
#ifdef _OPENMP
        int tid = omp_get_thread_num();
#else
        int tid = 0;
#endif

        Color *table = thread_tables[tid];
        size_t *size = &thread_sizes[tid];
        size_t *cap = &thread_caps[tid];

        for (int x = 0; x < w; x++) {
            png_bytep px = &rows[y][x * channels];
            int r = px[0] >> (8 - bit_depth);
            int g = px[1] >> (8 - bit_depth);
            int b = px[2] >> (8 - bit_depth);

            size_t j;
            for (j = 0; j < *size; j++) {
                if (table[j].r == r && table[j].g == g && table[j].b == b) {
                    table[j].count++;
                    break;
                }
            }
            
            if (j == *size) {
                if (*size == *cap) {
                    *cap *= 2;
                    table = realloc(table, *cap * sizeof(Color));
                    if (!table) die("realloc thread table");
                    thread_tables[tid] = table;
                }
                table[(*size)++] = (Color){ r, g, b, 1 };
            }
        }
    }

    for (int y = 0; y < h; y++) {
        free(rows[y]);
    }
    free(rows);

    size_t cap = 8192, size = 0;
    Color *table = malloc(cap * sizeof(Color));
    if (!table) die("malloc final table");

    for (int t = 0; t < num_threads; t++) {
        for (size_t i = 0; i < thread_sizes[t]; i++) {
            Color *c = &thread_tables[t][i];
            
            size_t j;
            for (j = 0; j < size; j++) {
                if (table[j].r == c->r && table[j].g == c->g && table[j].b == c->b) {
                    table[j].count += c->count;
                    break;
                }
            }
            
            if (j == size) {
                if (size == cap) {
                    cap *= 2;
                    table = realloc(table, cap * sizeof(Color));
                    if (!table) die("realloc final table");
                }
                table[size++] = *c;
            }
        }
        free(thread_tables[t]);
    }
    
    free(thread_tables);
    free(thread_sizes);
    free(thread_caps);

    qsort(table, size, sizeof(Color), cmp_color);

    int full_pal_len = max_colors > 0 ? max_colors : (int)size + skip;
    int constructed_pal_len = max_colors > 0 ? max_colors - skip : (int)size;
    if (constructed_pal_len < 1) die("pal length - skip must be >= 1");
    if (constructed_pal_len > (int)size) constructed_pal_len = (int)size;

    if (preselect < 1) preselect = 1;
    if (preselect > constructed_pal_len) preselect = constructed_pal_len;

    Color *selected = malloc(constructed_pal_len * sizeof(Color));
    int sel_count = 0;

    for (int i = 0; i < preselect; i++)
        selected[sel_count++] = table[i];

    char *used = calloc(size, 1);
    for (int i = 0; i < preselect; i++)
        used[i] = 1;

    while (sel_count < constructed_pal_len) {
        int highest_cost = -1;
        int best_candidate_idx = -1;

        for (int i = 0; i < (int)size; i++) {
            if (used[i]) continue;

            int md = min_dist(&table[i], selected, sel_count);
            int cost = md * table[i].count;

            if (cost > highest_cost) {
                highest_cost = cost;
                best_candidate_idx = i;
            }
        }

        if (best_candidate_idx < 0) break;

        used[best_candidate_idx] = 1;
        selected[sel_count++] = table[best_candidate_idx];
        
        fprintf(stderr, bit_depth < 7? "selected %d/%d: #%02d,%02d,%02d (count: %d, cost: %d)\n" : "selected %d/%d: #%03d,%03d,%03d (count: %d, cost: %d)\n",
                sel_count + skip, full_pal_len,
                selected[sel_count - 1].r, selected[sel_count - 1].g, selected[sel_count - 1].b,
                selected[sel_count - 1].count, highest_cost);
        fflush(stderr);
    }

    free(table);
    free(used);

    FILE *out = fopen(out_path, "w");

    if (!out) {
        fprintf(stderr, "Failed to write to file: %s\n", out_path);
        die("write results");
    }

    int maxv = (1 << bit_depth) - 1;
    fprintf(out, "JASC-PAL\n0100\n%d\n", full_pal_len);

    for (int k = 0; k < skip && k < full_pal_len; k++)
        fprintf(out, "0 %d %d\n", maxv, maxv);

    int written = 0;
    for (int k = 0; k < constructed_pal_len && k < (int)size; k++, written++)
        fprintf(out, "%d %d %d\n",
                selected[k].r, selected[k].g, selected[k].b);

    // pad to pal_len with dummy entries
    for (int k = written + skip; k < full_pal_len; k++)
        fprintf(out, "0 %d %d\n", maxv, maxv);

    fclose(out);
    free(selected);
    return 0;
}
