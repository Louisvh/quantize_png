#include "utils.h"

static Color** allocate_local_colcounters(int num_threads, size_t **local_ncolors, size_t **local_capacities);
static Color* merge_local_colcounters(Color **local_colcounters, size_t *local_ncolors, int num_threads, size_t *out_size);
static png_bytep pack_row(png_bytep unpacked, int width, int bit_depth);
static void png_error_fn(png_structp, png_const_charp msg);
static void png_warning_fn(png_structp, png_const_charp msg);
static int cmp_color(const void *a, const void *b);
static int color_dist(const Color *a, const Color *b);
static double min_dist(const Color *c, Color *sel, int n);
static int find_closest_color(int r, int g, int b, Color *palette, int pal_size);

void parse_arguments(int argc, char **argv, PaletteConfig *config, const char **in_path, const char **out_path) {
    int i = 1;
    int output_bit_depth_set = 0;
    while (i < argc && argv[i][0] == '-') {
        if (!strcmp(argv[i], "-b") && i + 1 < argc) {
            config->bit_depth = atoi(argv[++i]);
            if (config->bit_depth < 1 || config->bit_depth > 8) {
                fprintf(stderr, "expected logical bit depth [1, 8] (got: %d)\n", config->bit_depth);
                exit(1);
            }
            if (!output_bit_depth_set) {
                config->output_bit_depth = config->bit_depth;
            }
        } else if (!strcmp(argv[i], "-n") && i + 1 < argc) {
            config->max_colors = atoi(argv[++i]);
            if (config->max_colors < 1) {
                fprintf(stderr, "expected max_colors >= 1 (got %d)\n", config->max_colors);
                exit(1);
            }
        } else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            config->skip = atoi(argv[++i]);
            if (config->skip < 0) {
                fprintf(stderr, "expected skip >= 0 (got %d)\n", config->skip);
                exit(1);
            }
        } else if (!strcmp(argv[i], "-p") && i + 1 < argc) {
            config->preselect = atoi(argv[++i]);
            if (config->preselect < 0) {
                fprintf(stderr, "expected preselect >= 0 (got %d)\n", config->preselect);
                exit(1);
            }
        } else if (!strcmp(argv[i], "-db") && i + 1 < argc) {
            config->output_bit_depth = atoi(argv[++i]);
            if (config->output_bit_depth < 1 || config->output_bit_depth > 8) {
                fprintf(stderr, "expected output bit depth [1, 8] (got: %d)\n", config->output_bit_depth);
                exit(1);
            }
            output_bit_depth_set = 1;
        } else if (!strcmp(argv[i], "-v")) {
            config->verbose = 1;
        } else {
            break;
        }
        i++;
    }
    if (config->bit_depth > config->output_bit_depth) {
        fprintf(stderr, "Logical bit depth (%d) should not be greater than output bit depth (%d)\n", config->bit_depth, config->output_bit_depth);
        fprintf(stderr, "This would yield duplicate palette entries.\n");
        exit(1);
    }

    if (argc - i != 2) {
        fprintf(stderr,
            "usage: %s [options] input.png output.png\n"
             "options: \n"
             "\t -b bit_depth (logical, default: 8) \n \t -db output_bit_depth (default: =bit_depth) \n"
             "\t -n max_colors (default: 256) \n"
             "\t -s skip_slots; preceding slots filled with cyan (default: 0) \n"
             "\t -p preselect (slots purely selected by pixel frequency, default: 1)\n"
             "\t -v verbose (print selected color and cost information)\n", argv[0]);
        exit(1);
    }

    *in_path = argv[i];
    *out_path = argv[i + 1];
}

static int cmp_color(const void *a, const void *b) {
    return ((const Color*)b)->count - ((const Color*)a)->count;
}

void die(const char *msg) {
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

static double min_dist(const Color *c, Color *sel, int n) {
    double min = 1e30;
    for (int i = 0; i < n; i++) {
        double d = color_dist(c, &sel[i]);
        if (d < min) min = d;
    }
    return min;
}

static int find_closest_color(int r, int g, int b, Color *palette, int pal_size) {
    Color c = {r, g, b, 0};
    int best_idx = 0;
    int best_dist = color_dist(&c, &palette[0]);
    
    for (int i = 1; i < pal_size; i++) {
        int dist = color_dist(&c, &palette[i]);
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = i;
        }
    }
    return best_idx;
}

