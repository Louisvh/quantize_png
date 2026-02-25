# quantize_png & png_to_jasc

This is a very simple tool to quantize PNGs to a specific bit depth and palette size, with an option to skip a number
of palette entries. This can be useful for e.g. retro console programming, where the first palette entry may be hardwired to transparancy. If you found this repository looking for a general-purpose tool to quantize png images, you're likely better off with [pngquant](https://pngquant.org).

For images with more colours than fit the specified palette size, colors are selected based on a simple cost formula: `Frequency * min(dR + dG + dB + d(R-G) + d(G-B) + d(B-R))`, i.e., linear RGB distance to the closest color already in the palette plus a linear penalty on relative channel differences. Essentially, it prefers matching black to dark grey compared to matching black to other dark colours. The same cost function is used when assigning colours from a limited palette.

# Installation

This should build on basically any system with a functional c99 compiler. Simply `make` and copy the resulting binaries to anywhere in your path.

# Usage

`quantize_png [options] input.png output.png` 

or

`png_to_jasc [options] input.png output.pal` 

Options:

    -b bit_depth (logical, default: 8)
    -db output_bit_depth (default: =bit_depth)
    -n max_colors (default: 256)
    -s skip_slots; preceding slots filled with cyan (default: 0)
    -p preselect (slots purely selected by pixel frequency, default: 1)

e.g.

    > quantize_png -b 4 -db 8 -n 8 -s 1 example.png example_quant.png
    > png_to_jasc -b 4 -n 8 -s 1 example.png result.pal
    selected 2/8: #03,02,05 (count: 4148)
    selected 3/8: #05,09,08 (count: 1800, cost: 39600)
    selected 4/8: #12,05,03 (count: 688, cost: 15136)
    selected 5/8: #02,08,03 (count: 488, cost: 8296)
    selected 6/8: #02,02,03 (count: 1080, cost: 7560)
    selected 7/8: #08,02,03 (count: 484, cost: 7260)
    selected 8/8: #03,05,07 (count: 516, cost: 5676)

![](example.png) __→__ ![](example_quant.png)

    > cat result.pal
    JASC-PAL
    0100
    8
    0 15 15
    3 2 5
    5 9 8
    12 5 3
    2 8 3
    2 2 3
    8 2 3
    3 5 7





