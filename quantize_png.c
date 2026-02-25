#include "utils.h"

int main(int argc, char **argv) {
    PaletteConfig config = {
        .bit_depth = 8,
        .output_bit_depth = 8,
        .max_colors = 256,
        .skip = 0,
        .preselect = 1,
        .verbose = 0
    };
    
    const char *in_path, *out_path;
    parse_arguments(argc, argv, &config, &in_path, &out_path);

    int w, h, channels;
    png_bytep *rows = read_png_image(in_path, &w, &h, &channels);

    size_t num_colors;
    Color *all_colors = collect_colors(rows, w, h, channels, config.bit_depth, &num_colors, config.verbose);

    int palette_size;
    Color *palette = build_palette(all_colors, num_colors, &config, &palette_size);
    free(all_colors);

    png_bytep *index_rows = quantize_image(rows, w, h, channels, config.bit_depth, palette, palette_size);
    
    for (int y = 0;y < h; y++) {
        free(rows[y]);
    }
    free(rows);

    convert_palette_depth(palette, palette_size, config.bit_depth, config.output_bit_depth);
    write_palette_png(out_path, w, h, palette, palette_size, index_rows);
    
    free(palette);
    for (int y = 0; y < h; y++) {
        free(index_rows[y]);
    }
    free(index_rows);

    return 0;
}