Color* collect_colors(png_bytep *rows, int w, int h, int channels, int bit_depth, size_t *out_size, int verbose) {
#ifdef _OPENMP
    int num_threads = omp_get_max_threads();
    if (verbose) fprintf(stderr, "using %d threads\n", num_threads);
#else
    int num_threads = 1;
#endif

    size_t *local_ncolors, *local_countercapacities;
    Color **local_colcounters = allocate_local_colcounters(num_threads, &local_ncolors, &local_countercapacities);

#ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic, DYNAMIC_CHUNK_SIZE)
#endif
    for (int y = 0; y < h; y++) {
#ifdef _OPENMP
        int thread_id = omp_get_thread_num();
#else
        int thread_id = 0;
#endif

        Color *colcounter = local_colcounters[thread_id];
        size_t *ncolors = &local_ncolors[thread_id];
        size_t *capacity = &local_countercapacities[thread_id];

        for (int x = 0; x < w; x++) {
            png_bytep px = &rows[y][x * channels];
            int r = px[0] >> (8 - bit_depth);
            int g = px[1] >> (8 - bit_depth);
            int b = px[2] >> (8 - bit_depth);

            size_t j;
            for (j = 0; j < *ncolors; j++) {
                if (colcounter[j].r == r && colcounter[j].g == g && colcounter[j].b == b) {
                    colcounter[j].count++;
                    break;
                }
            }
            
            if (j == *ncolors) {
                if (*ncolors == *capacity) {
                    *capacity *= 2;
                    colcounter = realloc(colcounter, *capacity * sizeof(Color));
                    if (!colcounter) die("realloc thread colcounter");
                    local_colcounters[thread_id] = colcounter;
                }
                colcounter[(*ncolors)++] = (Color){ r, g, b, 1 };
            }
        }
    }

    Color *merged_colcounter = merge_local_colcounters(local_colcounters, local_ncolors, num_threads, out_size);
    
    free(local_colcounters);
    free(local_ncolors);
    free(local_countercapacities);
    
    return merged_colcounter;
}

Color* build_palette(Color *all_colors, size_t num_colors, const PaletteConfig *config, int *out_pal_size) {
    qsort(all_colors, num_colors, sizeof(Color), cmp_color);

    int full_pal_len = config->max_colors > 0 ? config->max_colors : (int)num_colors + config->skip;
    int constructed_pal_len = config->max_colors > 0 ? config->max_colors - config->skip : (int)num_colors;
    if (constructed_pal_len < 1) die("palette length - skip must be >= 1");
    if (constructed_pal_len > (int)num_colors) constructed_pal_len = (int)num_colors;

    int preselect = config->preselect;
    if (preselect < 1) preselect = 1;
    if (preselect > constructed_pal_len) preselect = constructed_pal_len;

    Color *selected = malloc(full_pal_len * sizeof(Color));
    Color cyan = {0, 255 >> (8 - config->bit_depth), 255 >> (8 - config->bit_depth), 0};
    int selected_count = 0;

    int i;
    for (i = 0; i < config->skip; i++) {
        selected[selected_count++] = cyan;
        if (config->verbose)
            fprintf(stderr, " %3d: #%d,%d,%d\n", selected_count, cyan.r, cyan.g, cyan.b);
    }
    for (i = 0; i < preselect; i++) {
        selected[selected_count++] = all_colors[i];
        if (config->verbose)
            fprintf(stderr, " %3d: #%d,%d,%d (count: %d)\n",
            selected_count, all_colors[i].r, all_colors[i].g, all_colors[i].b, all_colors[i].count);
    }

    char *used = calloc(num_colors, 1);
    for (int i = 0; i < preselect; i++)
        used[i] = 1;

    while (selected_count < constructed_pal_len + config->skip) {
        int highest_cost = -1;
        int best_candidate_idx = -1;

        for (int i = 0; i < (int)num_colors; i++) {
            if (used[i]) continue;

            int md = min_dist(&all_colors[i], selected, selected_count);
            int cost = md * all_colors[i].count;

            if (cost > highest_cost) {
                highest_cost = cost;
                best_candidate_idx = i;
            }
        }

        if (best_candidate_idx < 0) break;

        used[best_candidate_idx] = 1;
        selected[selected_count++] = all_colors[best_candidate_idx];
        
        if (config->verbose) {
            fprintf(stderr, " %3d: #%d,%d,%d (count: %d, cost: %d)\n",
                    selected_count,
                    selected[selected_count - 1].r,
                    selected[selected_count - 1].g,
                    selected[selected_count - 1].b,
                    selected[selected_count - 1].count, highest_cost);
            fflush(stderr);
        }
    }

    free(used);
    *out_pal_size = selected_count;
    return selected;
}

