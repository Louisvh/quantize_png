# quantize_png & png_to_jasc

![](example.png) __→__ ![](example_quant.png)

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
    -v (print selected color and cost information)

e.g.

    > png_to_jasc -v -b 4 -n 8 -s 1 example.png result.pal
    using 12 threads
        1: #0,15,15
        2: #0,0,0 (count: 6951)
        3: #8,7,6 (count: 111, cost: 2775)
        4: #4,4,3 (count: 91, cost: 1092)
        5: #13,12,11 (count: 61, cost: 915)
        6: #10,10,9 (count: 81, cost: 729)
        7: #10,8,7 (count: 81, cost: 486)
        8: #8,8,8 (count: 60, cost: 420)

or 

    > quantize_png -b 5 -db 8 -n 16 -s 1 example.png example_quant.png

![](example.png) __→__ ![](example_quant.png)


# Speed

The code here isn't very efficient, but thankfully, inefficient C code is still pretty fast. On a i5-1335U laptop, it'll churn through a few million pixels per second; processing UHD images takes a few seconds:

    > time-wall quantize_png -b 5 -db 8 -n 16 -s 1 esa_jupiter_large.png result.png

    provided image has 80000000 pixels, this may take a while...
    
    Elapsed time (min:sec) 0:05.97

    > identify esa_jupiter_large.png result.png 
    esa_jupiter_large.png PNG 10000x8000 8-bit sRGB 16.7784MiB
    result.png PNG 10000x8000 8-bit sRGB 16c 1.22251MiB

    
By default, the code tries to distribute the processing across the available cores. You can disable that by setting OMP_NUM_THREADS to 1.