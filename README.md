# toaru\_jpeg

`toaru_jpeg` is a standalone version of the ToaruOS JPEG decoder, which is itself a rewrite of Raul Aguaviva's Python "micro JPEG visualizer" in C.

This is a *minimal, incomplete* implementation of the JPEG standard - enough for basic images, but not for any advanced features. Mostly that means non-progressive, 24-bit RGB color images only.

## Usage

This tool produces a TGA image from the input JPG, so you can use it like this to verify it is working for your image:

    make
    ./toaru_jpeg ~/path/to/some/simple.jpg
    eog out.tga

## License

The Toaru JPEG decoder is itself released under the terms of the [NCSA / University of Illinois License](https://github.com/klange/toaruos/blob/master/LICENSE).

Raul Aguaviva's Python "micro JPEG visualizer" carried an MIT license.
