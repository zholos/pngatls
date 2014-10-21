#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <png.h>
#include <zlib.h>


// Runtime options

bool trim;
png_uint_32 padding;
png_uint_32 size = PNG_UINT_32_MAX;


// Images

struct image {
    struct image* next;
    png_uint_32 w, h, tl, tt, tr, tb;
    png_bytepp p;
    char* filename;
    bool packed;
    png_uint_32 x0, y0;
} *images, **images_tail = &images;
png_uint_32 min_w = PNG_UINT_32_MAX,
            min_h = PNG_UINT_32_MAX;


// Utility functions

static void*
wm(void* result)
{
    if (!result)
        err(1, "out of memory");
    return result;
}

static void
ww(int result)
{
    if (result < 0) // error or EOF
        err(1, "can't write");
}

static png_uint_32
imin(png_uint_32 a, png_uint_32 b)
{
    return a < b ? a : b;
}

static png_uint_32
imax(png_uint_32 a, png_uint_32 b)
{
    return a > b ? a : b;
}

static char*
filename_stem(char* path)
{
    char* t;
    char* base = wm(strdup((t = strrchr(path, '/')) ? t+1 : path));
    if ((t = strrchr(base, '.')) && !strcasecmp(t, ".png"))
        *t = '\0';
    return base;
}

static char*
filename_extract(char* path)
{
    char* t;
    char* base = (t = strrchr(path, '/')) ? t+1 : path;
    ww(asprintf(&t, "%s.png", base));
    return t;
}

static char*
filename_seq(char* path, size_t j)
{
    char* t;
    char* base = (t = strrchr(path, '/')) ? t+1 : path;
    char* dot = (t = strrchr(base, '.')) ? t : strchr(base, 0);
    ww(asprintf(&t, "%.*s%05zu%s", (int)(dot-path), path, j, dot));
    return t;
}


// Image loading

static bool
row_clear(png_bytepp p, struct image* i, png_uint_32 y)
{
    for (png_uint_32 x = 0; x < i->w; x++)
        if (p[i->y0+y][(i->x0+x)*4+3] != 0)
            return false;
    return true;
}

static bool
col_clear(png_bytepp p, struct image* i, png_uint_32 x)
{
    for (png_uint_32 y = 0; y < i->h; y++)
        if (p[i->y0+y][(i->x0+x)*4+3] != 0)
            return false;
    return true;
}

static png_byte atLS[5] = { 97, 116, 76, 83 };

static int PNGCBAPI
read_chunk_callback(png_structp png, png_unknown_chunkp chunk)
{
    if (memcmp(chunk->name, atLS, 4))
        return 0; // did not recognize

    png_bytep p = memchr(chunk->data, 0, chunk->size);
    if (!p)
        return -1;
    size_t rest = chunk->data + chunk->size - ++p;
    bool trimmed = rest > 4*4;
    if (rest != 4*4*(1+trimmed))
        return -1;

    struct image i = {
        .x0 = png_get_uint_32(p+4*0),
        .y0 = png_get_uint_32(p+4*1),
        .w = png_get_uint_32(p+4*2),
        .h = png_get_uint_32(p+4*3),
        .tl = trimmed ? png_get_uint_32(p+4*4) : 0,
        .tt = trimmed ? png_get_uint_32(p+4*5) : 0,
        .tr = trimmed ? png_get_uint_32(p+4*6) : 0,
        .tb = trimmed ? png_get_uint_32(p+4*7) : 0,
        .filename = wm(strdup((char*)chunk->data))
    };
    if (i.tl > PNG_UINT_32_MAX-i.w || i.tr > PNG_UINT_32_MAX-i.w-i.tl ||
        i.tt > PNG_UINT_32_MAX-i.w || i.tb > PNG_UINT_32_MAX-i.w-i.tt)
        return -1; // ensure we can restore transparency
    *images_tail = wm(malloc(sizeof i));
    **images_tail = i;
    images_tail = &(*images_tail)->next;
    return 1;
}

