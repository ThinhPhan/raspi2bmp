# raspi2bmp

Inspire from [raspi2png](https://github.com/AndrewFromMelbourne/raspi2png), I modified to capture Raspberry Pi screen without depends on png lib `libpng12-dev` by output direct from frame buffer to BMP format.

Utility to take a snapshot of the Raspberry Pi screen and save it as a BMP file.

    Usage: raspi2bmp [--output name] [--width <width>] [--height <height>] [--compression <level>] [--delay <delay>] [--display <number>] [--stdout] [--help]

    --output,-o - name of bmp file to create (default is snapshot.bmp)
    --height,-h - image height (default is screen height)
    --width,-w - image width (default is screen width)
    --compression,-c - PNG compression level (0 - 9)
    --delay,-d - delay in seconds (default 0)
    --display,-D - Raspberry Pi display number (default 0)
    --stdout,-s - write file to stdout
    --help,-H - print this usage information

Example:

```bash
raspi2bmp -w 1024 -h 768 -o snapshot.bmp -c 0
```

## Building

You will need to build on Raspbian. No depend library required.

Then just type 'make' in the raspi2bmp directory you cloned from github.

## Notice

The compression feature I haven't test yet. Just set `-c 0`.

## TODO

- [] Build shared library `bcm_host` along with binary file.
- [] Improve performance - reference [info-beamer/tools](https://github.com/info-beamer/tools)