static Color** allocate_local_colcounters(int num_threads, size_t **local_ncolors, size_t **local_capacities) {
    Color **tables = malloc(num_threads * sizeof(Color*));
    *local_ncolors = calloc(num_threads, sizeof(size_t));
    *local_capacities = malloc(num_threads * sizeof(size_t));
    
    for (int t = 0; t < num_threads; t++) {
        (*local_capacities)[t] = INITIAL_N_COLORS;
        tables[t] = malloc((*local_capacities)[t] * sizeof(Color));
        if (!tables[t]) die("malloc local colcounter");
    }
    return tables;
}

static Color* merge_local_colcounters(Color **local_colcounters, size_t *local_ncolors, int num_threads, size_t *out_size) {
    size_t capacity = INITIAL_N_COLORS;
    size_t ncolors = 0;
    Color *colcounter = malloc(capacity * sizeof(Color));
    if (!colcounter) die("malloc final colcounter");

    for (int t = 0; t < num_threads; t++) {
        for (size_t i = 0; i < local_ncolors[t]; i++) {
            Color *c = &local_colcounters[t][i];
            
            size_t j;
            for (j = 0; j < ncolors; j++) {
                if (colcounter[j].r == c->r && colcounter[j].g == c->g && colcounter[j].b == c->b) {
                    colcounter[j].count += c->count;
                    break;
                }
            }
            
            if (j == ncolors) {
                if (ncolors == capacity) {
                    capacity *= 2;
                    colcounter = realloc(colcounter, capacity * sizeof(Color));
                    if (!colcounter) die("realloc final colcounter");
                }
                colcounter[ncolors++] = *c;
            }
        }
        free(local_colcounters[t]);
    }
    
    *out_size = ncolors;
    return colcounter;
}

static png_bytep pack_row(png_bytep unpacked, int width, int bit_depth) {
    int pixels_per_byte = 8 / bit_depth;
    int packed_width = (width + pixels_per_byte - 1) / pixels_per_byte;
    
    png_bytep packed = malloc(packed_width);
    if (!packed) die("malloc packed_row");
    memset(packed, 0, packed_width);
    
    for (int x = 0; x < width; x++) {
        int byte_idx = x / pixels_per_byte;
        int bit_offset = (pixels_per_byte - 1 - (x % pixels_per_byte)) * bit_depth;
        packed[byte_idx] |= (unpacked[x] << bit_offset);
    }
    
    return packed;
}

void convert_palette_depth(Color *palette, int pal_size, int bit_depth, int output_bit_depth) {
    if (bit_depth == output_bit_depth) return;

    // Expand bits by replication; linearly distributes the difference between e.g. 240 (15*16) and 255
    for (int i = 0; i < pal_size; i++) {
        palette[i].r = (palette[i].r << (output_bit_depth - bit_depth)) | 
                       (palette[i].r >> (2 * bit_depth - output_bit_depth));
        palette[i].g = (palette[i].g << (output_bit_depth - bit_depth)) | 
                       (palette[i].g >> (2 * bit_depth - output_bit_depth));
        palette[i].b = (palette[i].b << (output_bit_depth - bit_depth)) | 
                       (palette[i].b >> (2 * bit_depth - output_bit_depth));
    }
}

