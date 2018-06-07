#define _GNU_SOURCE

#include <getopt.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#include "bcm_host.h"

//-----------------------------------------------------------------------

#ifndef ALIGN_TO_16
#define ALIGN_TO_16(x) ((x + 15) & ~15)
#endif

//-----------------------------------------------------------------------

#define DEFAULT_DELAY 0
#define DEFAULT_DISPLAY_NUMBER 0
#define DEFAULT_NAME "snapshot.bmp"

//-----------------------------------------------------------------------

const char *program = NULL;

//-----------------------------------------------------------------------

void usage(void)
{
    fprintf(stderr, "Usage: %s [--output name]", program);
    fprintf(stderr, " [--width <width>] [--height <height>]");
    fprintf(stderr, " [--compression <level>]");
    fprintf(stderr, " [--delay <delay>] [--display <number>]");
    fprintf(stderr, " [--stdout] [--help]\n");

    fprintf(stderr, "\n");

    fprintf(stderr, "    --output,-o - name of png file to create ");
    fprintf(stderr, "(default is %s)\n", DEFAULT_NAME);

    fprintf(stderr, "    --height,-h - image height ");
    fprintf(stderr, "(default is screen height)\n");

    fprintf(stderr, "    --width,-w - image width ");
    fprintf(stderr, "(default is screen width)\n");

    fprintf(stderr, "    --compression,-c - PNG compression level ");
    fprintf(stderr, "(0 - 9)\n");

    fprintf(stderr, "    --delay,-d - delay in seconds ");
    fprintf(stderr, "(default %d)\n", DEFAULT_DELAY);

    fprintf(stderr, "    --display,-D - Raspberry Pi display number ");
    fprintf(stderr, "(default %d)\n", DEFAULT_DISPLAY_NUMBER);

    fprintf(stderr, "    --stdout,-s - write file to stdout\n");

    fprintf(stderr, "    --help,-H - print this usage information\n");

    fprintf(stderr, "\n");
}

//-----------------------------------------------------------------------

