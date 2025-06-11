# PICO-8 interpreter for 386 PC systems and up!

In early development. Aiming to run *most* PICO-8 games on the IBM
compatibles with a 386+ processor. Currently targeting a 386 with 
_ample_ memory and VGA graphics.

Currently aiming to run cartridges, editor and other tooling later.

# Developing

To build, you first need to acquire the sources for zlib-1.2.11
[here](https://sourceforge.net/projects/libpng/files/zlib/1.2.11/zlib-1.2.11.tar.xz/download) and extract such that the files are in the 
`zlib/` folder, which is removed from the tree for convenience sake.

Then, simply `make` on a Linux machine with Open Watcom V2 installed.

(psst: there's a flake for development if you use Nix!)

On Linux, get DosBox-X and `socat`.

Start socat with this command

```
socat -d -d PTY,link=/tmp/com1,raw,echo=0 PTY,link=/tmp/com2,raw,echo=0
```

on DosBox-X, add
```
[serial]
serial2 = file file:/dev/pts/2
```

then on a new terminal,

```
cat /dev/pts/3
```

then start developing. Use `debug_serial_print()` to print debug info.

# Licensing

This application is licensed as MIT, and uses `zlib` which is 2-clause
BSD. 