void write_palette_png(const char *path, int w, int h, Color *palette, int pal_size, 
                               png_bytep *index_rows) {
    FILE *out_fp = fopen(path, "wb");
    if (!out_fp) {
        fprintf(stderr, "Failed to open output file: %s\n", path);
        die("open output");
    }

    png_structp out_png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                                   NULL, png_error_fn, png_warning_fn);
    png_infop out_info = png_create_info_struct(out_png);
    if (!out_png || !out_info) die("png write init");

    png_init_io(out_png, out_fp);
    
    int png_bit_depth = 8;
    if (pal_size <= 2) png_bit_depth = 1;
    else if (pal_size <= 4) png_bit_depth = 2;
    else if (pal_size <= 16) png_bit_depth = 4;
    
    png_set_IHDR(out_png, out_info, w, h, png_bit_depth, PNG_COLOR_TYPE_PALETTE,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);

    png_color *png_palette = malloc(pal_size * sizeof(png_color));
    if (!png_palette) die("malloc png_palette");
    
    for (int i = 0; i < pal_size; i++) {
        png_palette[i].red = palette[i].r;
        png_palette[i].green = palette[i].g;
        png_palette[i].blue = palette[i].b;
    }
    
    png_set_PLTE(out_png, out_info, png_palette, pal_size);
    png_write_info(out_png, out_info);
    free(png_palette);

    if (png_bit_depth < 8) {
        for (int y = 0; y < h; y++) {
            png_bytep packed = pack_row(index_rows[y], w, png_bit_depth);
            png_write_row(out_png, packed);
            free(packed);
        }
    } else {
        for (int y = 0; y < h; y++) {
            png_write_row(out_png, index_rows[y]);
        }
    }

    png_write_end(out_png, NULL);
    png_destroy_write_struct(&out_png, &out_info);
    fclose(out_fp);
}

png_bytep* quantize_image(png_bytep *rows, int w, int h, int channels, int bit_depth, Color *palette, int pal_size) {
    png_bytep *out_rows = malloc(h * sizeof(png_bytep));
    if (!out_rows) die("malloc out_rows");
    
    for (int y = 0; y < h; y++) {
        out_rows[y] = malloc(w);
        if (!out_rows[y]) die("malloc out_row");
    }

#ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic, DYNAMIC_CHUNK_SIZE)
#endif
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            png_bytep px = &rows[y][x * channels];
            int r = px[0] >> (8 - bit_depth);
            int g = px[1] >> (8 - bit_depth);
            int b = px[2] >> (8 - bit_depth);

            int idx = find_closest_color(r, g, b, palette, pal_size);
            out_rows[y][x] = idx;
        }
    }

    return out_rows;
}

void write_jasc_palette(const char *path, Color *palette, int pal_size, const PaletteConfig *config) {
    FILE *out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "Failed to write to file: %s\n", path);
        die("write results");
    }

    int full_pal_len = config->max_colors > 0 ? config->max_colors : pal_size + config->skip;
    int max_value = (1 << config->output_bit_depth) - 1;
    
    fprintf(out, "JASC-PAL\n0100\n%d\n", full_pal_len);

    int written = 0;
    for (int k = 0; k < pal_size; k++, written++)
        fprintf(out, "%d %d %d\n",
                palette[k].r, palette[k].g, palette[k].b);

    for (int k = written + config->skip; k < full_pal_len; k++)
        fprintf(out, "0 %d %d\n", max_value, max_value);

    fclose(out);
}

png_bytep* read_png_image(const char *path, int *w, int *h, int *channels) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Failed to open input file: %s\n", path);
        die("open input");
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, 
                                             NULL, png_error_fn, png_warning_fn);
    png_infop info = png_create_info_struct(png);
    if (!png || !info) die("png init");

    png_init_io(png, fp);
    png_read_info(png, info);

    *w = png_get_image_width(png, info);
    *h = png_get_image_height(png, info);
    
    if (*w * *h > 10 * 1024 * 1024) {
        fprintf(stderr, "provided image has %d pixels, this may take a while...\n", *w * *h);
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
    *channels = png_get_channels(png, info);

    size_t row_bytes = png_get_rowbytes(png, info);
    png_bytep *rows = malloc(*h * sizeof(png_bytep));
    if (!rows) die("malloc rows");
    
    for (int y = 0; y < *h; y++) {
        rows[y] = malloc(row_bytes);
        if (!rows[y]) die("malloc row");
        png_read_row(png, rows[y], NULL);
    }

    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    
    return rows;
}

static void png_error_fn(png_structp, png_const_charp msg) {
    fprintf(stderr, "PNG error: %s\n", msg);
    die("png error");
}

static void png_warning_fn(png_structp, png_const_charp msg) {
    fprintf(stderr, "PNG warning: %s\n", msg);
}

