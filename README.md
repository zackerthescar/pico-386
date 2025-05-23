# PICO-8 interpreter for IBM AT

In early development. Aiming to run *most* PICO-8 games on the IBM AT
with a 286 processor. Currently targeting a 286 with 512K + 2048K
memory and VGA graphics.

# Developing

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