static void
read_png(char* filename)
{
    FILE* f = fopen(filename, "rb");
    if (!f)
        err(1, "can't read file: %s", filename);

    // libpng functions check png and info != NULL
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                             NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png);
    png_init_io(png, f);

    struct image** start_tail = images_tail;
    png_set_read_user_chunk_fn(png, NULL, read_chunk_callback);
    png_read_png(png, info,
                 PNG_TRANSFORM_EXPAND | PNG_TRANSFORM_GRAY_TO_RGB, NULL);

    if (png_get_bit_depth(png, info) > 8)
        errx(1, "bit depth > 8");
    png_byte color = png_get_color_type(png, info);
    if (color != PNG_COLOR_TYPE_RGB_ALPHA)
        errx(1, "color type != RGBA");
    png_byte channels = png_get_channels(png, info);
    if (channels != 4)
        errx(1, "channels != 4");

    png_uint_32 w = png_get_image_width(png, info),
                h = png_get_image_height(png, info);
    png_bytepp p = png_get_rows(png, info);

    if (start_tail == images_tail) { // no atLS chunks
        struct image i = {
            .w = w,
            .h = h,
            .filename = filename_stem(filename)
        };
        *images_tail = wm(malloc(sizeof i));
        **images_tail = i;
        images_tail = &(*images_tail)->next;
    }

    for (struct image* i = *start_tail; i; i = i->next) {
        if (i->x0 > w || !i->w || i->w > w - i->x0 ||
            i->y0 > h || !i->h || i->h > h - i->y0)
            errx(1, "invalid atLS chunk");
        if (trim) {
            // keep at least 1 pixel
            for (; i->h>1 && row_clear(p, i, i->h-1); i->h--, i->tb++);
            for (; i->h>1 && row_clear(p, i, 0);      i->h--, i->tt++, i->y0++);
            for (; i->w>1 && col_clear(p, i, i->w-1); i->w--, i->tr++);
            for (; i->w>1 && col_clear(p, i, 0);      i->w--, i->tl++, i->x0++);
        }
        if (i->w > size-padding || i->h > size-padding)
            // ensure pack() can always progress
            errx(1, "image too big: %s (%lux%lu)",
                 i->filename, (unsigned long)i->w, (unsigned long)i->h);
        min_w = imin(min_w, i->w);
        min_h = imin(min_h, i->h);
        i->p = wm(malloc(sizeof *i->p * i->h));
        for (png_uint_32 y = 0; y < i->h; y++) {
            i->p[y] = wm(malloc(sizeof **i->p * 4 * i->w));
            memcpy(i->p[y], &p[i->y0+y][i->x0*4], sizeof **i->p * 4 * i->w);
        }
    }

    png_destroy_read_struct(&png, &info, NULL);
    if (fclose(f))
        err(1, "can't close file");
}

static void
free_packed()
{
    for (struct image** i = &images; *i;)
        if ((*i)->packed) {
            for (png_uint_32 y = 0; y < (*i)->h; y++)
                free((*i)->p[y]);
            free((*i)->p);
            free((*i)->filename);
            struct image* next = (*i)->next;
            free(*i);
            *i = next;
        } else
            i = &(*i)->next;
}


// Write PNG

