#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>

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
    int preselect_count = -1;

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
            preselect_count = atoi(argv[++i]);
            if (preselect_count < 0) {
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
            "usage: %s [-b bits (default: 8)] [-n max_entries (default: 256, 0=inf)] [-s skip (for >0, cyan entries are pre-pended)] [-p preselected_colours (default: 2/3 of palette)] input.png output.pal\n",
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

    png_bytep row = malloc(png_get_rowbytes(png, info));
    if (!row) die("malloc row");

    size_t cap = 1024, size = 0;
    Color *table = malloc(cap * sizeof(Color));
    if (!table) die("malloc table");

    fprintf(stderr, "processing %d pixels\n", w * h);
    if (w * h > 1024 * 1024) {
        fprintf(stderr, "Progress: --------------------");
        fflush(stderr);
    }
    for (int y = 0; y < h; y++) {
        if (h > 1024 && (y+1) % (h / 20) == 0) {
            int progress = ((y+1) * 20) / h;
            fprintf(stderr, "\rProgress: ");
            for (int i = 0; i < 20; i++) {
                fprintf(stderr, i < progress ? "+" : "-");
            }
            fflush(stderr);
        }
        png_read_row(png, row, NULL);
        for (int x = 0; x < w; x++) {
            png_bytep px = &row[x * channels];
            int r = px[0] >> (8 - bit_depth);
            int g = px[1] >> (8 - bit_depth);
            int b = px[2] >> (8 - bit_depth);

            size_t j;
            for (j = 0; j < size; j++) {
                if (table[j].r == r && table[j].g == g && table[j].b == b) {
                    table[j].count++;
                    break;
                }
            }
            if (j == size) {
                if (size == cap) {
                    cap *= 2;
                    table = realloc(table, cap * sizeof(Color));
                    if (!table) die("realloc table");
                }
                table[size++] = (Color){ r, g, b, 1 };
            }
        }
    }
    if (h > 1024) fprintf(stderr, "\n");

    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    free(row);

    qsort(table, size, sizeof(Color), cmp_color);

    int full_pal_len = max_colors > 0 ? max_colors : (int)size + skip;
    int constructed_pal_len = max_colors > 0 ? max_colors - skip : (int)size;
    if (constructed_pal_len < 1) die("pal length - skip must be >= 1");
    if (constructed_pal_len > (int)size) constructed_pal_len = (int)size;

    int preselect = preselect_count >= 0 ? preselect_count : (2 * constructed_pal_len) / 3;
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
