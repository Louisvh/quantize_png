#ifndef PALETTE_GEN_H
#define PALETTE_GEN_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#define INITIAL_N_COLORS 2048
#define DYNAMIC_CHUNK_SIZE 16

typedef struct {
    int r, g, b;
    int count;
} Color;

typedef struct {
    int bit_depth;
    int output_bit_depth;
    int max_colors;
    int skip;
    int preselect;
} PaletteConfig;

void die(const char *msg);
void parse_arguments(int argc, char **argv, PaletteConfig *config, const char **in_path, const char **out_path);


png_bytep* read_png_image(const char *path, int *w, int *h, int *channels);

Color* collect_colors(png_bytep *rows, int w, int h, int channels, int bit_depth, size_t *out_size);
Color* build_palette(Color *all_colors, size_t num_colors, const PaletteConfig *config, int *out_pal_size);

png_bytep* quantize_image(png_bytep *rows, int w, int h, int channels, int bit_depth, Color *palette, int pal_size);
void write_palette_png(const char *path, int w, int h, Color *palette, int pal_size, png_bytep *index_rows);
void write_jasc_palette(const char *path, Color *palette, int pal_size, const PaletteConfig *config);
void convert_palette_depth(Color *palette, int pal_size, int bit_depth, int output_bit_depth);

#endif // PALETTE_GEN_H