static void
write_png(char* filename, struct image* i)
{
    FILE* f = fopen(filename, "wb");
    if (!f)
        err(1, "can't write file: %s", filename);

    // libpng functions check png and info != NULL
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                              NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png);
    png_init_io(png, f);

    png_uint_32 w = i ? i->w+i->tl+i->tr : size,
                h = i ? i->h+i->tt+i->tb : size;
    png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGB_ALPHA,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    // Faster and better compression with no filtering
    png_set_filter(png, PNG_FILTER_TYPE_BASE, PNG_FILTER_NONE);
    png_set_compression_level(png, Z_BEST_COMPRESSION);
    png_set_compression_mem_level(png, MAX_MEM_LEVEL);
    png_set_compression_strategy(png, Z_DEFAULT_STRATEGY);
    png_set_compression_buffer_size(png, 1<<20);

    png_bytepp p = wm(calloc(h, sizeof *p));
    for (png_uint_32 y = 0; y < h; y++)
        p[y] = wm(calloc(w, sizeof **p * 4));

    for (struct image* i = images; i; i = i->next)
        if (i->packed) {
            for (png_uint_32 y = 0; y < i->h; y++)
                memcpy(&p[i->y0+y][i->x0*4], i->p[y], sizeof **p * 4 * i->w);

            bool trimmed = i->tl || i->tt || i->tr || i->tb;
            png_byte data[4*4];
            png_write_chunk_start(png, atLS, strlen(i->filename) + 1 +
                                             sizeof data*(1+trimmed));
            png_write_chunk_data(png, (png_const_bytep)i->filename,
                                 strlen(i->filename) + 1);
            png_save_uint_32(data+4*0, i->x0);
            png_save_uint_32(data+4*1, i->y0);
            png_save_uint_32(data+4*2, i->w);
            png_save_uint_32(data+4*3, i->h);
            png_write_chunk_data(png, data, sizeof data);
            if (trimmed) {
                png_save_uint_32(data+4*0, i->tl);
                png_save_uint_32(data+4*1, i->tt);
                png_save_uint_32(data+4*2, i->tr);
                png_save_uint_32(data+4*3, i->tb);
                png_write_chunk_data(png, data, sizeof data);
            }
            png_write_chunk_end(png);
        }
    if (i)
        for (png_uint_32 y = 0; y < i->h; y++)
            memcpy(&p[i->tt+y][i->tl*4], i->p[y], sizeof **p * 4 * i->w);

    png_write_image(png, p);
    png_write_end(png, info);

    for (png_uint_32 y = 0; y < h; y++)
        free(p[y]);
    free(p);

    png_destroy_write_struct(&png, &info);
    if (fclose(f))
        err(1, "can't close file");
}


// Write XML

// XML format described at
// http://doc.starling-framework.org/core/starling/textures/TextureAtlas.html

static void
fquoteattr(FILE* f, char* s)
{
    ww(fputc('"', f));
    for (; *s; s++)
        switch (*s) {
        case '<': ww(fputs("&lt;", f)); break;
        case '&': ww(fputs("&amp;", f)); break;
        case '"': ww(fputs("&quot;", f)); break;
        default: ww(fputc(*s, f));
        }
    ww(fputc('"', f));
}

static void
write_xml(char* filename, char* output)
{
    FILE* f = fopen(filename, "wt");
    if (!f)
        err(1, "can't write file: %s", filename);
    ww(fputs("<TextureAtlas imagePath=", f));
    fquoteattr(f, output);
    ww(fputs(">\n", f));

    for (struct image* i = images; i; i = i->next)
        if (i->packed) {
            ww(fputs("  <SubTexture name=", f));
            fquoteattr(f, i->filename);
            ww(fprintf(f, " x=\"%lu\" y=\"%lu\" width=\"%lu\" height=\"%lu\"",
                       (unsigned long)i->x0, (unsigned long)i->y0,
                       (unsigned long)i->w, (unsigned long)i->h));
            if (i->tl || i->tt || i->tr || i->tb)
                ww(fprintf(f, " frameX=\"-%lu\" frameY=\"-%lu\""
                              " frameWidth=\"%lu\" frameHeight=\"%lu\"",
                           (unsigned long)i->tl, (unsigned long)i->tt,
                           (unsigned long)(i->w + i->tl + i->tr),
                           (unsigned long)(i->h + i->tt + i->tb)));
            ww(fputs("/>\n", f));
        }

    ww(fputs("</TextureAtlas>\n", f));
    if (fclose(f))
        err(1, "can't close file");
}