int main(int argc, char *argv[])
{
    int opt                   = 0;
    bool writeToStdout        = false;
    char *fileName            = DEFAULT_NAME;
    int32_t requestedWidth    = 0;
    int32_t requestedHeight   = 0;
    uint32_t displayNumber    = DEFAULT_DISPLAY_NUMBER;
    int compression           = Z_DEFAULT_COMPRESSION;
    int delay                 = DEFAULT_DELAY;

    VC_IMAGE_TYPE_T imageType = VC_IMAGE_RGBA32;
    int8_t dmxBytesPerPixel   = 4;

    int result = 0;

    program = basename(argv[0]);

    //-------------------------------------------------------------------
    // Handle inputs

    char *sopts = "c:d:D:Hh:o:w:s";

    struct option lopts[] =
        {
            {"compression", required_argument, NULL, 'c'},
            {"delay", required_argument, NULL, 'd'},
            {"display", required_argument, NULL, 'D'},
            {"height", required_argument, NULL, 'h'},
            {"help", no_argument, NULL, 'H'},
            {"output", required_argument, NULL, 'o'},
            {"width", required_argument, NULL, 'w'},
            {"stdout", no_argument, NULL, 's'},
            {NULL, no_argument, NULL, 0}};

    while ((opt = getopt_long(argc, argv, sopts, lopts, NULL)) != -1)
    {
        switch (opt)
        {
        case 'c':

            compression = atoi(optarg);

            if ((compression < 0) || (compression > 9))
            {
                compression = Z_DEFAULT_COMPRESSION;
            }

            break;

        case 'd':

            delay = atoi(optarg);
            break;

        case 'D':

            displayNumber = atoi(optarg);
            break;

        case 'h':

            requestedHeight = atoi(optarg);
            break;

        case 'o':

            fileName = optarg;
            break;

        case 'w':

            requestedWidth = atoi(optarg);
            break;

        case 's':

            writeToStdout = true;
            break;

        case 'H':
        default:

            usage();

            if (opt == 'H')
            {
                exit(EXIT_SUCCESS);
            }
            else
            {
                exit(EXIT_FAILURE);
            }

            break;
        }
    }

    //-------------------------------------------------------------------

    bcm_host_init();

    //-------------------------------------------------------------------
    //
    // When the display is rotate (either 90 or 270 degrees) we need to
    // swap the width and height of the snapshot
    //

    char response[1024];
    int displayRotated = 0;

    if (vc_gencmd(response, sizeof(response), "get_config int") == 0)
    {
        vc_gencmd_number_property(response,
                                  "display_rotate",
                                  &displayRotated);
    }

    //-------------------------------------------------------------------

    if (delay)
    {
        sleep(delay);
    }

    //-------------------------------------------------------------------

    DISPMANX_DISPLAY_HANDLE_T displayHandle = vc_dispmanx_display_open(displayNumber);

    if (displayHandle == 0)
    {
        fprintf(stderr,
                "%s: unable to open display %d\n",
                program,
                displayNumber);

        exit(EXIT_FAILURE);
    }

    DISPMANX_MODEINFO_T modeInfo;
    result = vc_dispmanx_display_get_info(displayHandle, &modeInfo);

    if (result != 0)
    {
        fprintf(stderr, "%s: unable to get display information\n", program);
        exit(EXIT_FAILURE);
    }

    int32_t bmpWidth = modeInfo.width;
    int32_t pngHeight = modeInfo.height;

    if (requestedWidth > 0)
    {
        bmpWidth = requestedWidth;

        if (requestedHeight == 0)
        {
            double numerator = modeInfo.height * requestedWidth;
            double denominator = modeInfo.width;

            pngHeight = (int32_t)ceil(numerator / denominator);
        }
    }

    if (requestedHeight > 0)
    {
        pngHeight = requestedHeight;

        if (requestedWidth == 0)
        {
            double numerator = modeInfo.width * requestedHeight;
            double denominator = modeInfo.height;

            bmpWidth = (int32_t)ceil(numerator / denominator);
        }
    }

    //-------------------------------------------------------------------
    // only need to check low bit of displayRotated (value of 1 or 3).
    // If the display is rotated either 90 or 270 degrees (value 1 or 3)
    // the width and height need to be transposed.

    int32_t dmxWidth = bmpWidth;
    int32_t dmxHeight = pngHeight;

    if (displayRotated & 1)
    {
        dmxWidth = pngHeight;
        dmxHeight = bmpWidth;
    }

    int32_t dmxPitch = dmxBytesPerPixel * ALIGN_TO_16(dmxWidth);

    void *dmxImagePtr = malloc(dmxPitch * dmxHeight);

    if (dmxImagePtr == NULL)
    {
        fprintf(stderr, "%s: unable to allocated image buffer\n", program);
        exit(EXIT_FAILURE);
    }

    //-------------------------------------------------------------------

    uint32_t vcImagePtr = 0;
    DISPMANX_RESOURCE_HANDLE_T resourceHandle;
    resourceHandle = vc_dispmanx_resource_create(imageType,
                                                 dmxWidth,
                                                 dmxHeight,
                                                 &vcImagePtr);

    result = vc_dispmanx_snapshot(displayHandle,
                                  resourceHandle,
                                  DISPMANX_NO_ROTATE);

    if (result != 0)
    {
        vc_dispmanx_resource_delete(resourceHandle);
        vc_dispmanx_display_close(displayHandle);

        fprintf(stderr, "%s: vc_dispmanx_snapshot() failed\n", program);
        exit(EXIT_FAILURE);
    }

    VC_RECT_T rect;
    result = vc_dispmanx_rect_set(&rect, 0, 0, dmxWidth, dmxHeight);

    if (result != 0)
    {
        vc_dispmanx_resource_delete(resourceHandle);
        vc_dispmanx_display_close(displayHandle);

        fprintf(stderr, "%s: vc_dispmanx_rect_set() failed\n", program);
        exit(EXIT_FAILURE);
    }

    result = vc_dispmanx_resource_read_data(resourceHandle,
                                            &rect,
                                            dmxImagePtr,
                                            dmxPitch);

    if (result != 0)
    {
        vc_dispmanx_resource_delete(resourceHandle);
        vc_dispmanx_display_close(displayHandle);

        fprintf(stderr,
                "%s: vc_dispmanx_resource_read_data() failed\n",
                program);

        exit(EXIT_FAILURE);
    }

    vc_dispmanx_resource_delete(resourceHandle);
    vc_dispmanx_display_close(displayHandle);

    //-------------------------------------------------------------------
    // Convert from RGBA (32 bit) to RGB (24 bit)

    int8_t bytesPerPixel    = 3; /// red, green, blue
    int32_t pngPitch        = bytesPerPixel * bmpWidth;
    unsigned char *imagePtr = malloc(pngPitch * pngHeight);

    int32_t j = 0;
    for (j = 0; j < pngHeight; j++)
    {
        int32_t dmxXoffset = 0;
        int32_t dmxYoffset = 0;

        switch (displayRotated & 3)
        {
        case 0: // 0 degrees

            if (displayRotated & 0x20000) // flip vertical
            {
                dmxYoffset = (dmxHeight - j - 1) * dmxPitch;
            }
            else
            {
                dmxYoffset = j * dmxPitch;
            }

            break;

        case 1: // 90 degrees

            if (displayRotated & 0x20000) // flip vertical
            {
                dmxXoffset = j * dmxBytesPerPixel;
            }
            else
            {
                dmxXoffset = (dmxWidth - j - 1) * dmxBytesPerPixel;
            }

            break;

        case 2: // 180 degrees

            if (displayRotated & 0x20000) // flip vertical
            {
                dmxYoffset = j * dmxPitch;
            }
            else
            {
                dmxYoffset = (dmxHeight - j - 1) * dmxPitch;
            }

            break;

        case 3: // 270 degrees

            if (displayRotated & 0x20000) // flip vertical
            {
                dmxXoffset = (dmxWidth - j - 1) * dmxBytesPerPixel;
            }
            else
            {
                dmxXoffset = j * dmxBytesPerPixel;
            }

            break;
        }

        int32_t i = 0;
        for (i = 0; i < bmpWidth; i++)
        {
            uint8_t *pngPixelPtr = imagePtr + (i * bytesPerPixel) + (j * pngPitch);

            switch (displayRotated & 3)
            {
            case 0: // 0 degrees

                if (displayRotated & 0x10000) // flip horizontal
                {
                    dmxXoffset = (dmxWidth - i - 1) * dmxBytesPerPixel;
                }
                else
                {
                    dmxXoffset = i * dmxBytesPerPixel;
                }

                break;

            case 1: // 90 degrees

                if (displayRotated & 0x10000) // flip horizontal
                {
                    dmxYoffset = (dmxHeight - i - 1) * dmxPitch;
                }
                else
                {
                    dmxYoffset = i * dmxPitch;
                }

                break;

            case 2: // 180 degrees

                if (displayRotated & 0x10000) // flip horizontal
                {
                    dmxXoffset = i * dmxBytesPerPixel;
                }
                else
                {
                    dmxXoffset = (dmxWidth - i - 1) * dmxBytesPerPixel;
                }

                break;

            case 3: // 270 degrees

                if (displayRotated & 0x10000) // flip horizontal
                {
                    dmxYoffset = i * dmxPitch;
                }
                else
                {
                    dmxYoffset = (dmxHeight - i - 1) * dmxPitch;
                }

                break;
            }

            uint8_t *dmxPixelPtr = dmxImagePtr + dmxXoffset + dmxYoffset;

            memcpy(pngPixelPtr, dmxPixelPtr, 3);
        }
    }

    free(dmxImagePtr);
    dmxImagePtr = NULL;

    //-------------------------------------------------------------------

    FILE *fileBMP = NULL;

    if (writeToStdout)
    {
        fileBMP = stdout;
    }
    else
    {
        fileBMP = fopen(fileName, "wb");

        if (fileBMP == NULL)
        {
            fprintf(stderr,
                    "%s: unable to create %s - %s\n",
                    program,
                    fileName,
                    strerror(errno));

            exit(EXIT_FAILURE);
        }
        // else
        // {
            // Save as Raw
            // fwrite(imagePtr, 3, pngPitch * pngHeight, fileBMP);
        // }
    }
    //-------------------------------------------------------------------
    // Convert to BMP file
    // mimeType = "image/bmp";

    unsigned char *img = NULL;
    int filesize = 54 + 3 * bmpWidth * pngHeight;

    int pixelLength = bmpWidth * pngHeight;

    unsigned char r, g, b = 0;

    // Swap data in RGBQUAD structure 
    for (int i =0; i < pixelLength; i++) {
        r = *(imagePtr + 3*i + 0); //
        g = *(imagePtr + 3*i + 1);
        b = *(imagePtr + 3*i + 2);
        imagePtr[3*i + 0] = b; // blue
        imagePtr[3*i + 1] = g;
        imagePtr[3*i + 2] = r;
    }

    unsigned char bmpFileHeader[14] = {
        'B', 'M',        // magic
        0, 0, 0, 0,      // size in bytes
        0, 0,            // app data
        0, 0,            // app data
        40 + 14, 0, 0, 0 // start of data offset
    };
    unsigned char bmpInfoHeader[40] = {40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 24, 0};
    unsigned char bmpPad[3] = {0, 0, 0};
    int padSize = (4 - (bmpWidth * 3) % 4) % 4;
    int sizeData = bmpWidth * pngHeight * 3 + pngHeight * padSize;
    int sizeAll = sizeData + sizeof(bmpFileHeader) + sizeof(bmpInfoHeader);

    bmpFileHeader[2] = (unsigned char)(filesize);
    bmpFileHeader[3] = (unsigned char)(filesize >> 8);
    bmpFileHeader[4] = (unsigned char)(filesize >> 16);
    bmpFileHeader[5] = (unsigned char)(filesize >> 24);

    bmpInfoHeader[4] = (unsigned char)(bmpWidth);
    bmpInfoHeader[5] = (unsigned char)(bmpWidth >> 8);
    bmpInfoHeader[6] = (unsigned char)(bmpWidth >> 16);
    bmpInfoHeader[7] = (unsigned char)(bmpWidth >> 24);

    bmpInfoHeader[8] = (unsigned char)(pngHeight);
    bmpInfoHeader[9] = (unsigned char)(pngHeight >> 8);
    bmpInfoHeader[10] = (unsigned char)(pngHeight >> 16);
    bmpInfoHeader[11] = (unsigned char)(pngHeight >> 24);

    // fileBMP = fopen("snapshot.bmp", "wb");
    fwrite(bmpFileHeader, 1, 14, fileBMP);
    fwrite(bmpInfoHeader, 1, 40, fileBMP);

    for (int i = 0; i < pngHeight; i++)
    {
        fwrite(imagePtr + (bmpWidth * (pngHeight - i - 1) * 3), 3, bmpWidth, fileBMP);
        fwrite(bmpPad, 1, padSize, fileBMP);
    }

    fclose(fileBMP);
    ///////////////////////////////////

    // if (fileBMP != stdout)
    // {
    //     fclose(fileBMP);
    // }

    //-------------------------------------------------------------------
    free(imagePtr);
    imagePtr = NULL;

    return 0;
}
