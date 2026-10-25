# rion

Minimalist SDL2 canvas runtime for Chloride applications.

## Build dependencies

- C99 compiler (gcc or clang)
- make
- SDL2 headers

On Debian/Ubuntu:
`sudo apt install build-essential libsdl2-dev`

## Runtime dependencies

- SDL2

On Debian/Ubuntu:
`sudo apt install libsdl2`

## Building & Installation

make

sudo make install

## Usage

rion [address]

example: rion chloride.chlorine.me:49130

## Credits & Licensing

- wasm3 (https://github.com/wasm3/wasm3) - Fast WebAssembly interpreter engine (MIT License). Embedded in wasm3/.
- SDL2 (https://www.libsdl.org/) - Simple DirectMedia Layer (zlib License).