// Write JSON

// Unspecified but common format from random examples and
// http://docs.phaser.io/AnimationParser.js.html

static void
fquotestring(FILE* f, char* s)
{
    // NOTE: not all control characters escaped
    ww(fputc('"', f));
    for (; *s; s++)
        switch (*s) {
        case '"': ww(fputs("\\\"", f)); break;
        case '\\': ww(fputs("\\\\", f)); break;
        default: ww(fputc(*s, f));
        }
    ww(fputc('"', f));
}

static void
write_json(char* filename, char* output)
{
    FILE* f = fopen(filename, "wt");
    if (!f)
        err(1, "can't write file: %s", filename);
    ww(fputs("{\n    \"meta\": { \"image\": ", f));
    fquotestring(f, output);
    ww(fprintf(f, ", \"size\": { \"w\": %lu, \"h\": %lu } },\n",
               (unsigned long)size, (unsigned long)size));
    ww(fputs("    \"frames\": [\n", f));

    for (struct image* i = images; i; i = i->next)
        if (i->packed) {
            ww(fputs("        { \"filename\": ", f));
            fquoteattr(f, i->filename);
            ww(fprintf(f, ", \"frame\": { "
                          "\"x\": %lu, \"y\": %lu, \"w\": %lu, \"h\": %lu }",
                       (unsigned long)i->x0, (unsigned long)i->y0,
                       (unsigned long)i->w, (unsigned long)i->h));
            if (i->tl || i->tt || i->tr || i->tb)
                ww(fprintf(f, ", \"trimmed\": true, \"spriteSourceSize\": { "
                              "\"x\": %lu, \"y\": %lu, \"w\": %lu, \"h\": %lu }"
                              ", \"sourceSize\": { \"w\": %lu, \"h\": %lu }",
                           (unsigned long)i->tl, (unsigned long)i->tt,
                           (unsigned long)i->w, (unsigned long)i->h,
                           (unsigned long)(i->w + i->tl + i->tr),
                           (unsigned long)(i->h + i->tt + i->tb)));
            ww(fprintf(f, " }%s\n", i->next ? "," : ""));
        }

    ww(fputs("    ]\n}\n", f));
    if (fclose(f))
        err(1, "can't close file");
}


// Packing

// Implementation of Maximal Rectangles algorithm (MAXRECTS-BSSF-GLOBAL) as
// described in http://clb.demon.fi/files/RectangleBinPack.pdf

// Additional tie-breaking rules ensure consistent output regardless of how
// image and free rectangle lists are ordered.

struct rect {
    struct rect* next;
    png_uint_32 x0, y0, x1, y1;
}* free_rects;

static void
add_rect(png_uint_32 x0, png_uint_32 y0, png_uint_32 x1, png_uint_32 y1)
{
    if (x0 > x1 || x1 - x0 < min_w + padding ||
        y0 > y1 || y1 - y0 < min_h + padding)
        return;
    for (struct rect** f = &free_rects; *f;)
        if (x0 >= (*f)->x0 && x1 <= (*f)->x1 &&
            y0 >= (*f)->y0 && y1 <= (*f)->y1)
            return;
        else if (x0 <= (*f)->x0 && x1 >= (*f)->x1 &&
                 y0 <= (*f)->y0 && y1 >= (*f)->y1) {
            struct rect* next = (*f)->next;
            free(*f);
            *f = next;
        } else
            f = &(*f)->next;
    struct rect f = { .x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1 };
    f.next = free_rects;
    free_rects = wm(malloc(sizeof f));
    *free_rects = f;
}

static void
pack()
{
    add_rect(0, 0, size, size); // clears existing
    for (struct image* i = images; i; i = i->next)
        i->packed = false;

    for (;;) {
        struct image* b = NULL;
        png_uint_32 bssf, blsf;
        png_uint_32 bx1, by1;
        for (struct image* i = images; i; i = i->next) {
            if (i->packed)
                continue;
            for (struct rect* f = free_rects; f; f = f->next) {
                if (f->x1 - f->x0 < i->w + padding ||
                    f->y1 - f->y0 < i->h + padding)
                    continue;
                png_uint_32 x1 = f->x0 + i->w + padding,
                            y1 = f->y0 + i->h + padding;
                png_uint_32 ssf = imin(f->x1 - x1, f->y1 - y1),
                            lsf = imax(f->x1 - x1, f->y1 - y1);
                if (!b || bssf > ssf || bssf == ssf &&
                         (blsf > lsf || blsf == lsf &&
                         (imax(bx1, by1) > imax(x1, y1) ||
                          imax(bx1, by1) == imax(x1, y1) &&
                         (imin(bx1, by1) > imin(x1, y1) ||
                          imin(bx1, by1) == imin(x1, y1) &&
                          (bx1 < by1) > (x1 < y1))))) {
                    b = i;
                    bssf = ssf;
                    blsf = lsf;
                    bx1 = x1;
                    by1 = y1;
                    b->x0 = f->x0;
                    b->y0 = f->y0;
                }
            }
        }
        if (!b)
            break;
        b->packed = true;

        struct rect* f = free_rects;
        free_rects = NULL;
        while (f) {
            add_rect(imax(f->x0, bx1), f->y0, f->x1, f->y1);
            add_rect(f->x0, imax(f->y0, by1), f->x1, f->y1);
            add_rect(f->x0, f->y0, imin(f->x1, b->x0), f->y1);
            add_rect(f->x0, f->y0, f->x1, imin(f->y1, b->y0));
            struct rect* next = f->next;
            free(f);
            f = next;
        }
    }
}


// Main driver

int
main(int argc, char** argv)
{
    char *xml = NULL, *json = NULL;
    bool extract = false;
    int opt;
    while ((opt = getopt(argc, argv, "tp:m:x:j:eh")) != -1)
        switch (opt) {
        case 't':
            trim = true;
            break;
        case 'p':
        case 'm': {
            char* end;
            long v = strtol(optarg, &end, 10);
            if (v < 0 || v > PNG_UINT_32_MAX)
                goto usage;
            *(opt == 'p' ? &padding : &size) = v;
            break;
        }
        case 'x':
            xml = optarg;
            break;
        case 'j':
            json = optarg;
            break;
        case 'e':
            extract = true;
            break;
        default:
        usage:
            puts("usage: pngatls [-t] [-p pad] [-m size] [-x .xml] [-j .json] "
                 "atlas.png in.png ...");
            puts("       pngatls -e atlas.png ...");
            return opt == 'h' ? 0 : 1;
        }
    if (padding >= size)
        errx(1, "padding >= size");

    if (extract) {
        if (argc - optind < 1 || trim || padding || xml || json)
            goto usage;
        while (optind != argc)
            read_png(argv[optind++]);
        for (struct image* i = images; i; i = i->next)
            write_png(filename_extract(i->filename), i);
    } else {
        if (argc - optind < 2)
            goto usage;

        char* output = argv[optind++];
        while (optind != argc)
            read_png(argv[optind++]);

        if (size == PNG_UINT_32_MAX) {
            for (size = 1;; size *= 2) {
                pack();
                struct image* i = images;
                for (; i && i->packed; i = i->next);
                if (!i)
                    break;
                if (size >= PNG_UINT_32_MAX / 2)
                    errx(1, "too big");
            }
            write_png(output, NULL);
            if (xml)
                write_xml(xml, output);
            if (json)
                write_json(json, output);
            free_packed();
        } else
            for (size_t j = 1; images; j++) {
                pack();
                char* output_seq = filename_seq(output, j);
                write_png(output_seq, NULL);
                if (xml)
                    write_xml(filename_seq(xml, j), output_seq);
                if (json)
                    write_json(filename_seq(json, j), output_seq);
                free_packed();
            }
    }
}
