/**
 * Simple engine for creating PDF files.
 * It supports text, shapes, images etc...
 * Capable of handling millions of objects without too much performance
 * penalty.
 * Public domain license - no warranty implied; use at your own risk.
 */

/**
 * PDF HINTS & TIPS
 * The specification can be found at
 * https://www.adobe.com/content/dam/acom/en/devnet/pdf/pdfs/pdf_reference_archives/PDFReference.pdf
 * The following sites have various bits & pieces about PDF document
 * generation
 * http://www.mactech.com/articles/mactech/Vol.15/15.09/PDFIntro/index.html
 * http://gnupdf.org/Introduction_to_PDF
 * http://www.planetpdf.com/mainpage.asp?WebPageID=63
 * http://archive.vector.org.uk/art10008970
 * http://www.adobe.com/devnet/acrobat/pdfs/pdf_reference_1-7.pdf
 * https://blog.idrsolutions.com/2013/01/understanding-the-pdf-file-format-overview/
 *
 * To validate the PDF output, there are several online validators:
 * http://www.validatepdfa.com/online.htm
 * http://www.datalogics.com/products/callas/callaspdfA-onlinedemo.asp
 * http://www.pdf-tools.com/pdf/validate-pdfa-online.aspx
 *
 * In addition the 'pdftk' server can be used to analyse the output:
 * https://www.pdflabs.com/docs/pdftk-cli-examples/
 *
 * PDF page markup operators:
 * b    closepath, fill,and stroke path.
 * B    fill and stroke path.
 * b*   closepath, eofill,and stroke path.
 * B*   eofill and stroke path.
 * BI   begin image.
 * BMC  begin marked content.
 * BT   begin text object.
 * BX   begin section allowing undefined operators.
 * c    curveto.
 * cm   concat. Concatenates the matrix to the current transform.
 * cs   setcolorspace for fill.
 * CS   setcolorspace for stroke.
 * d    setdash.
 * Do   execute the named XObject.
 * DP   mark a place in the content stream, with a dictionary.
 * EI   end image.
 * EMC  end marked content.
 * ET   end text object.
 * EX   end section that allows undefined operators.
 * f    fill path.
 * f*   eofill Even/odd fill path.
 * g    setgray (fill).
 * G    setgray (stroke).
 * gs   set parameters in the extended graphics state.
 * h    closepath.
 * i    setflat.
 * ID   begin image data.
 * j    setlinejoin.
 * J    setlinecap.
 * k    setcmykcolor (fill).
 * K    setcmykcolor (stroke).
 * l    lineto.
 * m    moveto.
 * M    setmiterlimit.
 * n    end path without fill or stroke.
 * q    save graphics state.
 * Q    restore graphics state.
 * re   rectangle.
 * rg   setrgbcolor (fill).
 * RG   setrgbcolor (stroke).
 * s    closepath and stroke path.
 * S    stroke path.
 * sc   setcolor (fill).
 * SC   setcolor (stroke).
 * sh   shfill (shaded fill).
 * Tc   set character spacing.
 * Td   move text current point.
 * TD   move text current point and set leading.
 * Tf   set font name and size.
 * Tj   show text.
 * TJ   show text, allowing individual character positioning.
 * TL   set leading.
 * Tm   set text matrix.
 * Tr   set text rendering mode.
 * Ts   set super/subscripting text rise.
 * Tw   set word spacing.
 * Tz   set horizontal scaling.
 * T*   move to start of next line.
 * v    curveto.
 * w    setlinewidth.
 * W    clip.
 * y    curveto.
 */

#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS 1 // Drop the MSVC complaints about snprintf
#define _USE_MATH_DEFINES
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#else

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE /* For localtime_r */
#endif

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600 /* for M_SQRT2 */
#endif

#include <sys/types.h> /* for ssize_t */
#endif

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "pdfgen.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define PDF_RGB_R(c) (float)((((c) >> 16) & 0xff) / 255.0)
#define PDF_RGB_G(c) (float)((((c) >> 8) & 0xff) / 255.0)
#define PDF_RGB_B(c) (float)((((c) >> 0) & 0xff) / 255.0)
#define PDF_IS_TRANSPARENT(c) (((c) >> 24) == 0xff)

#if defined(_MSC_VER)
#define inline __inline
#define snprintf _snprintf
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define fileno _fileno
#define fstat _fstat
#ifdef stat
#undef stat
#endif
#define stat _stat
#define SKIP_ATTRIBUTE
#else
#include <strings.h> // strcasecmp
#endif

/**
 * Try and support big & little endian machines
 */
static inline uint32_t bswap32(uint32_t x)
{
    return (((x & 0xff000000u) >> 24) | ((x & 0x00ff0000u) >> 8) |
            ((x & 0x0000ff00u) << 8) | ((x & 0x000000ffu) << 24));
}

#ifdef __has_include // C++17, supported as extension to C++11 in clang, GCC
                     // 5+, vs2015
#if __has_include(<endian.h>)
#include <endian.h> // gnu libc normally provides, linux
#elif __has_include(<machine/endian.h>)
#include <machine/endian.h> //open bsd, macos
#elif __has_include(<sys/param.h>)
#include <sys/param.h> // mingw, some bsd (not open/macos)
#elif __has_include(<sys/isadefs.h>)
#include <sys/isadefs.h> // solaris
#endif
#endif

#if !defined(__LITTLE_ENDIAN__) && !defined(__BIG_ENDIAN__)
#ifndef __BYTE_ORDER__
/* Fall back to little endian by default */
#define __LITTLE_ENDIAN__
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ || __BYTE_ORDER == __BIG_ENDIAN
#define __BIG_ENDIAN__
#else
#define __LITTLE_ENDIAN__
#endif
#endif

#if defined(__LITTLE_ENDIAN__)
#define ntoh32(x) bswap32((x))
#elif defined(__BIG_ENDIAN__)
#define ntoh32(x) (x)
#endif

// Limits on image sizes for sanity checking & to avoid plausible overflow
// issues
#define MAX_IMAGE_WIDTH (16 * 1024)
#define MAX_IMAGE_HEIGHT (16 * 1024)

// Signatures for various image formats
static const uint8_t bmp_signature[] = {'B', 'M'};
static const uint8_t png_signature[] = {0x89, 0x50, 0x4E, 0x47,
                                        0x0D, 0x0A, 0x1A, 0x0A};
static const uint8_t jpeg_signature[] = {0xff, 0xd8};
static const uint8_t ppm_signature[] = {'P', '6'};
static const uint8_t pgm_signature[] = {'P', '5'};

// Special signatures for PNG chunks
static const char png_chunk_header[] = "IHDR";
static const char png_chunk_palette[] = "PLTE";
static const char png_chunk_data[] = "IDAT";
static const char png_chunk_end[] = "IEND";

// Read big-endian values from a byte array (used for TTF parsing)
static uint16_t ttf_be16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

static uint32_t ttf_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int16_t ttf_bes16(const uint8_t *p)
{
    return (int16_t)ttf_be16(p);
}

// Find a table in a TrueType font file by its 4-byte tag.
// Returns a pointer to the table data and optionally sets *table_len.
static const uint8_t *ttf_find_table(const uint8_t *data, size_t data_len,
                                     const char *tag, uint32_t *table_len)
{
    if (data_len < 12)
        return NULL;
    uint16_t numTables = ttf_be16(data + 4);
    if (numTables > (data_len - 12) / 16)
        numTables = (uint16_t)((data_len - 12) / 16);
    for (uint16_t i = 0; i < numTables; i++) {
        if ((size_t)(12 + ((size_t)i + 1) * 16) > data_len)
            break;
        const uint8_t *entry = data + 12 + (size_t)i * 16;
        if (memcmp(entry, tag, 4) == 0) {
            uint32_t offset = ttf_be32(entry + 8);
            uint32_t length = ttf_be32(entry + 12);
            if ((size_t)offset + length > data_len)
                return NULL;
            if (table_len)
                *table_len = length;
            return data + offset;
        }
    }
    return NULL;
}

// Look up glyph ID in a format-4 cmap subtable.
// The `subtable` pointer must point directly to the start of a format-4
// subtable (i.e., ttf_be16(subtable) == 4).
static uint16_t ttf_cmap_subtable_lookup(const uint8_t *subtable,
                                         size_t subtable_len,
                                         uint32_t unicode)
{
    if (!subtable || subtable_len < 14)
        return 0;

    uint16_t segCountX2 = ttf_be16(subtable + 6);
    uint16_t segCount = segCountX2 / 2;
    const uint8_t *endCounts = subtable + 14;
    const uint8_t *startCounts = endCounts + segCountX2 + 2;
    const uint8_t *idDeltas = startCounts + segCountX2;
    const uint8_t *idRangeOffsets = idDeltas + segCountX2;

    if (14 + (size_t)segCountX2 * 4 + 2 > subtable_len)
        return 0;

    for (uint16_t i = 0; i < segCount; i++) {
        uint16_t endCount = ttf_be16(endCounts + (size_t)i * 2);
        if (unicode > endCount)
            continue;
        uint16_t startCount = ttf_be16(startCounts + (size_t)i * 2);
        if (unicode < startCount)
            return 0; // No segment covers this codepoint
        int16_t idDelta = ttf_bes16(idDeltas + (size_t)i * 2);
        uint16_t idRangeOffset = ttf_be16(idRangeOffsets + (size_t)i * 2);

        uint16_t glyphId;
        if (idRangeOffset == 0) {
            glyphId =
                (uint16_t)((unicode + (uint32_t)(uint16_t)idDelta) & 0xFFFFu);
        } else {
            const uint8_t *ptr = idRangeOffsets + (size_t)i * 2 +
                                 idRangeOffset +
                                 (size_t)(unicode - startCount) * 2;
            if (ptr < subtable || ptr + 2 > subtable + subtable_len)
                return 0;
            glyphId = ttf_be16(ptr);
            if (glyphId == 0)
                return 0;
            glyphId =
                (uint16_t)((glyphId + (uint32_t)(uint16_t)idDelta) & 0xFFFFu);
        }
        return glyphId;
    }
    return 0;
}

// Find the best format-4 cmap subtable in a full cmap table.
// Returns a pointer to the subtable start (within `cmap`), and sets
// `*out_subtable_len` to the length given in the subtable header.
// Returns NULL if no suitable subtable is found.
static const uint8_t *ttf_find_cmap_subtable(const uint8_t *cmap,
                                             size_t cmap_len,
                                             uint16_t *out_subtable_len)
{
    if (!cmap || cmap_len < 4)
        return NULL;
    uint16_t numTables = ttf_be16(cmap + 2);
    if (numTables > (cmap_len - 4) / 8)
        numTables = (uint16_t)((cmap_len - 4) / 8);
    const uint8_t *best_subtable = NULL;
    int best_priority = -1;

    for (uint16_t i = 0; i < numTables; i++) {
        size_t rec_off = 4 + (size_t)i * 8;
        if (rec_off + 8 > cmap_len)
            break;
        const uint8_t *rec = cmap + rec_off;
        uint16_t platformID = ttf_be16(rec);
        uint16_t platEncID = ttf_be16(rec + 2);
        uint32_t subtable_offset = ttf_be32(rec + 4);
        if ((size_t)subtable_offset + 4 > cmap_len)
            continue;
        const uint8_t *sub = cmap + subtable_offset;
        uint16_t fmt = ttf_be16(sub);
        if (fmt != 4)
            continue; // Only format 4 is supported

        int priority;
        if (platformID == 3 && platEncID == 1)
            priority = 3; // Windows Unicode BMP - preferred
        else if (platformID == 0 && (platEncID == 3 || platEncID == 4))
            priority = 2; // Unicode BMP
        else if (platformID == 0)
            priority = 1; // Other Unicode
        else if (platformID == 1 && platEncID == 0)
            priority = 0; // Mac Roman fallback
        else
            continue;

        if (priority > best_priority) {
            best_priority = priority;
            best_subtable = sub;
        }
    }
    if (!best_subtable)
        return NULL;
    if (out_subtable_len)
        *out_subtable_len = ttf_be16(best_subtable + 2);
    return best_subtable;
}

// Get the advance width of a glyph from the hmtx table.
static uint16_t ttf_hmtx_advance(const uint8_t *hmtx, size_t hmtx_len,
                                 uint16_t glyph_id, uint16_t num_h_metrics)
{
    if (!hmtx || num_h_metrics == 0)
        return 0;
    if (glyph_id < num_h_metrics) {
        size_t offset = (size_t)glyph_id * 4;
        if (offset + 2 > hmtx_len)
            return 0;
        return ttf_be16(hmtx + offset);
    }
    // Glyphs past numberOfHMetrics share the last advance width
    size_t offset = (size_t)(num_h_metrics - 1) * 4;
    if (offset + 2 > hmtx_len)
        return 0;
    return ttf_be16(hmtx + offset);
}

// Extract the PostScript name (nameID 6) from the TTF 'name' table.
static void ttf_extract_name(const uint8_t *name_table, size_t table_len,
                             char *out, size_t out_len)
{
    if (!out || out_len == 0)
        return;
    out[0] = '\0';
    if (!name_table || table_len < 6)
        return;
    uint16_t count = ttf_be16(name_table + 2);
    if (count > (table_len - 6) / 12)
        count = (uint16_t)((table_len - 6) / 12);
    uint16_t stringOffset = ttf_be16(name_table + 4);

    for (uint16_t i = 0; i < count; i++) {
        size_t rec_off = 6 + (size_t)i * 12;
        if (rec_off + 12 > table_len)
            break;
        const uint8_t *rec = name_table + rec_off;
        uint16_t platformID = ttf_be16(rec);
        uint16_t nameID = ttf_be16(rec + 6);
        uint16_t length = ttf_be16(rec + 8);
        uint16_t offset = ttf_be16(rec + 10);
        if (nameID != 6) // Only the PostScript name (ID 6)
            continue;
        size_t str_off = stringOffset + offset;
        if (str_off + length > table_len)
            continue;
        const uint8_t *str = name_table + str_off;
        if (platformID == 3) {
            // Windows UTF-16 BE: extract only ASCII characters
            size_t ascii_len = 0;
            size_t safe_length = length;
            if (safe_length > table_len - str_off)
                safe_length = table_len - str_off;
            for (size_t j = 0; j + 1 < safe_length && ascii_len < out_len - 1;
                 j += 2) {
                uint16_t cp = ttf_be16(str + j);
                if (cp < 128)
                    out[ascii_len++] = (char)cp;
            }
            out[ascii_len] = '\0';
            if (ascii_len > 0)
                return;
        } else if (platformID == 1) {
            // Mac Roman: ASCII-compatible
            size_t safe_length = length;
            if (safe_length > table_len - str_off)
                safe_length = table_len - str_off;
            size_t copy_len =
                safe_length < out_len - 1 ? safe_length : out_len - 1;
            memcpy(out, str, copy_len);
            out[copy_len] = '\0';
            return;
        }
    }
}

// PDF standard fonts
static const char *valid_fonts[] = {
    "Times-Roman",
    "Times-Bold",
    "Times-Italic",
    "Times-BoldItalic",
    "Helvetica",
    "Helvetica-Bold",
    "Helvetica-Oblique",
    "Helvetica-BoldOblique",
    "Courier",
    "Courier-Bold",
    "Courier-Oblique",
    "Courier-BoldOblique",
    "Symbol",
    "ZapfDingbats",
};

typedef struct pdf_object pdf_object;

enum {
    OBJ_none, /* skipped */
    OBJ_info,
    OBJ_stream,
    OBJ_font,
    OBJ_font_descriptor,
    OBJ_cid_font,
    OBJ_page,
    OBJ_bookmark,
    OBJ_outline,
    OBJ_catalog,
    OBJ_pages,
    OBJ_image,
    OBJ_link,

    OBJ_count,
};

struct flexarray {
    void ***bins;
    int item_count;
    int bin_count;
};

/**
 * Simple dynamic string object. Tries to store a reasonable amount on the
 * stack before falling back to malloc once things get large
 */
struct dstr {
    char static_data[128];
    char *data;
    size_t alloc_len;
    size_t used_len;
};

struct pdf_object {
    int type;                /* See OBJ_xxxx */
    int index;               /* PDF output index */
    int offset;              /* Byte position within the output file */
    struct pdf_object *prev; /* Previous of this type */
    struct pdf_object *next; /* Next of this type */
    union {
        struct {
            struct pdf_object *page;
            char name[64];
            struct pdf_object *parent;
            struct flexarray children;
        } bookmark;
        struct {
            struct pdf_object *page;
            struct dstr stream;
        } stream;
        struct {
            float width;
            float height;
            struct flexarray children;
            struct flexarray annotations;
        } page;
        struct pdf_info *info;
        struct {
            char name[64];
            int index;
            bool is_ttf;
            // TTF: raw table copies kept for glyph lookups at text-add time
            uint8_t *cmap_data; // copy of best format-4 cmap subtable
            uint32_t cmap_data_len;
            uint8_t *hmtx_data; // copy of hmtx table
            uint32_t hmtx_data_len;
            uint16_t num_h_metrics; // from hhea
            int units_per_em;       // from head
            int descriptor_index;   // index of OBJ_font_descriptor
            int cid_font_index;     // index of OBJ_cid_font descendant
        } font;
        struct {
            char font_name[64];
            int descriptor_index; // index of OBJ_font_descriptor
            int default_width; // /DW: PDF width (thousandths/em) for unlisted
                               // glyphs
            uint16_t
                *glyph_widths; // per-glyph PDF width; num_h_metrics entries
            uint16_t num_h_metrics;
        } cid_font;
        struct {
            char font_name[64];
            int flags;
            float bbox[4]; // [xmin, ymin, xmax, ymax] in 1000/em units
            float italic_angle;
            float ascent;
            float descent;
            float cap_height;
            float stem_v;
            int font_file_index; // index of stream object with embedded font
        } font_descriptor;
        struct {
            float llx; /* Clickable rectangle */
            float lly;
            float urx;
            float ury;
            struct pdf_object *target_page; /* Target page */
            float target_x;                 /* Target location */
            float target_y;
        } link;
    };
};

struct pdf_doc {
    char errstr[128];
    int errval;
    struct flexarray objects;

    float width;
    float height;

    struct pdf_object *current_font;

    struct pdf_object *last_objects[OBJ_count];
    struct pdf_object *first_objects[OBJ_count];
};

/**
 * Since we're casting random areas of memory to these, make sure
 * they're packed properly to match the image format requirements
 */
#pragma pack(push, 1)
struct png_chunk {
    uint32_t length;
    // chunk type, see png_chunk_header, png_chunk_data, png_chunk_end
    char type[4];
};

#pragma pack(pop)

// Simple data container to store a single 24 Bit RGB value, used for
// processing PNG images
struct rgb_value {
    uint8_t red;
    uint8_t blue;
    uint8_t green;
};

/**
 * Simple flexible resizing array implementation
 * The bins get larger in powers of two
 * bin 0 = 1024 items
 *     1 = 2048 items
 *     2 = 4096 items
 *     etc...
 */
/* What is the first index that will be in the given bin? */
#define MIN_SHIFT 10
#define MIN_OFFSET ((1 << MIN_SHIFT) - 1)
static int bin_offset[] = {
    (1 << (MIN_SHIFT + 0)) - 1 - MIN_OFFSET,
    (1 << (MIN_SHIFT + 1)) - 1 - MIN_OFFSET,
    (1 << (MIN_SHIFT + 2)) - 1 - MIN_OFFSET,
    (1 << (MIN_SHIFT + 3)) - 1 - MIN_OFFSET,
    (1 << (MIN_SHIFT + 4)) - 1 - MIN_OFFSET,
    (1 << (MIN_SHIFT + 5)) - 1 - MIN_OFFSET,
    (1 << (MIN_SHIFT + 6)) - 1 - MIN_OFFSET,
    (1 << (MIN_SHIFT + 7)) - 1 - MIN_OFFSET,
    (1 << (MIN_SHIFT + 8)) - 1 - MIN_OFFSET,
    (1 << (MIN_SHIFT + 9)) - 1 - MIN_OFFSET,
    (1 << (MIN_SHIFT + 10)) - 1 - MIN_OFFSET,
    (1 << (MIN_SHIFT + 11)) - 1 - MIN_OFFSET,
    (1 << (MIN_SHIFT + 12)) - 1 - MIN_OFFSET,
    (1 << (MIN_SHIFT + 13)) - 1 - MIN_OFFSET,
    (1 << (MIN_SHIFT + 14)) - 1 - MIN_OFFSET,
    (1 << (MIN_SHIFT + 15)) - 1 - MIN_OFFSET,
};

static inline int flexarray_get_bin(const struct flexarray *flex, int index)
{
    (void)flex;
    for (int i = 0; i < (int)ARRAY_SIZE(bin_offset); i++)
        if (index < bin_offset[i])
            return i - 1;
    return -1;
}

static inline int flexarray_get_bin_size(const struct flexarray *flex,
                                         int bin)
{
    (void)flex;
    if (bin >= (int)ARRAY_SIZE(bin_offset) - 1)
        return -1;
    int next = bin_offset[bin + 1];
    return next - bin_offset[bin];
}

static inline int flexarray_get_bin_offset(const struct flexarray *flex,
                                           int bin, int index)
{
    (void)flex;
    return index - bin_offset[bin];
}

static void flexarray_clear(struct flexarray *flex)
{
    for (int i = 0; i < flex->bin_count; i++)
        free(flex->bins[i]);
    free(flex->bins);
    flex->bin_count = 0;
    flex->item_count = 0;
}

static inline int flexarray_size(const struct flexarray *flex)
{
    return flex->item_count;
}

static int flexarray_set(struct flexarray *flex, int index, void *data)
{
    int bin = flexarray_get_bin(flex, index);
    if (bin < 0)
        return -EINVAL;
    while (bin >= flex->bin_count) {
        void ***bins =
            (void ***)malloc((flex->bin_count + 1) * sizeof(*flex->bins));
        if (!bins)
            return -ENOMEM;
        if (flex->bins) {
            for (int i = 0; i < flex->bin_count; i++) {
                bins[i] = flex->bins[i];
            }
            free(flex->bins);
        }
        flex->bin_count++;
        flex->bins = bins;
        flex->bins[flex->bin_count - 1] =
            (void **)calloc(flexarray_get_bin_size(flex, flex->bin_count - 1),
                            sizeof(void *));
        if (!flex->bins[flex->bin_count - 1]) {
            flex->bin_count--;
            return -ENOMEM;
        }
    }
    flex->item_count++;
    flex->bins[bin][flexarray_get_bin_offset(flex, bin, index)] = data;
    return flex->item_count - 1;
}

static inline int flexarray_append(struct flexarray *flex, void *data)
{
    return flexarray_set(flex, flexarray_size(flex), data);
}

static inline void *flexarray_get(const struct flexarray *flex, int index)
{
    int bin;

    if (index >= flex->item_count)
        return NULL;
    bin = flexarray_get_bin(flex, index);
    if (bin < 0 || bin >= flex->bin_count)
        return NULL;
    return flex->bins[bin][flexarray_get_bin_offset(flex, bin, index)];
}

/**
 * Simple dynamic string object. Tries to store a reasonable amount on the
 * stack before falling back to malloc once things get large
 */

#define INIT_DSTR                                                            \
    (struct dstr){                                                           \
        .static_data = {0}, .data = NULL, .alloc_len = 0, .used_len = 0}

static char *dstr_data(struct dstr *str)
{
    return str->data ? str->data : str->static_data;
}

static size_t dstr_len(const struct dstr *str)
{
    return str->used_len;
}

static ssize_t dstr_ensure(struct dstr *str, size_t len)
{
    if (len <= str->alloc_len)
        return 0;
    if (!str->data && len <= sizeof(str->static_data))
        str->alloc_len = len;
    else {
        size_t new_len;

        new_len = len + 4096;

        if (str->data) {
            char *new_data = (char *)realloc((void *)str->data, new_len);
            if (!new_data)
                return -ENOMEM;
            str->data = new_data;
        } else {
            str->data = (char *)malloc(new_len);
            if (!str->data)
                return -ENOMEM;
            if (str->used_len)
                memcpy(str->data, str->static_data, str->used_len + 1);
        }

        str->alloc_len = new_len;
    }
    return 0;
}

// Locales can replace the decimal character with a ','.
// This breaks the PDF output, so we force a 'safe' locale.
static void force_locale(char *buf, int len)
{
    const char *saved_locale = setlocale(LC_ALL, NULL);

    if (!saved_locale) {
        *buf = '\0';
    } else {
        strncpy(buf, saved_locale, len - 1);
        buf[len - 1] = '\0';
    }

    setlocale(LC_NUMERIC, "POSIX");
}

static void restore_locale(const char *buf)
{
    setlocale(LC_ALL, buf);
}

#ifndef SKIP_ATTRIBUTE
static int dstr_printf(struct dstr *str, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#endif
static int dstr_printf(struct dstr *str, const char *fmt, ...)
{
    va_list ap, aq;
    int len;
    char saved_locale[32];

    force_locale(saved_locale, sizeof(saved_locale));

    va_start(ap, fmt);
    va_copy(aq, ap);
    len = vsnprintf(NULL, 0, fmt, ap);
    if (dstr_ensure(str, str->used_len + len + 1) < 0) {
        va_end(ap);
        va_end(aq);
        restore_locale(saved_locale);
        return -ENOMEM;
    }
    vsprintf(dstr_data(str) + str->used_len, fmt, aq);
    str->used_len += len;
    va_end(ap);
    va_end(aq);
    restore_locale(saved_locale);

    return len;
}

static ssize_t dstr_append_data(struct dstr *str, const void *extend,
                                size_t len)
{
    if (dstr_ensure(str, str->used_len + len + 1) < 0)
        return -ENOMEM;
    memcpy(dstr_data(str) + str->used_len, extend, len);
    str->used_len += len;
    dstr_data(str)[str->used_len] = '\0';
    return len;
}

static ssize_t dstr_append(struct dstr *str, const char *extend)
{
    return dstr_append_data(str, extend, strlen(extend));
}

static void dstr_free(struct dstr *str)
{
    if (str->data)
        free(str->data);
    *str = INIT_DSTR;
}

/**
 * PDF Implementation
 */

#ifndef SKIP_ATTRIBUTE
static int pdf_set_err(struct pdf_doc *doc, int errval, const char *buffer,
                       ...) __attribute__((format(printf, 3, 4)));
#endif
static int pdf_set_err(struct pdf_doc *doc, int errval, const char *buffer,
                       ...)
{
    va_list ap;
    int len;

    va_start(ap, buffer);
    len = vsnprintf(doc->errstr, sizeof(doc->errstr) - 1, buffer, ap);
    va_end(ap);

    if (len < 0) {
        doc->errstr[0] = '\0';
        return errval;
    }

    if (len >= (int)(sizeof(doc->errstr) - 1))
        len = (int)(sizeof(doc->errstr) - 1);

    doc->errstr[len] = '\0';
    doc->errval = errval;

    return errval;
}

const char *pdf_get_err(const struct pdf_doc *pdf, int *errval)
{
    if (!pdf)
        return NULL;
    if (pdf->errstr[0] == '\0')
        return NULL;
    if (errval)
        *errval = pdf->errval;
    return pdf->errstr;
}

void pdf_clear_err(struct pdf_doc *pdf)
{
    if (!pdf)
        return;
    pdf->errstr[0] = '\0';
    pdf->errval = 0;
}

static int pdf_get_errval(const struct pdf_doc *pdf)
{
    if (!pdf)
        return 0;
    return pdf->errval;
}

static struct pdf_object *pdf_get_object(const struct pdf_doc *pdf, int index)
{
    return (struct pdf_object *)flexarray_get(&pdf->objects, index);
}

static int pdf_append_object(struct pdf_doc *pdf, struct pdf_object *obj)
{
    int index = flexarray_append(&pdf->objects, obj);

    if (index < 0)
        return index;
    obj->index = index;

    if (pdf->last_objects[obj->type]) {
        obj->prev = pdf->last_objects[obj->type];
        pdf->last_objects[obj->type]->next = obj;
    }
    pdf->last_objects[obj->type] = obj;

    if (!pdf->first_objects[obj->type])
        pdf->first_objects[obj->type] = obj;

    return 0;
}

static void pdf_object_destroy(struct pdf_object *object)
{
    if (!object)
        return;
    switch (object->type) {
    case OBJ_stream:
    case OBJ_image:
        dstr_free(&object->stream.stream);
        break;
    case OBJ_font:
        free(object->font.cmap_data);
        free(object->font.hmtx_data);
        break;
    case OBJ_cid_font:
        free(object->cid_font.glyph_widths);
        break;
    case OBJ_page:
        flexarray_clear(&object->page.children);
        flexarray_clear(&object->page.annotations);
        break;
    case OBJ_info:
        free(object->info);
        break;
    case OBJ_bookmark:
        flexarray_clear(&object->bookmark.children);
        break;
    }
    free(object);
}

static struct pdf_object *pdf_add_object(struct pdf_doc *pdf, int type)
{
    struct pdf_object *obj;

    if (!pdf)
        return NULL;

    obj = (struct pdf_object *)calloc(1, sizeof(*obj));
    if (!obj) {
        pdf_set_err(pdf, -errno,
                    "Unable to allocate object %d of type %d: %s",
                    flexarray_size(&pdf->objects) + 1, type, strerror(errno));
        return NULL;
    }

    obj->type = type;

    if (pdf_append_object(pdf, obj) < 0) {
        free(obj);
        return NULL;
    }

    return obj;
}

static void pdf_del_object(struct pdf_doc *pdf, struct pdf_object *obj)
{
    int type = obj->type;
    flexarray_set(&pdf->objects, obj->index, NULL);
    if (pdf->last_objects[type] == obj) {
        pdf->last_objects[type] = NULL;
        for (int i = 0; i < flexarray_size(&pdf->objects); i++) {
            struct pdf_object *o = pdf_get_object(pdf, i);
            if (o && o->type == type)
                pdf->last_objects[type] = o;
        }
    }

    if (pdf->first_objects[type] == obj) {
        pdf->first_objects[type] = NULL;
        for (int i = 0; i < flexarray_size(&pdf->objects); i++) {
            struct pdf_object *o = pdf_get_object(pdf, i);
            if (o && o->type == type) {
                pdf->first_objects[type] = o;
                break;
            }
        }
    }

    pdf_object_destroy(obj);
}

struct pdf_doc *pdf_create(float width, float height,
                           const struct pdf_info *info)
{
    struct pdf_doc *pdf;
    struct pdf_object *obj;

    pdf = (struct pdf_doc *)calloc(1, sizeof(*pdf));
    if (!pdf)
        return NULL;
    pdf->width = width;
    pdf->height = height;

    /* We don't want to use ID 0 */
    pdf_add_object(pdf, OBJ_none);

    /* Create the 'info' object */
    obj = pdf_add_object(pdf, OBJ_info);
    if (!obj) {
        pdf_destroy(pdf);
        return NULL;
    }
    obj->info = (struct pdf_info *)calloc(sizeof(*obj->info), 1);
    if (!obj->info) {
        pdf_destroy(pdf);
        return NULL;
    }
    if (info) {
        *obj->info = *info;
        obj->info->creator[sizeof(obj->info->creator) - 1] = '\0';
        obj->info->producer[sizeof(obj->info->producer) - 1] = '\0';
        obj->info->title[sizeof(obj->info->title) - 1] = '\0';
        obj->info->author[sizeof(obj->info->author) - 1] = '\0';
        obj->info->subject[sizeof(obj->info->subject) - 1] = '\0';
        obj->info->date[sizeof(obj->info->date) - 1] = '\0';
    }
    /* FIXME: Should be quoting PDF strings? */
    if (!obj->info->date[0]) {
        time_t now = time(NULL);
        struct tm tm;
#ifdef _WIN32
        const struct tm *tmp;
        tmp = localtime(&now);
        tm = *tmp;
#else
        localtime_r(&now, &tm);
#endif
        strftime(obj->info->date, sizeof(obj->info->date), "%Y%m%d%H%M%SZ",
                 &tm);
    }

    if (!pdf_add_object(pdf, OBJ_pages)) {
        pdf_destroy(pdf);
        return NULL;
    }
    if (!pdf_add_object(pdf, OBJ_catalog)) {
        pdf_destroy(pdf);
        return NULL;
    }

    if (pdf_set_font(pdf, "Times-Roman") < 0) {
        pdf_destroy(pdf);
        return NULL;
    }

    return pdf;
}

float pdf_width(const struct pdf_doc *pdf)
{
    return pdf->width;
}

float pdf_height(const struct pdf_doc *pdf)
{
    return pdf->height;
}

float pdf_page_width(const struct pdf_object *page)
{
    return page->page.width;
}

float pdf_page_height(const struct pdf_object *page)
{
    return page->page.height;
}

void pdf_destroy(struct pdf_doc *pdf)
{
    if (pdf) {
        for (int i = 0; i < flexarray_size(&pdf->objects); i++)
            pdf_object_destroy(pdf_get_object(pdf, i));
        flexarray_clear(&pdf->objects);
        free(pdf);
    }
}

static struct pdf_object *pdf_find_first_object(const struct pdf_doc *pdf,
                                                int type)
{
    if (!pdf)
        return NULL;
    return pdf->first_objects[type];
}

static struct pdf_object *pdf_find_last_object(const struct pdf_doc *pdf,
                                               int type)
{
    if (!pdf)
        return NULL;
    return pdf->last_objects[type];
}

int pdf_set_font(struct pdf_doc *pdf, const char *font)
{
    struct pdf_object *obj;
    int last_index = 0;
    bool found = false;

    // PDF requires one of the 14 standard fonts to be used, so check that
    // the font name is valid and canonicalise it to the correct case if it is
    for (size_t i = 0; i < sizeof(valid_fonts) / sizeof(valid_fonts[0]);
         i++) {
        if (strcasecmp(font, valid_fonts[i]) == 0) {
            font = valid_fonts[i];
            found = true;
            break;
        }
    }

    if (!found) {
        return pdf_set_err(pdf, -EINVAL, "Invalid font name '%s'", font);
    }

    /* See if we've used this font before */
    for (obj = pdf_find_first_object(pdf, OBJ_font); obj; obj = obj->next) {
        if (strcmp(obj->font.name, font) == 0)
            break;
        last_index = obj->font.index;
    }

    /* Create a new font object if we need it */
    if (!obj) {
        obj = pdf_add_object(pdf, OBJ_font);
        if (!obj)
            return pdf->errval;
        strncpy(obj->font.name, font, sizeof(obj->font.name) - 1);
        obj->font.name[sizeof(obj->font.name) - 1] = '\0';
        obj->font.index = last_index + 1;
    }

    pdf->current_font = obj;

    return 0;
}

const char *pdf_set_font_ttf(struct pdf_doc *pdf, const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        pdf_set_err(pdf, -errno, "Unable to open font file '%s': %s", path,
                    strerror(errno));
        return NULL;
    }
    const char *res = pdf_set_font_ttf_file(pdf, fp, path);
    fclose(fp);
    return res;
}

const char *pdf_set_font_ttf_file(struct pdf_doc *pdf, FILE *fp,
                                  const char *path)
{
    uint8_t *font_data;
    size_t font_data_len;
    struct pdf_object *font_obj, *descriptor_obj, *stream_obj, *cid_obj;
    int last_font_index = 0;
    uint32_t sfVersion;
    uint32_t head_len = 0, hhea_len = 0, hmtx_len = 0;
    uint32_t cmap_len = 0, os2_len = 0, post_len = 0, name_len = 0;
    const uint8_t *head, *hhea, *hmtx, *cmap, *os2, *post, *name_table;
    uint16_t units_per_em, numberOfHMetrics;
    int16_t xMin, yMin, xMax, yMax;
    uint16_t macStyle;
    int16_t ascender, descender;
    int16_t cap_height_val, ascender_os2, descender_os2;
    uint16_t fs_selection;
    float ascent, descent, cap_height, italic_angle, stem_v;
    int flags;
    char font_name[64];
    struct stat st;

    if (fstat(fileno(fp), &st) < 0) {
        pdf_set_err(pdf, -errno, "Unable to stat font file '%s': %s", path,
                    strerror(errno));
        return NULL;
    }
    font_data_len = (size_t)st.st_size;
    font_data = (uint8_t *)malloc(font_data_len);
    if (!font_data) {
        pdf_set_err(pdf, -ENOMEM,
                    "Unable to allocate %zu bytes for font '%s'",
                    font_data_len, path);
        return NULL;
    }
    if (fread(font_data, 1, font_data_len, fp) != font_data_len) {
        free(font_data);
        pdf_set_err(pdf, -EIO, "Unable to read font file '%s'", path);
        return NULL;
    }

    if (font_data_len < 12) {
        free(font_data);
        pdf_set_err(pdf, -EINVAL,
                    "Font file '%s' is too small to be a TrueType font",
                    path);
        return NULL;
    }
    sfVersion = ttf_be32(font_data);
    // TrueType fonts have sfVersion 0x00010000 or 'true' (0x74727565)
    if (sfVersion != 0x00010000u && sfVersion != 0x74727565u) {
        free(font_data);
        pdf_set_err(pdf, -EINVAL,
                    "File '%s' is not a TrueType font (version 0x%08x)", path,
                    sfVersion);
        return NULL;
    }

    head = ttf_find_table(font_data, font_data_len, "head", &head_len);
    hhea = ttf_find_table(font_data, font_data_len, "hhea", &hhea_len);
    hmtx = ttf_find_table(font_data, font_data_len, "hmtx", &hmtx_len);
    cmap = ttf_find_table(font_data, font_data_len, "cmap", &cmap_len);
    os2 = ttf_find_table(font_data, font_data_len, "OS/2", &os2_len);
    post = ttf_find_table(font_data, font_data_len, "post", &post_len);
    name_table = ttf_find_table(font_data, font_data_len, "name", &name_len);

    if (!head || head_len < 54 || !hhea || hhea_len < 36 || !hmtx || !cmap) {
        free(font_data);
        pdf_set_err(pdf, -EINVAL,
                    "Font '%s' is missing required TrueType tables", path);
        return NULL;
    }

    // Parse head table
    units_per_em = ttf_be16(head + 18);
    xMin = ttf_bes16(head + 36);
    yMin = ttf_bes16(head + 38);
    xMax = ttf_bes16(head + 40);
    yMax = ttf_bes16(head + 42);
    macStyle = ttf_be16(head + 44);
    if (units_per_em == 0) {
        free(font_data);
        pdf_set_err(pdf, -EINVAL, "Font '%s' has zero units_per_em", path);
        return NULL;
    }

    // Parse hhea table
    ascender = ttf_bes16(hhea + 4);
    descender = ttf_bes16(hhea + 6);
    numberOfHMetrics = ttf_be16(hhea + 34);

    // Parse OS/2 table (optional, provides better metrics when available)
    cap_height_val = 0;
    ascender_os2 = 0;
    descender_os2 = 0;
    fs_selection = 0;
    if (os2 && os2_len >= 78) {
        uint16_t os2_version = ttf_be16(os2);
        fs_selection = ttf_be16(os2 + 62);
        ascender_os2 = ttf_bes16(os2 + 68);  // sTypoAscender
        descender_os2 = ttf_bes16(os2 + 70); // sTypoDescender
        if (os2_version >= 2 && os2_len >= 90)
            cap_height_val = ttf_bes16(os2 + 88); // sCapHeight
    }

    // Compute ascent, descent, cap height in PDF units (1000/em)
    ascent = (float)(ascender_os2 != 0 ? ascender_os2 : ascender) * 1000.0f /
             (float)units_per_em;
    descent = (float)(descender_os2 != 0 ? descender_os2 : descender) *
              1000.0f / (float)units_per_em;
    cap_height = cap_height_val != 0
                     ? (float)cap_height_val * 1000.0f / (float)units_per_em
                     : ascent * 0.7f;

    // Parse post table for italic angle (16.16 fixed-point)
    italic_angle = 0.0f;
    if (post && post_len >= 12) {
        int32_t ia_fixed = (int32_t)ttf_be32(post + 4);
        italic_angle = (float)ia_fixed / 65536.0f;
    }

    // Compute font descriptor flags
    // Bit 6 (value 32): Nonsymbolic -- standard latin font
    // Bit 7 (value 64): Italic
    // Bit 1 (value  1): FixedPitch
    flags = 32; // Nonsymbolic
    if ((macStyle & 2u) || (fs_selection & 1u) || italic_angle != 0.0f)
        flags |= 64; // Italic
    if (post && post_len >= 16 && ttf_be32(post + 12))
        flags |= 1; // FixedPitch

    // Approximate StemV (vertical stem width) from weight
    stem_v = (macStyle & 1u) ? 120.0f : 80.0f;

    // Extract PostScript name from the name table, fall back to filename
    font_name[0] = '\0';
    if (name_table && name_len > 0)
        ttf_extract_name(name_table, (size_t)name_len, font_name,
                         sizeof(font_name));
    if (font_name[0] == '\0') {
        const char *base = strrchr(path, '/');
        const char *name_src = base ? base + 1 : path;
        strncpy(font_name, name_src, sizeof(font_name) - 1);
        font_name[sizeof(font_name) - 1] = '\0';
        char *dot = strrchr(font_name, '.');
        if (dot)
            *dot = '\0';
    }

    // Check whether this TTF font has already been loaded (reuse if so)
    for (struct pdf_object *obj = pdf_find_first_object(pdf, OBJ_font); obj;
         obj = obj->next) {
        if (obj->font.is_ttf && strcmp(obj->font.name, font_name) == 0) {
            free(font_data);
            pdf->current_font = obj;
            return obj->font.name;
        }
        last_font_index = obj->font.index;
    }

    // Find the best cmap format-4 subtable and make a copy for runtime
    // lookups
    uint16_t cmap_subtable_len = 0;
    const uint8_t *cmap_subtable =
        ttf_find_cmap_subtable(cmap, (size_t)cmap_len, &cmap_subtable_len);
    if (!cmap_subtable || cmap_subtable_len == 0 ||
        cmap_subtable_len > font_data_len) {
        free(font_data);
        pdf_set_err(pdf, -EINVAL, "Font '%s' has no usable cmap subtable",
                    path);
        return NULL;
    }
    uint8_t *cmap_copy = (uint8_t *)malloc(cmap_subtable_len);
    if (!cmap_copy) {
        free(font_data);
        pdf_set_err(pdf, -ENOMEM,
                    "Unable to allocate cmap subtable for font '%s'", path);
        return NULL;
    }
    // ensure subtable fits in cmap bounds safely before blind copy
    if (cmap_subtable_len > (cmap + cmap_len) - cmap_subtable) {
        free(cmap_copy);
        free(font_data);
        pdf_set_err(pdf, -EINVAL, "Font '%s' cmap subtable length invalid",
                    path);
        return NULL;
    }
    memcpy(cmap_copy, cmap_subtable, cmap_subtable_len);

    if (hmtx_len == 0 || hmtx_len > font_data_len) {
        free(cmap_copy);
        free(font_data);
        pdf_set_err(pdf, -EINVAL, "Font '%s' has invalid hmtx_len", path);
        return NULL;
    }

    // Make a copy of the hmtx table for runtime advance-width lookups
    uint8_t *hmtx_copy = (uint8_t *)malloc(hmtx_len);
    if (!hmtx_copy) {
        free(cmap_copy);
        free(font_data);
        pdf_set_err(pdf, -ENOMEM,
                    "Unable to allocate hmtx table for font '%s'", path);
        return NULL;
    }
    memcpy(hmtx_copy, hmtx, hmtx_len);

    // Build the glyph-width array for the CIDFont /W entry.
    // Widths are in PDF thousandths-of-em units.  We store one entry per
    // glyph ID in 0..numberOfHMetrics-1; glyphs beyond that share the last
    // advance width (/DW).
    if (numberOfHMetrics == 0 || numberOfHMetrics > font_data_len / 2) {
        free(hmtx_copy);
        free(cmap_copy);
        free(font_data);
        pdf_set_err(pdf, -EINVAL, "Font '%s' has invalid numberOfHMetrics",
                    path);
        return NULL;
    }
    uint16_t *glyph_widths =
        (uint16_t *)calloc(numberOfHMetrics, sizeof(uint16_t));
    if (!glyph_widths) {
        free(hmtx_copy);
        free(cmap_copy);
        free(font_data);
        pdf_set_err(pdf, -ENOMEM,
                    "Unable to allocate glyph widths for font '%s'", path);
        return NULL;
    }
    for (uint16_t g = 0; g < numberOfHMetrics; g++) {
        uint16_t advance =
            ttf_hmtx_advance(hmtx, hmtx_len, g, numberOfHMetrics);
        glyph_widths[g] =
            (uint16_t)((unsigned)advance * 1000u / (unsigned)units_per_em);
    }
    // Default width = advance of the last hmtx entry (applies to all glyphs
    // beyond numberOfHMetrics)
    int default_width = (int)glyph_widths[numberOfHMetrics - 1];

    // Create the embedded font file stream
    stream_obj = pdf_add_object(pdf, OBJ_stream);
    if (!stream_obj) {
        free(glyph_widths);
        free(hmtx_copy);
        free(cmap_copy);
        free(font_data);
        return NULL;
    }
    dstr_printf(&stream_obj->stream.stream,
                "<<\r\n"
                "  /Length %zu\r\n"
                "  /Length1 %zu\r\n"
                ">>stream\r\n",
                font_data_len, font_data_len);
    if (dstr_append_data(&stream_obj->stream.stream, font_data,
                         font_data_len) < 0) {
        free(glyph_widths);
        free(hmtx_copy);
        free(cmap_copy);
        free(font_data);
        pdf_set_err(pdf, -ENOMEM, "Unable to allocate font stream data");
        return NULL;
    }
    dstr_append(&stream_obj->stream.stream, "\r\nendstream\r\n");
    free(font_data);

    // Create the font descriptor object
    descriptor_obj = pdf_add_object(pdf, OBJ_font_descriptor);
    if (!descriptor_obj) {
        free(glyph_widths);
        free(hmtx_copy);
        free(cmap_copy);
        return NULL;
    }
    strncpy(descriptor_obj->font_descriptor.font_name, font_name,
            sizeof(descriptor_obj->font_descriptor.font_name) - 1);
    descriptor_obj->font_descriptor
        .font_name[sizeof(descriptor_obj->font_descriptor.font_name) - 1] =
        '\0';
    descriptor_obj->font_descriptor.flags = flags;
    descriptor_obj->font_descriptor.bbox[0] =
        (float)xMin * 1000.0f / (float)units_per_em;
    descriptor_obj->font_descriptor.bbox[1] =
        (float)yMin * 1000.0f / (float)units_per_em;
    descriptor_obj->font_descriptor.bbox[2] =
        (float)xMax * 1000.0f / (float)units_per_em;
    descriptor_obj->font_descriptor.bbox[3] =
        (float)yMax * 1000.0f / (float)units_per_em;
    descriptor_obj->font_descriptor.italic_angle = italic_angle;
    descriptor_obj->font_descriptor.ascent = ascent;
    descriptor_obj->font_descriptor.descent = descent;
    descriptor_obj->font_descriptor.cap_height = cap_height;
    descriptor_obj->font_descriptor.stem_v = stem_v;
    descriptor_obj->font_descriptor.font_file_index = stream_obj->index;

    // Create the CIDFont (Type2) descendant object
    cid_obj = pdf_add_object(pdf, OBJ_cid_font);
    if (!cid_obj) {
        free(glyph_widths);
        free(hmtx_copy);
        free(cmap_copy);
        return NULL;
    }
    strncpy(cid_obj->cid_font.font_name, font_name,
            sizeof(cid_obj->cid_font.font_name) - 1);
    cid_obj->cid_font.font_name[sizeof(cid_obj->cid_font.font_name) - 1] =
        '\0';
    cid_obj->cid_font.descriptor_index = descriptor_obj->index;
    cid_obj->cid_font.default_width = default_width;
    cid_obj->cid_font.glyph_widths = glyph_widths;
    cid_obj->cid_font.num_h_metrics = numberOfHMetrics;

    // Create the Type0 (composite) font dictionary object
    font_obj = pdf_add_object(pdf, OBJ_font);
    if (!font_obj) {
        free(hmtx_copy);
        free(cmap_copy);
        return NULL;
    }
    strncpy(font_obj->font.name, font_name, sizeof(font_obj->font.name) - 1);
    font_obj->font.name[sizeof(font_obj->font.name) - 1] = '\0';
    font_obj->font.index = last_font_index + 1;
    font_obj->font.is_ttf = true;
    font_obj->font.cmap_data = cmap_copy;
    font_obj->font.cmap_data_len = cmap_subtable_len;
    font_obj->font.hmtx_data = hmtx_copy;
    font_obj->font.hmtx_data_len = hmtx_len;
    font_obj->font.num_h_metrics = numberOfHMetrics;
    font_obj->font.units_per_em = units_per_em;
    font_obj->font.descriptor_index = descriptor_obj->index;
    font_obj->font.cid_font_index = cid_obj->index;

    pdf->current_font = font_obj;
    return font_obj->font.name;
}

struct pdf_object *pdf_append_page(struct pdf_doc *pdf)
{
    struct pdf_object *page;

    page = pdf_add_object(pdf, OBJ_page);

    if (!page)
        return NULL;

    page->page.width = pdf->width;
    page->page.height = pdf->height;

    return page;
}

struct pdf_object *pdf_get_page(struct pdf_doc *pdf, int page_number)
{
    if (page_number <= 0) {
        pdf_set_err(pdf, -EINVAL, "page number must be >= 1");
        return NULL;
    }

    for (struct pdf_object *obj = pdf_find_first_object(pdf, OBJ_page); obj;
         obj = obj->next, page_number--) {
        if (page_number == 1) {
            return obj;
        }
    }

    pdf_set_err(pdf, -EINVAL, "no such page");
    return NULL;
}

int pdf_page_set_size(struct pdf_doc *pdf, struct pdf_object *page,
                      float width, float height)
{
    if (!page)
        page = pdf_find_last_object(pdf, OBJ_page);

    if (!page || page->type != OBJ_page)
        return pdf_set_err(pdf, -EINVAL, "Invalid PDF page");
    page->page.width = width;
    page->page.height = height;
    return 0;
}

// Recursively scan for the number of children
static int pdf_get_bookmark_count(const struct pdf_object *obj)
{
    int count = 0;
    if (obj->type == OBJ_bookmark) {
        int nchildren = flexarray_size(&obj->bookmark.children);
        count += nchildren;
        for (int i = 0; i < nchildren; i++) {
            count += pdf_get_bookmark_count(
                (const struct pdf_object *)flexarray_get(
                    &obj->bookmark.children, i));
        }
    }
    return count;
}

/* Forward declarations for encryption helpers used by
 * pdf_save_object_internal
 */
#define PDF_CRYPT_KEY_LEN 5 /* 40-bit key = 5 bytes */
struct pdf_crypt {
    uint8_t file_key[PDF_CRYPT_KEY_LEN];
};
static bool pdf_crypt_write_string(const struct pdf_crypt *crypt,
                                   int obj_index, const char *str, FILE *fp);
static bool pdf_crypt_write_stream(const struct pdf_crypt *crypt,
                                   int obj_index, const struct dstr *stream,
                                   FILE *fp);
static void pdf_crypt_data(const struct pdf_crypt *crypt, int obj_index,
                           uint8_t *data, size_t len);

static void pdf_print_escaped_string(FILE *fp, const char *str)
{
    while (*str) {
        if (*str == '(' || *str == ')' || *str == '\\')
            fputc('\\', fp);
        fputc(*str, fp);
        str++;
    }
}

/*
 * Write a PDF string value to fp.
 * If crypt is non-NULL, the string is RC4-encrypted and written as
 * <hexbytes>. Otherwise it is written as a literal (escaped) string (...).
 * Returns 0 on success, < 0 on error (pdf error is set).
 */
static int pdf_write_string_val(struct pdf_doc *pdf, FILE *fp, int obj_index,
                                const char *str,
                                const struct pdf_crypt *crypt)
{
    if (crypt) {
        if (!pdf_crypt_write_string(crypt, obj_index, str, fp))
            return pdf_set_err(pdf, -ENOMEM,
                               "Out of memory encrypting string");
    } else {
        fputc('(', fp);
        pdf_print_escaped_string(fp, str);
        fputc(')', fp);
    }
    return 0;
}

/*
 * Internal object-save routine shared by encrypted and unencrypted paths.
 * When crypt is NULL the object is written in plain text (PDF literal strings
 * and raw stream bytes).  When crypt is non-NULL, string values are encrypted
 * and written as hex strings, and stream payload bytes are RC4-encrypted.
 */
static int pdf_save_object_internal(struct pdf_doc *pdf, FILE *fp, int index,
                                    const struct pdf_crypt *crypt)
{
    struct pdf_object *object = pdf_get_object(pdf, index);
    if (!object)
        return -ENOENT;

    if (object->type == OBJ_none)
        return -ENOENT;

    object->offset = ftell(fp);

    fprintf(fp, "%d 0 obj\r\n", index);

    switch (object->type) {
    case OBJ_stream:
    case OBJ_image: {
        if (crypt) {
            if (!pdf_crypt_write_stream(crypt, index, &object->stream.stream,
                                        fp))
                return pdf_set_err(pdf, -ENOMEM,
                                   "Out of memory encrypting stream %d",
                                   index);
        } else {
            fwrite(dstr_data(&object->stream.stream),
                   dstr_len(&object->stream.stream), 1, fp);
        }
        break;
    }
    case OBJ_info: {
        const struct pdf_info *info = object->info;
        int e;

        fprintf(fp, "<<\r\n");
        if (info->creator[0]) {
            fprintf(fp, "  /Creator ");
            if ((e = pdf_write_string_val(pdf, fp, index, info->creator,
                                          crypt)) < 0)
                return e;
            fprintf(fp, "\r\n");
        }
        if (info->producer[0]) {
            fprintf(fp, "  /Producer ");
            if ((e = pdf_write_string_val(pdf, fp, index, info->producer,
                                          crypt)) < 0)
                return e;
            fprintf(fp, "\r\n");
        }
        if (info->title[0]) {
            fprintf(fp, "  /Title ");
            if ((e = pdf_write_string_val(pdf, fp, index, info->title,
                                          crypt)) < 0)
                return e;
            fprintf(fp, "\r\n");
        }
        if (info->author[0]) {
            fprintf(fp, "  /Author ");
            if ((e = pdf_write_string_val(pdf, fp, index, info->author,
                                          crypt)) < 0)
                return e;
            fprintf(fp, "\r\n");
        }
        if (info->subject[0]) {
            fprintf(fp, "  /Subject ");
            if ((e = pdf_write_string_val(pdf, fp, index, info->subject,
                                          crypt)) < 0)
                return e;
            fprintf(fp, "\r\n");
        }
        if (info->date[0]) {
            if (crypt) {
                /* Encrypt the date string with its "D:" prefix */
                size_t dlen = strlen(info->date);
                uint8_t *dbuf = (uint8_t *)malloc(dlen + 2);
                if (!dbuf)
                    return pdf_set_err(
                        pdf, -ENOMEM, "Out of memory encrypting date string");
                dbuf[0] = 'D';
                dbuf[1] = ':';
                memcpy(dbuf + 2, info->date, dlen);
                pdf_crypt_data(crypt, index, dbuf, dlen + 2);
                fprintf(fp, "  /CreationDate <");
                for (size_t i = 0; i < dlen + 2; i++)
                    fprintf(fp, "%02x", dbuf[i]);
                fprintf(fp, ">\r\n");
                free(dbuf);
            } else {
                fprintf(fp, "  /CreationDate (D:");
                pdf_print_escaped_string(fp, info->date);
                fprintf(fp, ")\r\n");
            }
        }
        fprintf(fp, ">>\r\n");
        break;
    }

    case OBJ_page: {
        const struct pdf_object *pages =
            pdf_find_first_object(pdf, OBJ_pages);
        bool printed_xobjects = false;

        fprintf(fp,
                "<<\r\n"
                "  /Type /Page\r\n"
                "  /Parent %d 0 R\r\n",
                pages->index);
        fprintf(fp, "  /MediaBox [0 0 %f %f]\r\n", object->page.width,
                object->page.height);
        fprintf(fp, "  /Resources <<\r\n");
        fprintf(fp, "    /Font <<\r\n");
        for (struct pdf_object *font = pdf_find_first_object(pdf, OBJ_font);
             font; font = font->next)
            fprintf(fp, "      /F%d %d 0 R\r\n", font->font.index,
                    font->index);
        fprintf(fp, "    >>\r\n");
        // We trim transparency to just 4-bits
        fprintf(fp, "    /ExtGState <<\r\n");
        for (int i = 0; i < 16; i++) {
            fprintf(fp, "      /GS%d <</ca %f>>\r\n", i,
                    (float)(15 - i) / 15);
        }
        fprintf(fp, "    >>\r\n");

        for (struct pdf_object *image = pdf_find_first_object(pdf, OBJ_image);
             image; image = image->next) {
            if (image->stream.page == object) {
                if (!printed_xobjects) {
                    fprintf(fp, "    /XObject <<");
                    printed_xobjects = true;
                }
                fprintf(fp, "      /Image%d %d 0 R ", image->index,
                        image->index);
            }
        }
        if (printed_xobjects)
            fprintf(fp, "    >>\r\n");
        fprintf(fp, "  >>\r\n");

        fprintf(fp, "  /Contents [\r\n");
        for (int i = 0; i < flexarray_size(&object->page.children); i++) {
            const struct pdf_object *child =
                (const struct pdf_object *)flexarray_get(
                    &object->page.children, i);
            fprintf(fp, "%d 0 R\r\n", child->index);
        }
        fprintf(fp, "]\r\n");

        if (flexarray_size(&object->page.annotations)) {
            fprintf(fp, "  /Annots [\r\n");
            for (int i = 0; i < flexarray_size(&object->page.annotations);
                 i++) {
                const struct pdf_object *child =
                    (const struct pdf_object *)flexarray_get(
                        &object->page.annotations, i);
                fprintf(fp, "%d 0 R\r\n", child->index);
            }
            fprintf(fp, "]\r\n");
        }

        fprintf(fp, ">>\r\n");
        break;
    }

    case OBJ_bookmark: {
        const struct pdf_object *parent, *other;
        int e;

        parent = object->bookmark.parent;
        if (!parent)
            parent = pdf_find_first_object(pdf, OBJ_outline);
        if (!object->bookmark.page)
            break;
        fprintf(fp,
                "<<\r\n"
                "  /Dest [%d 0 R /XYZ 0 %f null]\r\n"
                "  /Parent %d 0 R\r\n"
                "  /Title ",
                object->bookmark.page->index, pdf->height, parent->index);
        if ((e = pdf_write_string_val(pdf, fp, index, object->bookmark.name,
                                      crypt)) < 0)
            return e;
        fprintf(fp, "\r\n");
        int nchildren = flexarray_size(&object->bookmark.children);
        if (nchildren > 0) {
            const struct pdf_object *f, *l;
            f = (const struct pdf_object *)flexarray_get(
                &object->bookmark.children, 0);
            l = (const struct pdf_object *)flexarray_get(
                &object->bookmark.children, nchildren - 1);
            fprintf(fp, "  /First %d 0 R\r\n", f->index);
            fprintf(fp, "  /Last %d 0 R\r\n", l->index);
            fprintf(
                fp, "  /Count %d\r\n",
                pdf_get_bookmark_count((const struct pdf_object *)object));
        }
        // Find the previous bookmark with the same parent
        for (other = object->prev;
             other && other->bookmark.parent != object->bookmark.parent;
             other = other->prev)
            ;
        if (other)
            fprintf(fp, "  /Prev %d 0 R\r\n", other->index);
        // Find the next bookmark with the same parent
        for (other = object->next;
             other && other->bookmark.parent != object->bookmark.parent;
             other = other->next)
            ;
        if (other)
            fprintf(fp, "  /Next %d 0 R\r\n", other->index);
        fprintf(fp, ">>\r\n");
        break;
    }

    case OBJ_outline: {
        const struct pdf_object *first, *last, *cur;
        first = pdf_find_first_object(pdf, OBJ_bookmark);
        last = pdf_find_last_object(pdf, OBJ_bookmark);

        if (first && last) {
            int count = 0;
            cur = first;
            while (cur) {
                if (!cur->bookmark.parent)
                    count += pdf_get_bookmark_count(cur) + 1;
                cur = cur->next;
            }

            /* Bookmark outline */
            fprintf(fp,
                    "<<\r\n"
                    "  /Count %d\r\n"
                    "  /Type /Outlines\r\n"
                    "  /First %d 0 R\r\n"
                    "  /Last %d 0 R\r\n"
                    ">>\r\n",
                    count, first->index, last->index);
        }
        break;
    }

    case OBJ_font:
        if (object->font.is_ttf) {
            // Type0 (composite) font using Identity-H encoding so that
            // arbitrary Unicode text can be represented as 2-byte glyph IDs.
            fprintf(fp,
                    "<<\r\n"
                    "  /Type /Font\r\n"
                    "  /Subtype /Type0\r\n"
                    "  /BaseFont /%s\r\n"
                    "  /Encoding /Identity-H\r\n"
                    "  /DescendantFonts [%d 0 R]\r\n"
                    ">>\r\n",
                    object->font.name, object->font.cid_font_index);
        } else {
            fprintf(fp,
                    "<<\r\n"
                    "  /Type /Font\r\n"
                    "  /Subtype /Type1\r\n"
                    "  /BaseFont /%s\r\n"
                    "  /Encoding /WinAnsiEncoding\r\n"
                    ">>\r\n",
                    object->font.name);
        }
        break;

    case OBJ_cid_font: {
        // CIDFontType2 descendant for the Type0 font.
        // /W specifies per-glyph advance widths in thousandths-of-em for
        // glyph IDs 0 .. num_h_metrics-1; remaining glyphs use /DW.
        fprintf(fp,
                "<<\r\n"
                "  /Type /Font\r\n"
                "  /Subtype /CIDFontType2\r\n"
                "  /BaseFont /%s\r\n"
                "  /CIDSystemInfo <<\r\n"
                "    /Registry (Adobe)\r\n"
                "    /Ordering (Identity)\r\n"
                "    /Supplement 0\r\n"
                "  >>\r\n"
                "  /DW %d\r\n"
                "  /W [0 [",
                object->cid_font.font_name, object->cid_font.default_width);
        for (uint16_t g = 0; g < object->cid_font.num_h_metrics; g++)
            fprintf(fp, " %d", (int)object->cid_font.glyph_widths[g]);
        fprintf(fp,
                " ]]\r\n"
                "  /FontDescriptor %d 0 R\r\n"
                "  /CIDToGIDMap /Identity\r\n"
                ">>\r\n",
                object->cid_font.descriptor_index);
        break;
    }

    case OBJ_font_descriptor:
        fprintf(
            fp,
            "<<\r\n"
            "  /Type /FontDescriptor\r\n"
            "  /FontName /%s\r\n"
            "  /Flags %d\r\n"
            "  /FontBBox [%g %g %g %g]\r\n"
            "  /ItalicAngle %g\r\n"
            "  /Ascent %g\r\n"
            "  /Descent %g\r\n"
            "  /CapHeight %g\r\n"
            "  /StemV %g\r\n"
            "  /FontFile2 %d 0 R\r\n"
            ">>\r\n",
            object->font_descriptor.font_name, object->font_descriptor.flags,
            object->font_descriptor.bbox[0], object->font_descriptor.bbox[1],
            object->font_descriptor.bbox[2], object->font_descriptor.bbox[3],
            object->font_descriptor.italic_angle,
            object->font_descriptor.ascent, object->font_descriptor.descent,
            object->font_descriptor.cap_height,
            object->font_descriptor.stem_v,
            object->font_descriptor.font_file_index);
        break;

    case OBJ_pages: {
        int npages = 0;

        fprintf(fp, "<<\r\n"
                    "  /Type /Pages\r\n"
                    "  /Kids [ ");
        for (const struct pdf_object *page =
                 pdf_find_first_object(pdf, OBJ_page);
             page; page = page->next) {
            npages++;
            fprintf(fp, "%d 0 R ", page->index);
        }
        fprintf(fp, "]\r\n");
        fprintf(fp, "  /Count %d\r\n", npages);
        fprintf(fp, ">>\r\n");
        break;
    }

    case OBJ_catalog: {
        const struct pdf_object *outline =
            pdf_find_first_object(pdf, OBJ_outline);
        const struct pdf_object *pages =
            pdf_find_first_object(pdf, OBJ_pages);

        fprintf(fp, "<<\r\n"
                    "  /Type /Catalog\r\n");
        if (outline)
            fprintf(fp,
                    "  /Outlines %d 0 R\r\n"
                    "  /PageMode /UseOutlines\r\n",
                    outline->index);
        fprintf(fp,
                "  /Pages %d 0 R\r\n"
                ">>\r\n",
                pages->index);
        break;
    }

    case OBJ_link: {
        fprintf(fp,
                "<<\r\n"
                "  /Type /Annot\r\n"
                "  /Subtype /Link\r\n"
                "  /Rect [%f %f %f %f]\r\n"
                "  /Dest [%d 0 R /XYZ %f %f null]\r\n"
                "  /Border [0 0 0]\r\n"
                ">>\r\n",
                object->link.llx, object->link.lly, object->link.urx,
                object->link.ury, object->link.target_page->index,
                object->link.target_x, object->link.target_y);
        break;
    }

    default:
        return pdf_set_err(pdf, -EINVAL, "Invalid PDF object type %d",
                           object->type);
    }

    fprintf(fp, "endobj\r\n");

    return 0;
}

// Slightly modified djb2 hash algorithm to get pseudo-random ID
static uint64_t hash(uint64_t hash, const void *data, size_t len)
{
    const uint8_t *d8 = (const uint8_t *)data;
    for (; len; len--) {
        hash = (((hash & 0x03ffffffffffffff) << 5) +
                (hash & 0x7fffffffffffffff)) +
               *d8++;
    }
    return hash;
}

/* ========== PDF Standard Security Handler (Rev 2, 40-bit RC4) ========== */

/* Standard padding string defined in the PDF specification */
static const uint8_t pdf_crypt_padding[32] = {
    0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41, 0x64, 0x00, 0x4E,
    0x56, 0xFF, 0xFA, 0x01, 0x08, 0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68,
    0x3E, 0x80, 0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A};

/* ----- MD5 ----------------------------------------------------------------
 */

#define PDF_MD5_ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void pdf_md5(const uint8_t *data, size_t len, uint8_t digest[16])
{
    static const uint32_t T[64] = {
        0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu,
        0x4787c62au, 0xa8304613u, 0xfd469501u, 0x698098d8u, 0x8b44f7afu,
        0xffff5bb1u, 0x895cd7beu, 0x6b901122u, 0xfd987193u, 0xa679438eu,
        0x49b40821u, 0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
        0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u, 0x21e1cde6u,
        0xc33707d6u, 0xf4d50d87u, 0x455a14edu, 0xa9e3e905u, 0xfcefa3f8u,
        0x676f02d9u, 0x8d2a4c8au, 0xfffa3942u, 0x8771f681u, 0x6d9d6122u,
        0xfde5380cu, 0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
        0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u, 0xd9d4d039u,
        0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u, 0xf4292244u, 0x432aff97u,
        0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u, 0xffeff47du,
        0x85845dd1u, 0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
        0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u};
    static const uint8_t s[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

    uint32_t a0 = 0x67452301u, b0 = 0xefcdab89u, c0 = 0x98badcfeu,
             d0 = 0x10325476u;

    /* Process data in 512-bit (64-byte) blocks, with padding */
    size_t padded = ((len + 8) / 64 + 1) * 64;
    uint8_t *buf = (uint8_t *)calloc(padded, 1);
    if (!buf) {
        memset(digest, 0, 16);
        return;
    }
    memcpy(buf, data, len);
    buf[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++)
        buf[padded - 8 + i] = (uint8_t)(bits >> (8 * i));

    for (size_t offset = 0; offset < padded; offset += 64) {
        uint32_t M[16];
        for (int i = 0; i < 16; i++) {
            M[i] = (uint32_t)buf[offset + i * 4] |
                   ((uint32_t)buf[offset + i * 4 + 1] << 8) |
                   ((uint32_t)buf[offset + i * 4 + 2] << 16) |
                   ((uint32_t)buf[offset + i * 4 + 3] << 24);
        }
        uint32_t A = a0, B = b0, C = c0, D = d0;
        for (int i = 0; i < 64; i++) {
            uint32_t F, g;
            if (i < 16) {
                F = (B & C) | (~B & D);
                g = (uint32_t)i;
            } else if (i < 32) {
                F = (D & B) | (~D & C);
                g = (uint32_t)(5 * i + 1) % 16;
            } else if (i < 48) {
                F = B ^ C ^ D;
                g = (uint32_t)(3 * i + 5) % 16;
            } else {
                F = C ^ (B | ~D);
                g = (uint32_t)(7 * i) % 16;
            }
            F = F + A + T[i] + M[g];
            A = D;
            D = C;
            C = B;
            B = B + PDF_MD5_ROTL(F, s[i]);
        }
        a0 += A;
        b0 += B;
        c0 += C;
        d0 += D;
    }
    free(buf);

    for (int i = 0; i < 4; i++) {
        digest[i] = (uint8_t)(a0 >> (8 * i));
        digest[4 + i] = (uint8_t)(b0 >> (8 * i));
        digest[8 + i] = (uint8_t)(c0 >> (8 * i));
        digest[12 + i] = (uint8_t)(d0 >> (8 * i));
    }
}

/* ----- RC4 ----------------------------------------------------------------
 */

static void pdf_rc4(const uint8_t *key, size_t keylen, uint8_t *data,
                    size_t datalen)
{
    uint8_t S[256];
    uint8_t j = 0;
    for (int i = 0; i < 256; i++)
        S[i] = (uint8_t)i;
    for (int i = 0; i < 256; i++) {
        j = (uint8_t)((j + S[i] + key[i % keylen]) & 0xff);
        uint8_t tmp = S[i];
        S[i] = S[j];
        S[j] = tmp;
    }
    uint8_t ii = 0;
    j = 0;
    for (size_t k = 0; k < datalen; k++) {
        ii = (uint8_t)((ii + 1) & 0xff);
        j = (uint8_t)((j + S[ii]) & 0xff);
        uint8_t tmp = S[ii];
        S[ii] = S[j];
        S[j] = tmp;
        data[k] ^= S[(S[ii] + S[j]) & 0xff];
    }
}

/* ----- PDF 40-bit encryption key derivation ------------------------------
 */

/* Pad or truncate password to 32 bytes using the PDF padding string */
static void pdf_crypt_pad_password(const char *pwd, uint8_t out[32])
{
    size_t plen = pwd ? strlen(pwd) : 0;
    if (plen > 32)
        plen = 32;
    if (pwd && plen > 0)
        memcpy(out, pwd, plen);
    if (plen < 32)
        memcpy(out + plen, pdf_crypt_padding, 32 - plen);
}

/*
 * Compute the encryption keys for PDF Standard Security Handler Rev. 2.
 * user_password : the "open" password (may be NULL/"" for no user password)
 * file_id       : first element of /ID array (exactly 8 bytes)
 * crypt         : output - encryption context (file key)
 * out_owner_key : output - 32-byte /O value for the encryption dictionary
 * out_user_key  : output - 32-byte /U value for the encryption dictionary
 * permissions   : /P value (signed 32-bit, stored little-endian)
 */
static void
pdf_crypt_compute_keys(const char *user_password, const uint8_t file_id[8],
                       int32_t permissions, struct pdf_crypt *crypt,
                       uint8_t out_owner_key[32], uint8_t out_user_key[32])
{
    uint8_t padded_user[32];
    pdf_crypt_pad_password(user_password, padded_user);

    /* Step 1: compute /O (owner key) */
    /* MD5 of padded user password, take first 5 bytes as RC4 key */
    uint8_t md5_out[16];
    pdf_md5(padded_user, 32, md5_out);
    /* RC4-encrypt the padded user password with the 5-byte key */
    memcpy(out_owner_key, padded_user, 32);
    pdf_rc4(md5_out, PDF_CRYPT_KEY_LEN, out_owner_key, 32);

    /* Step 2: compute file encryption key */
    /* MD5 of: padded_user + owner_key + P (LE 4 bytes) + file_id (8 bytes) */
    uint8_t key_input[32 + 32 + 4 + 8];
    memcpy(key_input, padded_user, 32);
    memcpy(key_input + 32, out_owner_key, 32);
    key_input[64] = (uint8_t)(permissions & 0xff);
    key_input[65] = (uint8_t)((permissions & 0xff00) >> 8);
    key_input[66] = (uint8_t)((permissions & 0xff0000) >> 16);
    key_input[67] = (uint8_t)((permissions & 0xff000000) >> 24);
    memcpy(key_input + 68, file_id, 8);
    pdf_md5(key_input, 32 + 32 + 4 + 8, md5_out);
    memcpy(crypt->file_key, md5_out, PDF_CRYPT_KEY_LEN);

    /* Step 3: compute /U (user key) */
    /* RC4-encrypt the padding string with the file encryption key */
    memcpy(out_user_key, pdf_crypt_padding, 32);
    pdf_rc4(crypt->file_key, PDF_CRYPT_KEY_LEN, out_user_key, 32);
}

/*
 * Derive the per-object RC4 key for object at obj_index (generation 0).
 * out_key must point to a buffer of at least PDF_CRYPT_KEY_LEN + 5 bytes.
 * Returns the key length (10 bytes for 40-bit base key).
 */
static size_t pdf_crypt_object_key(const struct pdf_crypt *crypt,
                                   int obj_index, uint8_t out_key[16])
{
    uint8_t key_in[PDF_CRYPT_KEY_LEN + 5];
    memcpy(key_in, crypt->file_key, PDF_CRYPT_KEY_LEN);
    key_in[PDF_CRYPT_KEY_LEN + 0] = (uint8_t)(obj_index & 0xff);
    key_in[PDF_CRYPT_KEY_LEN + 1] = (uint8_t)((obj_index >> 8) & 0xff);
    key_in[PDF_CRYPT_KEY_LEN + 2] = (uint8_t)((obj_index >> 16) & 0xff);
    key_in[PDF_CRYPT_KEY_LEN + 3] = 0; /* generation number low byte */
    key_in[PDF_CRYPT_KEY_LEN + 4] = 0; /* generation number high byte */
    uint8_t md5_out[16];
    pdf_md5(key_in, sizeof(key_in), md5_out);
    size_t keylen = PDF_CRYPT_KEY_LEN + 5; /* min(n+5, 16) = 10 for 40-bit */
    memcpy(out_key, md5_out, keylen);
    return keylen;
}

/*
 * Encrypt (or decrypt - RC4 is symmetric) data in-place for a given object.
 */
static void pdf_crypt_data(const struct pdf_crypt *crypt, int obj_index,
                           uint8_t *data, size_t len)
{
    uint8_t key[16];
    size_t keylen = pdf_crypt_object_key(crypt, obj_index, key);
    pdf_rc4(key, keylen, data, len);
}

/*
 * Encrypt a string and write it to fp as a PDF hex string <hexbytes>.
 * Returns false on allocation failure (caller should treat the PDF as
 * invalid).
 */
static bool pdf_crypt_write_string(const struct pdf_crypt *crypt,
                                   int obj_index, const char *str, FILE *fp)
{
    size_t slen = strlen(str);
    uint8_t *buf = (uint8_t *)malloc(slen + 1);
    if (!buf)
        return false;
    memcpy(buf, str, slen);
    pdf_crypt_data(crypt, obj_index, buf, slen);
    fputc('<', fp);
    for (size_t i = 0; i < slen; i++)
        fprintf(fp, "%02x", buf[i]);
    fputc('>', fp);
    free(buf);
    return true;
}

/*
 * Write a stream object (OBJ_stream or OBJ_image) to fp with the stream
 * content RC4-encrypted.  The dictionary header and endstream trailer are
 * written verbatim; only the payload bytes are encrypted.
 * Returns false on allocation failure (content is skipped, not written in
 * plain text).
 */
static bool pdf_crypt_write_stream(const struct pdf_crypt *crypt,
                                   int obj_index, const struct dstr *stream,
                                   FILE *fp)
{
    const char *raw = stream->data ? stream->data : stream->static_data;
    size_t total = dstr_len(stream);
    const char *stream_marker = "stream\r\n";
    size_t marker_len = strlen(stream_marker);
    const char *footer = "\r\nendstream\r\n";
    size_t footer_len = strlen(footer);

    /* Find the first "stream\r\n" — marks the end of the dictionary header */
    const char *hdr_end = strstr(raw, stream_marker);
    if (!hdr_end || total < footer_len) {
        /* Unexpected format: write empty stream to avoid leaking data */
        fwrite(raw, 1, (size_t)(hdr_end ? hdr_end - raw + marker_len : 0),
               fp);
        fwrite(footer, 1, footer_len, fp);
        return false;
    }

    size_t hdr_len = (size_t)(hdr_end - raw) + marker_len;
    size_t content_len = total - hdr_len - footer_len;

    /* Write the dictionary header unchanged */
    fwrite(raw, 1, hdr_len, fp);

    /* Encrypt the payload and write it */
    uint8_t *buf = (uint8_t *)malloc(content_len + 1);
    if (!buf) {
        /* Encryption failed due to OOM - skip remaining content to avoid
         * writing unencrypted stream data */
        fwrite(footer, 1, footer_len, fp);
        return false;
    }
    memcpy(buf, raw + hdr_len, content_len);
    pdf_crypt_data(crypt, obj_index, buf, content_len);
    fwrite(buf, 1, content_len, fp);
    free(buf);

    /* Write the endstream trailer unchanged */
    fwrite(footer, 1, footer_len, fp);
    return true;
}

/*
 * Internal implementation used by both pdf_save_file and
 * pdf_save_file_encrypted.  When crypt is NULL the file is written as a
 * plain PDF (PDF 1.3).  When crypt is non-NULL the stream and string content
 * is RC4-encrypted, an encryption dictionary object is appended, and the
 * trailer references it (PDF 1.4).
 *
 * file_id is only used when crypt is non-NULL: it must be the same 8 bytes
 * that were passed to pdf_crypt_compute_keys, so the trailer /ID element
 * matches the bytes used for key derivation.
 */
static int pdf_save_file_internal(struct pdf_doc *pdf, FILE *fp,
                                  const struct pdf_crypt *crypt,
                                  const uint8_t *owner_key,
                                  const uint8_t *user_key,
                                  int32_t permissions, const uint8_t *file_id)
{
    int xref_offset;
    int xref_count = 0;
    uint64_t id2;
    time_t now = time(NULL);
    char saved_locale[32];

    force_locale(saved_locale, sizeof(saved_locale));

    fprintf(fp, crypt ? "%%PDF-1.4\r\n" : "%%PDF-1.3\r\n");
    /* Hibit bytes */
    fprintf(fp, "%c%c%c%c%c\r\n", 0x25, 0xc7, 0xec, 0x8f, 0xa2);

    /* Write all objects (encrypted or plain) */
    for (int i = 0; i < flexarray_size(&pdf->objects); i++)
        if (pdf_save_object_internal(pdf, fp, i, crypt) >= 0)
            xref_count++;

    /* For encrypted output: append the encryption dictionary object */
    int encrypt_obj_index = flexarray_size(&pdf->objects);
    int encrypt_obj_offset = 0;
    if (crypt) {
        encrypt_obj_offset = ftell(fp);
        fprintf(fp, "%d 0 obj\r\n", encrypt_obj_index);
        fprintf(fp, "<<\r\n"
                    "  /Filter /Standard\r\n"
                    "  /V 1\r\n"
                    "  /R 2\r\n"
                    "  /O <");
        for (int i = 0; i < 32; i++)
            fprintf(fp, "%02x", owner_key[i]);
        fprintf(fp, ">\r\n  /U <");
        for (int i = 0; i < 32; i++)
            fprintf(fp, "%02x", user_key[i]);
        fprintf(fp, ">\r\n  /P %d\r\n", permissions);
        fprintf(fp, ">>\r\nendobj\r\n");
        xref_count++;
    }

    /* Cross-reference table */
    xref_offset = ftell(fp);
    fprintf(fp, "xref\r\n");
    fprintf(fp, "0 %d\r\n", xref_count + 1);
    fprintf(fp, "0000000000 65535 f\r\n");
    for (int i = 0; i < flexarray_size(&pdf->objects); i++) {
        const struct pdf_object *obj = pdf_get_object(pdf, i);
        if (obj && obj->type != OBJ_none)
            fprintf(fp, "%10.10d 00000 n\r\n", obj->offset);
    }
    if (crypt)
        fprintf(fp, "%10.10d 00000 n\r\n", encrypt_obj_offset);

    /* Trailer */
    const struct pdf_object *info_obj = pdf_find_first_object(pdf, OBJ_info);
    const struct pdf_object *catalog =
        pdf_find_first_object(pdf, OBJ_catalog);
    id2 = hash(5381, &now, sizeof(now));
    fprintf(fp,
            "trailer\r\n"
            "<<\r\n"
            "/Size %d\r\n",
            xref_count + 1);
    fprintf(fp, "/Root %d 0 R\r\n", catalog->index);
    fprintf(fp, "/Info %d 0 R\r\n", info_obj->index);
    if (crypt) {
        fprintf(fp, "/Encrypt %d 0 R\r\n", encrypt_obj_index);
        /* Write the /ID using the exact file_id bytes used for key derivation
         * (8 bytes each element). Use same bytes for both elements. */
        fprintf(fp, "/ID [<");
        for (int i = 0; i < 8; i++)
            fprintf(fp, "%02x", file_id[i]);
        fprintf(fp, "> <");
        for (int i = 0; i < 8; i++)
            fprintf(fp, "%02x", file_id[i]);
        fprintf(fp, ">]\r\n");
    } else {
        uint64_t id1;
        id1 = hash(5381, info_obj->info, sizeof(struct pdf_info));
        id1 = hash(id1, &xref_count, sizeof(xref_count));
        fprintf(fp, "/ID [<%016" PRIx64 "> <%016" PRIx64 ">]\r\n", id1, id2);
    }
    fprintf(fp, ">>\r\nstartxref\r\n");
    fprintf(fp, "%d\r\n", xref_offset);
    fprintf(fp, "%%%%EOF\r\n");

    restore_locale(saved_locale);
    return 0;
}

int pdf_save_file(struct pdf_doc *pdf, FILE *fp)
{
    return pdf_save_file_internal(pdf, fp, NULL, NULL, NULL, 0, NULL);
}

static int pdf_save_file_encrypted(struct pdf_doc *pdf, FILE *fp,
                                   const char *user_password)
{
    /* Pre-count objects (including the encrypt dict) to build the file ID
     * that is used for key derivation. */
    int xref_count = 0;
    for (int i = 0; i < flexarray_size(&pdf->objects); i++) {
        const struct pdf_object *o = pdf_get_object(pdf, i);
        if (o && o->type != OBJ_none)
            xref_count++;
    }
    xref_count++; /* for the encryption dictionary object */

    const struct pdf_object *info_obj = pdf_find_first_object(pdf, OBJ_info);
    uint64_t id1 = hash(5381, info_obj->info, sizeof(struct pdf_info));
    id1 = hash(id1, &xref_count, sizeof(xref_count));

    /* Convert id1 to 8 bytes big-endian — these are the exact bytes written
     * to the PDF's /ID first element and used for key derivation. */
    uint8_t file_id[8];
    for (int i = 0; i < 8; i++)
        file_id[i] = (uint8_t)((id1 >> (56 - 8 * i)) & 0xff);

    /* Derive encryption keys */
    struct pdf_crypt crypt;
    uint8_t owner_key[32], user_key[32];
    /* P = -4 (0xFFFFFFFC): all operations permitted once opened */
    int32_t permissions = -4;
    pdf_crypt_compute_keys(user_password, file_id, permissions, &crypt,
                           owner_key, user_key);

    return pdf_save_file_internal(pdf, fp, &crypt, owner_key, user_key,
                                  permissions, file_id);
}

int pdf_save(struct pdf_doc *pdf, const char *filename)
{
    FILE *fp;
    int e;

    if (filename == NULL)
        fp = stdout;
    else if ((fp = fopen(filename, "wb")) == NULL)
        return pdf_set_err(pdf, -errno, "Unable to open '%s': %s", filename,
                           strerror(errno));

    e = pdf_save_file(pdf, fp);

    if (fp != stdout) {
        if (fclose(fp) != 0)
            return pdf_set_err(pdf, -errno, "Unable to close '%s': %s",
                               filename, strerror(errno));
    }

    return e;
}

static int pdf_add_stream(struct pdf_doc *pdf, struct pdf_object *page,
                          const char *buffer)
{
    struct pdf_object *obj;
    size_t len;

    if (!page)
        page = pdf_find_last_object(pdf, OBJ_page);

    if (!page)
        return pdf_set_err(pdf, -EINVAL, "Invalid pdf page");

    len = strlen(buffer);
    /* We don't want any trailing whitespace in the stream */
    while (len >= 1 && (buffer[len - 1] == '\r' || buffer[len - 1] == '\n'))
        len--;

    obj = pdf_add_object(pdf, OBJ_stream);
    if (!obj)
        return pdf->errval;

    dstr_printf(&obj->stream.stream, "<< /Length %zu >>stream\r\n", len);
    dstr_append_data(&obj->stream.stream, buffer, len);
    dstr_append(&obj->stream.stream, "\r\nendstream\r\n");

    return flexarray_append(&page->page.children, obj);
}

int pdf_add_bookmark(struct pdf_doc *pdf, struct pdf_object *page, int parent,
                     const char *name)
{
    struct pdf_object *obj, *outline = NULL;

    if (!page)
        page = pdf_find_last_object(pdf, OBJ_page);

    if (!page)
        return pdf_set_err(pdf, -EINVAL,
                           "Unable to add bookmark, no pages available");

    if (!pdf_find_first_object(pdf, OBJ_outline)) {
        outline = pdf_add_object(pdf, OBJ_outline);
        if (!outline)
            return pdf->errval;
    }

    obj = pdf_add_object(pdf, OBJ_bookmark);
    if (!obj) {
        if (outline)
            pdf_del_object(pdf, outline);
        return pdf->errval;
    }

    strncpy(obj->bookmark.name, name, sizeof(obj->bookmark.name) - 1);
    obj->bookmark.name[sizeof(obj->bookmark.name) - 1] = '\0';
    obj->bookmark.page = page;
    if (parent >= 0) {
        struct pdf_object *parent_obj = pdf_get_object(pdf, parent);
        if (!parent_obj)
            return pdf_set_err(pdf, -EINVAL, "Invalid parent ID %d supplied",
                               parent);
        obj->bookmark.parent = parent_obj;
        flexarray_append(&parent_obj->bookmark.children, obj);
    }

    return obj->index;
}

int pdf_add_link(struct pdf_doc *pdf, struct pdf_object *page, float x,
                 float y, float width, float height,
                 struct pdf_object *target_page, float target_x,
                 float target_y)
{
    struct pdf_object *obj;

    if (!page)
        page = pdf_find_last_object(pdf, OBJ_page);

    if (!page)
        return pdf_set_err(pdf, -EINVAL,
                           "Unable to add link, no pages available");

    if (!target_page)
        return pdf_set_err(pdf, -EINVAL, "Unable to link, no target page");

    obj = pdf_add_object(pdf, OBJ_link);
    if (!obj) {
        return pdf->errval;
    }

    obj->link.target_page = target_page;
    obj->link.target_x = target_x;
    obj->link.target_y = target_y;
    obj->link.llx = x;
    obj->link.lly = y;
    obj->link.urx = x + width;
    obj->link.ury = y + height;
    flexarray_append(&page->page.annotations, obj);

    return obj->index;
}

static int utf8_to_utf32(const char *utf8, int len, uint32_t *utf32)
{
    uint32_t ch;
    uint8_t mask;

    if (len <= 0 || !utf8 || !utf32)
        return -ENOSPC;

    ch = *(uint8_t *)utf8;
    if ((ch & 0x80) == 0) {
        len = 1;
        mask = 0x7f;
    } else if ((ch & 0xe0) == 0xc0 && len >= 2) {
        len = 2;
        mask = 0x1f;
    } else if ((ch & 0xf0) == 0xe0 && len >= 3) {
        len = 3;
        mask = 0xf;
    } else if ((ch & 0xf8) == 0xf0 && len >= 4) {
        len = 4;
        mask = 0x7;
    } else
        return -EINVAL;

    ch = 0;
    for (int i = 0; i < len; i++) {
        int shift = (len - i - 1) * 6;
        if (!*utf8)
            return -EINVAL;
        if (i == 0)
            ch |= ((uint32_t)(*utf8++) & mask) << shift;
        else
            ch |= ((uint32_t)(*utf8++) & 0x3f) << shift;
    }

    *utf32 = ch;

    return len;
}

static int utf8_to_pdfencoding(struct pdf_doc *pdf, const char *utf8, int len,
                               uint8_t *res)
{
    uint32_t code;
    int code_len;

    *res = 0;

    code_len = utf8_to_utf32(utf8, len, &code);
    if (code_len < 0) {
        return pdf_set_err(pdf, -EINVAL, "Invalid UTF-8 encoding");
    }

    if (code > 255) {
        /* We support *some* minimal UTF-8 characters */
        // See Appendix D of
        // https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf
        // These are all in WinAnsiEncoding
        switch (code) {
        case 0x152: // Latin Capital Ligature OE
            *res = 0214;
            break;
        case 0x153: // Latin Small Ligature oe
            *res = 0234;
            break;
        case 0x160: // Latin Capital Letter S with caron
            *res = 0212;
            break;
        case 0x161: // Latin Small Letter S with caron
            *res = 0232;
            break;
        case 0x178: // Latin Capital Letter y with diaeresis
            *res = 0237;
            break;
        case 0x17d: // Latin Capital Letter Z with caron
            *res = 0216;
            break;
        case 0x17e: // Latin Small Letter Z with caron
            *res = 0236;
            break;
        case 0x192: // Latin Small Letter F with hook
            *res = 0203;
            break;
        case 0x2c6: // Modifier Letter Circumflex Accent
            *res = 0210;
            break;
        case 0x2dc: // Small Tilde
            *res = 0230;
            break;
        case 0x2013: // Endash
            *res = 0226;
            break;
        case 0x2014: // Emdash
            *res = 0227;
            break;
        case 0x2018: // Left Single Quote
            *res = 0221;
            break;
        case 0x2019: // Right Single Quote
            *res = 0222;
            break;
        case 0x201a: // Single low-9 Quotation Mark
            *res = 0202;
            break;
        case 0x201c: // Left Double Quote
            *res = 0223;
            break;
        case 0x201d: // Right Double Quote
            *res = 0224;
            break;
        case 0x201e: // Double low-9 Quotation Mark
            *res = 0204;
            break;
        case 0x2020: // Dagger
            *res = 0206;
            break;
        case 0x2021: // Double Dagger
            *res = 0207;
            break;
        case 0x2022: // Bullet
            *res = 0225;
            break;
        case 0x2026: // Horizontal Ellipsis
            *res = 0205;
            break;
        case 0x2030: // Per Mille Sign
            *res = 0211;
            break;
        case 0x2039: // Single Left-pointing Angle Quotation Mark
            *res = 0213;
            break;
        case 0x203a: // Single Right-pointing Angle Quotation Mark
            *res = 0233;
            break;
        case 0x20ac: // Euro
            *res = 0200;
            break;
        case 0x2122: // Trade Mark Sign
            *res = 0231;
            break;
        default:
            return pdf_set_err(pdf, -EINVAL,
                               "Unsupported UTF-8 character: 0x%x 0o%o %s",
                               code, code, utf8);
        }
    } else {
        *res = code;
    }
    return code_len;
}

static int pdf_add_text_spacing(struct pdf_doc *pdf, struct pdf_object *page,
                                const char *text, float size, float xoff,
                                float yoff, uint32_t colour, float spacing,
                                float angle)
{
    int ret;
    size_t len = text ? strlen(text) : 0;
    struct dstr str = INIT_DSTR;
    int alpha = (colour >> 24) >> 4;

    /* Don't bother adding empty/null strings */
    if (!len)
        return 0;

    dstr_append(&str, "BT ");
    dstr_printf(&str, "/GS%d gs ", alpha);
    if (angle != 0) {
        dstr_printf(&str, "%f %f %f %f %f %f Tm ", cosf(angle), sinf(angle),
                    -sinf(angle), cosf(angle), xoff, yoff);
    } else {
        dstr_printf(&str, "%f %f TD ", xoff, yoff);
    }
    dstr_printf(&str, "/F%d %f Tf ", pdf->current_font->font.index, size);
    dstr_printf(&str, "%f %f %f rg ", PDF_RGB_R(colour), PDF_RGB_G(colour),
                PDF_RGB_B(colour));
    dstr_printf(&str, "%f Tc ", spacing);

    if (pdf->current_font->font.is_ttf) {
        /* Type0/Identity-H: encode text as a hex string of 2-byte glyph IDs.
         * Each UTF-8 codepoint is mapped to its glyph ID via the stored cmap
         * subtable.  Control characters (< U+0020) are skipped. */
        dstr_append(&str, "<");
        for (size_t i = 0; i < len;) {
            uint32_t codepoint;
            int code_len =
                utf8_to_utf32(&text[i], (int)(len - i), &codepoint);
            if (code_len < 0) {
                dstr_free(&str);
                return pdf_set_err(pdf, -EINVAL,
                                   "Invalid UTF-8 encoding in text at "
                                   "position %zu",
                                   i);
            }
            i += code_len;
            if (codepoint < 0x20u)
                continue; // skip control characters
            uint16_t glyph_id = ttf_cmap_subtable_lookup(
                pdf->current_font->font.cmap_data,
                pdf->current_font->font.cmap_data_len, codepoint);
            char hex[5];
            snprintf(hex, sizeof(hex), "%04X", (unsigned)glyph_id);
            dstr_append(&str, hex);
        }
        dstr_append(&str, "> Tj ");
    } else {
        dstr_append(&str, "(");
        /* Escape magic characters properly */
        for (size_t i = 0; i < len;) {
            int code_len;
            uint8_t pdf_char;
            code_len = utf8_to_pdfencoding(pdf, &text[i], len - i, &pdf_char);
            if (code_len < 0) {
                dstr_free(&str);
                return code_len;
            }

            if (strchr("()\\", pdf_char)) {
                char buf[3];
                /* Escape some characters */
                buf[0] = '\\';
                buf[1] = pdf_char;
                buf[2] = '\0';
                dstr_append(&str, buf);
            } else if (strrchr("\n\r\t\b\f", pdf_char)) {
                /* Skip over these characters */
                ;
            } else {
                dstr_append_data(&str, &pdf_char, 1);
            }

            i += code_len;
        }
        dstr_append(&str, ") Tj ");
    }

    dstr_append(&str, "ET");

    ret = pdf_add_stream(pdf, page, dstr_data(&str));
    dstr_free(&str);
    return ret;
}

int pdf_add_text(struct pdf_doc *pdf, struct pdf_object *page,
                 const char *text, float size, float xoff, float yoff,
                 uint32_t colour)
{
    return pdf_add_text_spacing(pdf, page, text, size, xoff, yoff, colour, 0,
                                0);
}

int pdf_add_text_rotate(struct pdf_doc *pdf, struct pdf_object *page,
                        const char *text, float size, float xoff, float yoff,
                        float angle, uint32_t colour)
{
    return pdf_add_text_spacing(pdf, page, text, size, xoff, yoff, colour, 0,
                                angle);
}

/* How wide is each character, in points, at size 14 */
static const uint16_t helvetica_widths[256] = {
    280, 280, 280, 280,  280, 280, 280, 280,  280,  280, 280,  280, 280,
    280, 280, 280, 280,  280, 280, 280, 280,  280,  280, 280,  280, 280,
    280, 280, 280, 280,  280, 280, 280, 280,  357,  560, 560,  896, 672,
    192, 335, 335, 392,  588, 280, 335, 280,  280,  560, 560,  560, 560,
    560, 560, 560, 560,  560, 560, 280, 280,  588,  588, 588,  560, 1023,
    672, 672, 727, 727,  672, 615, 784, 727,  280,  504, 672,  560, 839,
    727, 784, 672, 784,  727, 672, 615, 727,  672,  951, 672,  672, 615,
    280, 280, 280, 472,  560, 335, 560, 560,  504,  560, 560,  280, 560,
    560, 223, 223, 504,  223, 839, 560, 560,  560,  560, 335,  504, 280,
    560, 504, 727, 504,  504, 504, 336, 262,  336,  588, 352,  560, 352,
    223, 560, 335, 1008, 560, 560, 335, 1008, 672,  335, 1008, 352, 615,
    352, 352, 223, 223,  335, 335, 352, 560,  1008, 335, 1008, 504, 335,
    951, 352, 504, 672,  280, 335, 560, 560,  560,  560, 262,  560, 335,
    742, 372, 560, 588,  335, 742, 335, 403,  588,  335, 335,  335, 560,
    541, 280, 335, 335,  367, 560, 840, 840,  840,  615, 672,  672, 672,
    672, 672, 672, 1008, 727, 672, 672, 672,  672,  280, 280,  280, 280,
    727, 727, 784, 784,  784, 784, 784, 588,  784,  727, 727,  727, 727,
    672, 672, 615, 560,  560, 560, 560, 560,  560,  896, 504,  560, 560,
    560, 560, 280, 280,  280, 280, 560, 560,  560,  560, 560,  560, 560,
    588, 615, 560, 560,  560, 560, 504, 560,  504,
};

static const uint16_t helvetica_bold_widths[256] = {
    280,  280, 280,  280, 280, 280, 280, 280,  280, 280, 280, 280,  280, 280,
    280,  280, 280,  280, 280, 280, 280, 280,  280, 280, 280, 280,  280, 280,
    280,  280, 280,  280, 280, 335, 477, 560,  560, 896, 727, 239,  335, 335,
    392,  588, 280,  335, 280, 280, 560, 560,  560, 560, 560, 560,  560, 560,
    560,  560, 335,  335, 588, 588, 588, 615,  982, 727, 727, 727,  727, 672,
    615,  784, 727,  280, 560, 727, 615, 839,  727, 784, 672, 784,  727, 672,
    615,  727, 672,  951, 672, 672, 615, 335,  280, 335, 588, 560,  335, 560,
    615,  560, 615,  560, 335, 615, 615, 280,  280, 560, 280, 896,  615, 615,
    615,  615, 392,  560, 335, 615, 560, 784,  560, 560, 504, 392,  282, 392,
    588,  352, 560,  352, 280, 560, 504, 1008, 560, 560, 335, 1008, 672, 335,
    1008, 352, 615,  352, 352, 280, 280, 504,  504, 352, 560, 1008, 335, 1008,
    560,  335, 951,  352, 504, 672, 280, 335,  560, 560, 560, 560,  282, 560,
    335,  742, 372,  560, 588, 335, 742, 335,  403, 588, 335, 335,  335, 615,
    560,  280, 335,  335, 367, 560, 840, 840,  840, 615, 727, 727,  727, 727,
    727,  727, 1008, 727, 672, 672, 672, 672,  280, 280, 280, 280,  727, 727,
    784,  784, 784,  784, 784, 588, 784, 727,  727, 727, 727, 672,  672, 615,
    560,  560, 560,  560, 560, 560, 896, 560,  560, 560, 560, 560,  280, 280,
    280,  280, 615,  615, 615, 615, 615, 615,  615, 588, 615, 615,  615, 615,
    615,  560, 615,  560,
};

static const uint16_t helvetica_bold_oblique_widths[256] = {
    280,  280, 280,  280, 280, 280, 280, 280,  280, 280, 280, 280,  280, 280,
    280,  280, 280,  280, 280, 280, 280, 280,  280, 280, 280, 280,  280, 280,
    280,  280, 280,  280, 280, 335, 477, 560,  560, 896, 727, 239,  335, 335,
    392,  588, 280,  335, 280, 280, 560, 560,  560, 560, 560, 560,  560, 560,
    560,  560, 335,  335, 588, 588, 588, 615,  982, 727, 727, 727,  727, 672,
    615,  784, 727,  280, 560, 727, 615, 839,  727, 784, 672, 784,  727, 672,
    615,  727, 672,  951, 672, 672, 615, 335,  280, 335, 588, 560,  335, 560,
    615,  560, 615,  560, 335, 615, 615, 280,  280, 560, 280, 896,  615, 615,
    615,  615, 392,  560, 335, 615, 560, 784,  560, 560, 504, 392,  282, 392,
    588,  352, 560,  352, 280, 560, 504, 1008, 560, 560, 335, 1008, 672, 335,
    1008, 352, 615,  352, 352, 280, 280, 504,  504, 352, 560, 1008, 335, 1008,
    560,  335, 951,  352, 504, 672, 280, 335,  560, 560, 560, 560,  282, 560,
    335,  742, 372,  560, 588, 335, 742, 335,  403, 588, 335, 335,  335, 615,
    560,  280, 335,  335, 367, 560, 840, 840,  840, 615, 727, 727,  727, 727,
    727,  727, 1008, 727, 672, 672, 672, 672,  280, 280, 280, 280,  727, 727,
    784,  784, 784,  784, 784, 588, 784, 727,  727, 727, 727, 672,  672, 615,
    560,  560, 560,  560, 560, 560, 896, 560,  560, 560, 560, 560,  280, 280,
    280,  280, 615,  615, 615, 615, 615, 615,  615, 588, 615, 615,  615, 615,
    615,  560, 615,  560,
};

static const uint16_t helvetica_oblique_widths[256] = {
    280, 280, 280, 280,  280, 280, 280, 280,  280,  280, 280,  280, 280,
    280, 280, 280, 280,  280, 280, 280, 280,  280,  280, 280,  280, 280,
    280, 280, 280, 280,  280, 280, 280, 280,  357,  560, 560,  896, 672,
    192, 335, 335, 392,  588, 280, 335, 280,  280,  560, 560,  560, 560,
    560, 560, 560, 560,  560, 560, 280, 280,  588,  588, 588,  560, 1023,
    672, 672, 727, 727,  672, 615, 784, 727,  280,  504, 672,  560, 839,
    727, 784, 672, 784,  727, 672, 615, 727,  672,  951, 672,  672, 615,
    280, 280, 280, 472,  560, 335, 560, 560,  504,  560, 560,  280, 560,
    560, 223, 223, 504,  223, 839, 560, 560,  560,  560, 335,  504, 280,
    560, 504, 727, 504,  504, 504, 336, 262,  336,  588, 352,  560, 352,
    223, 560, 335, 1008, 560, 560, 335, 1008, 672,  335, 1008, 352, 615,
    352, 352, 223, 223,  335, 335, 352, 560,  1008, 335, 1008, 504, 335,
    951, 352, 504, 672,  280, 335, 560, 560,  560,  560, 262,  560, 335,
    742, 372, 560, 588,  335, 742, 335, 403,  588,  335, 335,  335, 560,
    541, 280, 335, 335,  367, 560, 840, 840,  840,  615, 672,  672, 672,
    672, 672, 672, 1008, 727, 672, 672, 672,  672,  280, 280,  280, 280,
    727, 727, 784, 784,  784, 784, 784, 588,  784,  727, 727,  727, 727,
    672, 672, 615, 560,  560, 560, 560, 560,  560,  896, 504,  560, 560,
    560, 560, 280, 280,  280, 280, 560, 560,  560,  560, 560,  560, 560,
    588, 615, 560, 560,  560, 560, 504, 560,  504,
};

static const uint16_t symbol_widths[256] = {
    252, 252, 252, 252,  252, 252, 252,  252, 252,  252,  252, 252, 252, 252,
    252, 252, 252, 252,  252, 252, 252,  252, 252,  252,  252, 252, 252, 252,
    252, 252, 252, 252,  252, 335, 718,  504, 553,  839,  784, 442, 335, 335,
    504, 553, 252, 553,  252, 280, 504,  504, 504,  504,  504, 504, 504, 504,
    504, 504, 280, 280,  553, 553, 553,  447, 553,  727,  672, 727, 616, 615,
    769, 607, 727, 335,  636, 727, 691,  896, 727,  727,  774, 746, 560, 596,
    615, 695, 442, 774,  650, 801, 615,  335, 869,  335,  663, 504, 504, 636,
    553, 553, 497, 442,  525, 414, 607,  331, 607,  553,  553, 580, 525, 553,
    553, 525, 553, 607,  442, 580, 718,  691, 496,  691,  497, 483, 201, 483,
    553, 0,   0,   0,    0,   0,   0,    0,   0,    0,    0,   0,   0,   0,
    0,   0,   0,   0,    0,   0,   0,    0,   0,    0,    0,   0,   0,   0,
    0,   0,   0,   0,    0,   0,   756,  624, 248,  553,  168, 718, 504, 759,
    759, 759, 759, 1050, 994, 607, 994,  607, 403,  553,  414, 553, 553, 718,
    497, 463, 553, 553,  553, 553, 1008, 607, 1008, 663,  829, 691, 801, 994,
    774, 774, 829, 774,  774, 718, 718,  718, 718,  718,  718, 718, 774, 718,
    796, 796, 897, 829,  553, 252, 718,  607, 607,  1050, 994, 607, 994, 607,
    497, 331, 796, 796,  792, 718, 387,  387, 387,  387,  387, 387, 497, 497,
    497, 497, 0,   331,  276, 691, 691,  691, 387,  387,  387, 387, 387, 387,
    497, 497, 497, 0,
};

static const uint16_t times_widths[256] = {
    252, 252, 252, 252, 252, 252, 252, 252,  252, 252, 252, 252,  252, 252,
    252, 252, 252, 252, 252, 252, 252, 252,  252, 252, 252, 252,  252, 252,
    252, 252, 252, 252, 252, 335, 411, 504,  504, 839, 784, 181,  335, 335,
    504, 568, 252, 335, 252, 280, 504, 504,  504, 504, 504, 504,  504, 504,
    504, 504, 280, 280, 568, 568, 568, 447,  928, 727, 672, 672,  727, 615,
    560, 727, 727, 335, 392, 727, 615, 896,  727, 727, 560, 727,  672, 560,
    615, 727, 727, 951, 727, 727, 615, 335,  280, 335, 472, 504,  335, 447,
    504, 447, 504, 447, 335, 504, 504, 280,  280, 504, 280, 784,  504, 504,
    504, 504, 335, 392, 280, 504, 504, 727,  504, 504, 447, 483,  201, 483,
    545, 352, 504, 352, 335, 504, 447, 1008, 504, 504, 335, 1008, 560, 335,
    896, 352, 615, 352, 352, 335, 335, 447,  447, 352, 504, 1008, 335, 987,
    392, 335, 727, 352, 447, 727, 252, 335,  504, 504, 504, 504,  201, 504,
    335, 766, 278, 504, 568, 335, 766, 335,  403, 568, 302, 302,  335, 504,
    456, 252, 335, 302, 312, 504, 756, 756,  756, 447, 727, 727,  727, 727,
    727, 727, 896, 672, 615, 615, 615, 615,  335, 335, 335, 335,  727, 727,
    727, 727, 727, 727, 727, 568, 727, 727,  727, 727, 727, 727,  560, 504,
    447, 447, 447, 447, 447, 447, 672, 447,  447, 447, 447, 447,  280, 280,
    280, 280, 504, 504, 504, 504, 504, 504,  504, 568, 504, 504,  504, 504,
    504, 504, 504, 504,
};

static const uint16_t times_bold_widths[256] = {
    252, 252, 252, 252,  252, 252, 252, 252,  252,  252,  252,  252,  252,
    252, 252, 252, 252,  252, 252, 252, 252,  252,  252,  252,  252,  252,
    252, 252, 252, 252,  252, 252, 252, 335,  559,  504,  504,  1008, 839,
    280, 335, 335, 504,  574, 252, 335, 252,  280,  504,  504,  504,  504,
    504, 504, 504, 504,  504, 504, 335, 335,  574,  574,  574,  504,  937,
    727, 672, 727, 727,  672, 615, 784, 784,  392,  504,  784,  672,  951,
    727, 784, 615, 784,  727, 560, 672, 727,  727,  1008, 727,  727,  672,
    335, 280, 335, 585,  504, 335, 504, 560,  447,  560,  447,  335,  504,
    560, 280, 335, 560,  280, 839, 560, 504,  560,  560,  447,  392,  335,
    560, 504, 727, 504,  504, 447, 397, 221,  397,  524,  352,  504,  352,
    335, 504, 504, 1008, 504, 504, 335, 1008, 560,  335,  1008, 352,  672,
    352, 352, 335, 335,  504, 504, 352, 504,  1008, 335,  1008, 392,  335,
    727, 352, 447, 727,  252, 335, 504, 504,  504,  504,  221,  504,  335,
    752, 302, 504, 574,  335, 752, 335, 403,  574,  302,  302,  335,  560,
    544, 252, 335, 302,  332, 504, 756, 756,  756,  504,  727,  727,  727,
    727, 727, 727, 1008, 727, 672, 672, 672,  672,  392,  392,  392,  392,
    727, 727, 784, 784,  784, 784, 784, 574,  784,  727,  727,  727,  727,
    727, 615, 560, 504,  504, 504, 504, 504,  504,  727,  447,  447,  447,
    447, 447, 280, 280,  280, 280, 504, 560,  504,  504,  504,  504,  504,
    574, 504, 560, 560,  560, 560, 504, 560,  504,
};

static const uint16_t times_bold_italic_widths[256] = {
    252, 252, 252, 252, 252, 252, 252, 252,  252, 252, 252, 252,  252, 252,
    252, 252, 252, 252, 252, 252, 252, 252,  252, 252, 252, 252,  252, 252,
    252, 252, 252, 252, 252, 392, 559, 504,  504, 839, 784, 280,  335, 335,
    504, 574, 252, 335, 252, 280, 504, 504,  504, 504, 504, 504,  504, 504,
    504, 504, 335, 335, 574, 574, 574, 504,  838, 672, 672, 672,  727, 672,
    672, 727, 784, 392, 504, 672, 615, 896,  727, 727, 615, 727,  672, 560,
    615, 727, 672, 896, 672, 615, 615, 335,  280, 335, 574, 504,  335, 504,
    504, 447, 504, 447, 335, 504, 560, 280,  280, 504, 280, 784,  560, 504,
    504, 504, 392, 392, 280, 560, 447, 672,  504, 447, 392, 350,  221, 350,
    574, 352, 504, 352, 335, 504, 504, 1008, 504, 504, 335, 1008, 560, 335,
    951, 352, 615, 352, 352, 335, 335, 504,  504, 352, 504, 1008, 335, 1008,
    392, 335, 727, 352, 392, 615, 252, 392,  504, 504, 504, 504,  221, 504,
    335, 752, 268, 504, 610, 335, 752, 335,  403, 574, 302, 302,  335, 580,
    504, 252, 335, 302, 302, 504, 756, 756,  756, 504, 672, 672,  672, 672,
    672, 672, 951, 672, 672, 672, 672, 672,  392, 392, 392, 392,  727, 727,
    727, 727, 727, 727, 727, 574, 727, 727,  727, 727, 727, 615,  615, 504,
    504, 504, 504, 504, 504, 504, 727, 447,  447, 447, 447, 447,  280, 280,
    280, 280, 504, 560, 504, 504, 504, 504,  504, 574, 504, 560,  560, 560,
    560, 447, 504, 447,
};

static const uint16_t times_italic_widths[256] = {
    252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252,  252, 252,
    252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252,  252, 252,
    252, 252, 252, 252, 252, 335, 423, 504, 504, 839, 784, 215,  335, 335,
    504, 680, 252, 335, 252, 280, 504, 504, 504, 504, 504, 504,  504, 504,
    504, 504, 335, 335, 680, 680, 680, 504, 927, 615, 615, 672,  727, 615,
    615, 727, 727, 335, 447, 672, 560, 839, 672, 727, 615, 727,  615, 504,
    560, 727, 615, 839, 615, 560, 560, 392, 280, 392, 425, 504,  335, 504,
    504, 447, 504, 447, 280, 504, 504, 280, 280, 447, 280, 727,  504, 504,
    504, 504, 392, 392, 280, 504, 447, 672, 447, 447, 392, 403,  277, 403,
    545, 352, 504, 352, 335, 504, 560, 896, 504, 504, 335, 1008, 504, 335,
    951, 352, 560, 352, 352, 335, 335, 560, 560, 352, 504, 896,  335, 987,
    392, 335, 672, 352, 392, 560, 252, 392, 504, 504, 504, 504,  277, 504,
    335, 766, 278, 504, 680, 335, 766, 335, 403, 680, 302, 302,  335, 504,
    527, 252, 335, 302, 312, 504, 756, 756, 756, 504, 615, 615,  615, 615,
    615, 615, 896, 672, 615, 615, 615, 615, 335, 335, 335, 335,  727, 672,
    727, 727, 727, 727, 727, 680, 727, 727, 727, 727, 727, 560,  615, 504,
    504, 504, 504, 504, 504, 504, 672, 447, 447, 447, 447, 447,  280, 280,
    280, 280, 504, 504, 504, 504, 504, 504, 504, 680, 504, 504,  504, 504,
    504, 447, 504, 447,
};

static const uint16_t zapfdingbats_widths[256] = {
    0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   280,  981, 968, 981, 987, 724, 795, 796, 797, 695,
    967, 946, 553, 861, 918,  940, 918, 952, 981, 761, 852, 768, 767, 575,
    682, 769, 766, 765, 760,  497, 556, 541, 581, 697, 792, 794, 794, 796,
    799, 800, 822, 829, 795,  847, 829, 839, 822, 837, 930, 749, 728, 754,
    796, 798, 700, 782, 774,  798, 765, 712, 713, 687, 706, 832, 821, 795,
    795, 712, 692, 701, 694,  792, 793, 718, 797, 791, 797, 879, 767, 768,
    768, 765, 765, 899, 899,  794, 790, 441, 139, 279, 418, 395, 395, 673,
    673, 0,   393, 393, 319,  319, 278, 278, 513, 513, 413, 413, 235, 235,
    336, 336, 0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,    0,   0,   737, 548, 548, 917, 672, 766, 766,
    782, 599, 699, 631, 794,  794, 794, 794, 794, 794, 794, 794, 794, 794,
    794, 794, 794, 794, 794,  794, 794, 794, 794, 794, 794, 794, 794, 794,
    794, 794, 794, 794, 794,  794, 794, 794, 794, 794, 794, 794, 794, 794,
    794, 794, 901, 844, 1024, 461, 753, 931, 753, 925, 934, 935, 935, 840,
    879, 834, 931, 931, 924,  937, 938, 466, 890, 842, 842, 873, 873, 701,
    701, 880, 0,   880, 766,  953, 777, 871, 777, 895, 974, 895, 837, 879,
    934, 977, 925, 0,
};

static const uint16_t courier_widths[256] = {
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604, 604,
    604,
};

static int pdf_text_point_width(struct pdf_doc *pdf, const char *text,
                                ptrdiff_t text_len, float size,
                                const uint16_t *widths, float *point_width)
{
    uint32_t len = 0;
    if (text_len < 0)
        text_len = strlen(text);
    *point_width = 0.0f;

    for (int i = 0; i < (int)text_len;) {
        uint8_t pdf_char = 0;
        int code_len;
        code_len =
            utf8_to_pdfencoding(pdf, &text[i], text_len - i, &pdf_char);
        if (code_len < 0)
            return pdf_set_err(pdf, code_len,
                               "Invalid unicode string at position %d in %s",
                               i, text);
        i += code_len;

        if (pdf_char != '\n' && pdf_char != '\r')
            len += widths[pdf_char];
    }

    /* Our widths arrays are for 14pt fonts */
    *point_width = len * size / (14.0f * 72.0f);

    return 0;
}

// Compute text point width for a TTF font using direct Unicode → glyph
// width lookup (bypasses the WinAnsi encoding used by standard fonts).
static int pdf_ttf_text_point_width(struct pdf_doc *pdf,
                                    const struct pdf_object *font_obj,
                                    const char *text, ptrdiff_t text_len,
                                    float size, float *point_width)
{
    float total = 0.0f;
    if (text_len < 0)
        text_len = (ptrdiff_t)strlen(text);
    *point_width = 0.0f;

    for (int byte_pos = 0; byte_pos < (int)text_len;) {
        uint32_t codepoint;
        int code_len = utf8_to_utf32(&text[byte_pos],
                                     (int)(text_len - byte_pos), &codepoint);
        if (code_len < 0)
            return pdf_set_err(
                pdf, -EINVAL, "Invalid UTF-8 encoding in text at position %d",
                byte_pos);
        byte_pos += code_len;
        if (codepoint < 0x20u)
            continue; // skip control characters
        uint16_t glyph_id =
            ttf_cmap_subtable_lookup(font_obj->font.cmap_data,
                                     font_obj->font.cmap_data_len, codepoint);
        uint16_t advance = ttf_hmtx_advance(
            font_obj->font.hmtx_data, font_obj->font.hmtx_data_len, glyph_id,
            font_obj->font.num_h_metrics);
        total += (float)advance;
    }
    *point_width = total * size / (float)font_obj->font.units_per_em;
    return 0;
}

// Dispatch text width calculation based on whether the current font is a TTF.
// For standard fonts, `widths` must be a pointer to a 256-entry advance-width
// table; for TTF fonts it is ignored (glyph widths are looked up via cmap).
static int pdf_text_width_for_font(struct pdf_doc *pdf, bool is_ttf,
                                   const uint16_t *widths, const char *text,
                                   ptrdiff_t len, float size, float *out)
{
    if (is_ttf)
        return pdf_ttf_text_point_width(pdf, pdf->current_font, text, len,
                                        size, out);
    return pdf_text_point_width(pdf, text, len, size, widths, out);
}

static const uint16_t *find_font_widths(const char *font_name)
{
    if (strcasecmp(font_name, "Helvetica") == 0)
        return helvetica_widths;
    if (strcasecmp(font_name, "Helvetica-Bold") == 0)
        return helvetica_bold_widths;
    if (strcasecmp(font_name, "Helvetica-BoldOblique") == 0)
        return helvetica_bold_oblique_widths;
    if (strcasecmp(font_name, "Helvetica-Oblique") == 0)
        return helvetica_oblique_widths;
    if (strcasecmp(font_name, "Courier") == 0 ||
        strcasecmp(font_name, "Courier-Bold") == 0 ||
        strcasecmp(font_name, "Courier-BoldOblique") == 0 ||
        strcasecmp(font_name, "Courier-Oblique") == 0)
        return courier_widths;
    if (strcasecmp(font_name, "Times-Roman") == 0)
        return times_widths;
    if (strcasecmp(font_name, "Times-Bold") == 0)
        return times_bold_widths;
    if (strcasecmp(font_name, "Times-Italic") == 0)
        return times_italic_widths;
    if (strcasecmp(font_name, "Times-BoldItalic") == 0)
        return times_bold_italic_widths;
    if (strcasecmp(font_name, "Symbol") == 0)
        return symbol_widths;
    if (strcasecmp(font_name, "ZapfDingbats") == 0)
        return zapfdingbats_widths;

    return NULL;
}

int pdf_get_font_text_width(struct pdf_doc *pdf, const char *font_name,
                            const char *text, float size, float *text_width)
{
    if (!font_name)
        font_name = pdf->current_font->font.name;

    // Check for a loaded TTF font with this name
    for (const struct pdf_object *obj = pdf_find_first_object(pdf, OBJ_font);
         obj; obj = obj->next) {
        if (obj->font.is_ttf && strcmp(obj->font.name, font_name) == 0) {
            return pdf_ttf_text_point_width(pdf, obj, text, -1, size,
                                            text_width);
        }
    }

    const uint16_t *widths = find_font_widths(font_name);
    if (!widths)
        return pdf_set_err(pdf, -EINVAL,
                           "Unable to determine width for font '%s'",
                           pdf->current_font->font.name);
    return pdf_text_point_width(pdf, text, -1, size, widths, text_width);
}

static const char *find_word_break(const char *string)
{
    if (!string)
        return NULL;

    /* Skip over the actual word */
    while (*string) {
        uint32_t codepoint;
        int code_len = utf8_to_utf32(string, strlen(string), &codepoint);
        if (code_len <= 0) {
            code_len = 1;
        } else if (codepoint < 256 && isspace((unsigned char)codepoint)) {
            break;
        }
        string += code_len;
    }

    return string;
}

int pdf_add_text_wrap(struct pdf_doc *pdf, struct pdf_object *page,
                      const char *text, float size, float xoff, float yoff,
                      float angle, uint32_t colour, float wrap_width,
                      int align, float *height)
{
    /* Move through the text string, stopping at word boundaries,
     * trying to find the longest text string we can fit in the given width
     */
    const char *start = text;
    const char *last_best = text;
    const char *end = text;
    char line[512];
    const uint16_t *widths = NULL;
    bool ttf_font = pdf->current_font->font.is_ttf;
    float orig_yoff = yoff;

    if (!ttf_font) {
        widths = find_font_widths(pdf->current_font->font.name);
        if (!widths)
            return pdf_set_err(pdf, -EINVAL,
                               "Unable to determine width for font '%s'",
                               pdf->current_font->font.name);
    }

    while (start && *start) {
        const char *new_end = find_word_break(end + 1);
        float line_width;
        int output = 0;
        int e;

        end = new_end;

        e = pdf_text_width_for_font(pdf, ttf_font, widths, start, end - start,
                                    size, &line_width);
        if (e < 0)
            return e;

        if (line_width >= wrap_width) {
            if (last_best == start) {
                /* There is a single word that is too long for the line */
                ptrdiff_t i;
                /* Find the best character to chop it at */
                for (i = end - start - 1; i > 0; i--) {
                    float this_width;
                    // Don't look at places that are in the middle of a utf-8
                    // sequence
                    if ((start[i - 1] & 0xc0) == 0xc0 ||
                        ((start[i - 1] & 0xc0) == 0x80 &&
                         (start[i] & 0xc0) == 0x80))
                        continue;
                    e = pdf_text_width_for_font(pdf, ttf_font, widths, start,
                                                i, size, &this_width);
                    if (e < 0)
                        return e;
                    if (this_width < wrap_width)
                        break;
                }
                if (i == 0)
                    return pdf_set_err(pdf, -EINVAL,
                                       "Unable to find suitable line break");

                end = start + i;
            } else
                end = last_best;
            output = 1;
        }
        if (*end == '\0')
            output = 1;

        if (*end == '\n' || *end == '\r')
            output = 1;

        if (output) {
            int len = end - start;
            float char_spacing = 0;
            float xoff_align = xoff;

            if (len >= (int)sizeof(line))
                len = (int)sizeof(line) - 1;
            strncpy(line, start, len);
            line[len] = '\0';

            e = pdf_text_width_for_font(pdf, ttf_font, widths, start, len,
                                        size, &line_width);
            if (e < 0)
                return e;

            switch (align) {
            case PDF_ALIGN_RIGHT:
                xoff_align += wrap_width - line_width;
                break;
            case PDF_ALIGN_CENTER:
                xoff_align += (wrap_width - line_width) / 2;
                break;
            case PDF_ALIGN_JUSTIFY:
                if ((len - 1) > 0 && *end != '\r' && *end != '\n' &&
                    *end != '\0')
                    char_spacing = (wrap_width - line_width) / (len - 2);
                break;
            case PDF_ALIGN_JUSTIFY_ALL:
                if ((len - 1) > 0)
                    char_spacing = (wrap_width - line_width) / (len - 2);
                break;
            }

            if (align != PDF_ALIGN_NO_WRITE) {
                pdf_add_text_spacing(pdf, page, line, size, xoff_align, yoff,
                                     colour, char_spacing, angle);
            }

            if (*end == ' ')
                end++;

            start = last_best = end;
            yoff -= size;
        } else
            last_best = end;
    }

    if (height)
        *height = orig_yoff - yoff;
    return 0;
}

int pdf_add_line_pattern(struct pdf_doc *pdf, struct pdf_object *page,
                         float x1, float y1, float x2, float y2, float width,
                         uint32_t colour, const float pattern[],
                         int pattern_len, float phase)
{
    int ret;
    struct dstr str = INIT_DSTR;
    int nonzero = 0;

    if (pattern_len < 0 || (pattern_len > 0 && !pattern))
        return pdf_set_err(pdf, -EINVAL, "Invalid line pattern");
    for (int i = 0; i < pattern_len; i++) {
        if (pattern[i] < 0.0f)
            return pdf_set_err(pdf, -EINVAL,
                               "Line pattern lengths must be >= 0");
        if (pattern[i] > 0.0f)
            nonzero = 1;
    }
    if (pattern_len > 0 && !nonzero)
        return pdf_set_err(pdf, -EINVAL,
                           "Line pattern must have a non-zero length");

    if (pattern_len > 0) {
        dstr_append(&str, "[");
        for (int i = 0; i < pattern_len; i++)
            dstr_printf(&str, "%s%f", i ? " " : "", pattern[i]);
        dstr_printf(&str, "] %f d\r\n", phase);
    }
    dstr_printf(&str, "%f w\r\n", width);
    dstr_printf(&str, "%f %f m\r\n", x1, y1);
    dstr_printf(&str, "/DeviceRGB CS\r\n");
    dstr_printf(&str, "%f %f %f RG\r\n", PDF_RGB_R(colour), PDF_RGB_G(colour),
                PDF_RGB_B(colour));
    dstr_printf(&str, "%f %f l S\r\n", x2, y2);
    /* Restore the solid pattern */
    if (pattern_len > 0)
        dstr_append(&str, "[] 0 d\r\n");

    ret = pdf_add_stream(pdf, page, dstr_data(&str));
    dstr_free(&str);

    return ret;
}

int pdf_add_line(struct pdf_doc *pdf, struct pdf_object *page, float x1,
                 float y1, float x2, float y2, float width, uint32_t colour)
{
    return pdf_add_line_pattern(pdf, page, x1, y1, x2, y2, width, colour,
                                NULL, 0, 0.0f);
}

int pdf_add_cubic_bezier(struct pdf_doc *pdf, struct pdf_object *page,
                         float x1, float y1, float x2, float y2, float xq1,
                         float yq1, float xq2, float yq2, float width,
                         uint32_t colour)
{
    int ret;
    struct dstr str = INIT_DSTR;

    dstr_printf(&str, "%f w\r\n", width);
    dstr_printf(&str, "%f %f m\r\n", x1, y1);
    dstr_printf(&str, "/DeviceRGB CS\r\n");
    dstr_printf(&str, "%f %f %f RG\r\n", PDF_RGB_R(colour), PDF_RGB_G(colour),
                PDF_RGB_B(colour));
    dstr_printf(&str, "%f %f %f %f %f %f c S\r\n", xq1, yq1, xq2, yq2, x2,
                y2);

    ret = pdf_add_stream(pdf, page, dstr_data(&str));
    dstr_free(&str);

    return ret;
}

int pdf_add_quadratic_bezier(struct pdf_doc *pdf, struct pdf_object *page,
                             float x1, float y1, float x2, float y2,
                             float xq1, float yq1, float width,
                             uint32_t colour)
{
    float xc1 = x1 + (xq1 - x1) * (2.0f / 3.0f);
    float yc1 = y1 + (yq1 - y1) * (2.0f / 3.0f);
    float xc2 = x2 + (xq1 - x2) * (2.0f / 3.0f);
    float yc2 = y2 + (yq1 - y2) * (2.0f / 3.0f);
    return pdf_add_cubic_bezier(pdf, page, x1, y1, x2, y2, xc1, yc1, xc2, yc2,
                                width, colour);
}

int pdf_add_custom_path(struct pdf_doc *pdf, struct pdf_object *page,
                        const struct pdf_path_operation *operations,
                        int operation_count, float stroke_width,
                        uint32_t stroke_colour, uint32_t fill_colour)
{
    int ret;
    struct dstr str = INIT_DSTR;

    if (!PDF_IS_TRANSPARENT(fill_colour)) {
        dstr_printf(&str, "/DeviceRGB CS\r\n");
        dstr_printf(&str, "%f %f %f rg\r\n", PDF_RGB_R(fill_colour),
                    PDF_RGB_G(fill_colour), PDF_RGB_B(fill_colour));
    }
    dstr_printf(&str, "%f w\r\n", stroke_width);
    dstr_printf(&str, "/DeviceRGB CS\r\n");
    dstr_printf(&str, "%f %f %f RG\r\n", PDF_RGB_R(stroke_colour),
                PDF_RGB_G(stroke_colour), PDF_RGB_B(stroke_colour));

    for (int i = 0; i < operation_count; i++) {
        struct pdf_path_operation operation = operations[i];
        switch (operation.op) {
        case 'm':
            dstr_printf(&str, "%f %f m\r\n", operation.x1, operation.y1);
            break;
        case 'l':
            dstr_printf(&str, "%f %f l\r\n", operation.x1, operation.y1);
            break;
        case 'c':
            dstr_printf(&str, "%f %f %f %f %f %f c\r\n", operation.x1,
                        operation.y1, operation.x2, operation.y2,
                        operation.x3, operation.y3);
            break;
        case 'v':
            dstr_printf(&str, "%f %f %f %f v\r\n", operation.x1, operation.y1,
                        operation.x2, operation.y2);
            break;
        case 'y':
            dstr_printf(&str, "%f %f %f %f y\r\n", operation.x1, operation.y1,
                        operation.x2, operation.y2);
            break;
        case 'h':
            dstr_printf(&str, "h\r\n");
            break;
        default:
            return pdf_set_err(pdf, -errno, "Invalid operation");
            break;
        }
    }

    if (PDF_IS_TRANSPARENT(fill_colour))
        dstr_printf(&str, "%s", "S ");
    else
        dstr_printf(&str, "%s", "B ");
    ret = pdf_add_stream(pdf, page, dstr_data(&str));
    dstr_free(&str);

    return ret;
}

int pdf_add_ellipse(struct pdf_doc *pdf, struct pdf_object *page, float x,
                    float y, float xradius, float yradius, float width,
                    uint32_t colour, uint32_t fill_colour)
{
    int ret;
    struct dstr str = INIT_DSTR;
    float lx, ly;

    lx = (4.0f / 3.0f) * (float)(M_SQRT2 - 1) * xradius;
    ly = (4.0f / 3.0f) * (float)(M_SQRT2 - 1) * yradius;

    if (!PDF_IS_TRANSPARENT(fill_colour)) {
        dstr_printf(&str, "/DeviceRGB CS\r\n");
        dstr_printf(&str, "%f %f %f rg\r\n", PDF_RGB_R(fill_colour),
                    PDF_RGB_G(fill_colour), PDF_RGB_B(fill_colour));
    }

    /* stroke color */
    dstr_printf(&str, "/DeviceRGB CS\r\n");
    dstr_printf(&str, "%f %f %f RG\r\n", PDF_RGB_R(colour), PDF_RGB_G(colour),
                PDF_RGB_B(colour));

    dstr_printf(&str, "%f w ", width);

    dstr_printf(&str, "%.2f %.2f m ", (x + xradius), (y));

    dstr_printf(&str, "%.2f %.2f %.2f %.2f %.2f %.2f c ", (x + xradius),
                (y - ly), (x + lx), (y - yradius), x, (y - yradius));

    dstr_printf(&str, "%.2f %.2f %.2f %.2f %.2f %.2f c ", (x - lx),
                (y - yradius), (x - xradius), (y - ly), (x - xradius), y);

    dstr_printf(&str, "%.2f %.2f %.2f %.2f %.2f %.2f c ", (x - xradius),
                (y + ly), (x - lx), (y + yradius), x, (y + yradius));

    dstr_printf(&str, "%.2f %.2f %.2f %.2f %.2f %.2f c ", (x + lx),
                (y + yradius), (x + xradius), (y + ly), (x + xradius), y);

    if (PDF_IS_TRANSPARENT(fill_colour))
        dstr_printf(&str, "%s", "S ");
    else
        dstr_printf(&str, "%s", "B ");

    ret = pdf_add_stream(pdf, page, dstr_data(&str));
    dstr_free(&str);

    return ret;
}

int pdf_add_circle(struct pdf_doc *pdf, struct pdf_object *page, float xr,
                   float yr, float radius, float width, uint32_t colour,
                   uint32_t fill_colour)
{
    return pdf_add_ellipse(pdf, page, xr, yr, radius, radius, width, colour,
                           fill_colour);
}

int pdf_add_rectangle(struct pdf_doc *pdf, struct pdf_object *page, float x,
                      float y, float width, float height, float border_width,
                      uint32_t colour)
{
    int ret;
    struct dstr str = INIT_DSTR;

    dstr_printf(&str, "%f %f %f RG ", PDF_RGB_R(colour), PDF_RGB_G(colour),
                PDF_RGB_B(colour));
    dstr_printf(&str, "%f w ", border_width);
    dstr_printf(&str, "%f %f %f %f re S ", x, y, width, height);

    ret = pdf_add_stream(pdf, page, dstr_data(&str));
    dstr_free(&str);

    return ret;
}

int pdf_add_filled_rectangle(struct pdf_doc *pdf, struct pdf_object *page,
                             float x, float y, float width, float height,
                             float border_width, uint32_t colour_fill,
                             uint32_t colour_border)
{
    int ret;
    struct dstr str = INIT_DSTR;

    dstr_printf(&str, "%f %f %f rg ", PDF_RGB_R(colour_fill),
                PDF_RGB_G(colour_fill), PDF_RGB_B(colour_fill));
    if (border_width > 0) {
        dstr_printf(&str, "%f %f %f RG ", PDF_RGB_R(colour_border),
                    PDF_RGB_G(colour_border), PDF_RGB_B(colour_border));
        dstr_printf(&str, "%f w ", border_width);
        dstr_printf(&str, "%f %f %f %f re B ", x, y, width, height);
    } else {
        dstr_printf(&str, "%f %f %f %f re f ", x, y, width, height);
    }

    ret = pdf_add_stream(pdf, page, dstr_data(&str));
    dstr_free(&str);

    return ret;
}

int pdf_add_polygon(struct pdf_doc *pdf, struct pdf_object *page, float x[],
                    float y[], int count, float border_width, uint32_t colour)
{
    int ret;
    struct dstr str = INIT_DSTR;

    dstr_printf(&str, "%f %f %f RG ", PDF_RGB_R(colour), PDF_RGB_G(colour),
                PDF_RGB_B(colour));
    dstr_printf(&str, "%f w ", border_width);
    dstr_printf(&str, "%f %f m ", x[0], y[0]);
    for (int i = 1; i < count; i++) {
        dstr_printf(&str, "%f %f l ", x[i], y[i]);
    }
    dstr_printf(&str, "h S ");

    ret = pdf_add_stream(pdf, page, dstr_data(&str));
    dstr_free(&str);

    return ret;
}

int pdf_add_filled_polygon(struct pdf_doc *pdf, struct pdf_object *page,
                           float x[], float y[], int count,
                           float border_width, uint32_t colour)
{
    int ret;
    struct dstr str = INIT_DSTR;

    dstr_printf(&str, "%f %f %f RG ", PDF_RGB_R(colour), PDF_RGB_G(colour),
                PDF_RGB_B(colour));
    dstr_printf(&str, "%f %f %f rg ", PDF_RGB_R(colour), PDF_RGB_G(colour),
                PDF_RGB_B(colour));
    dstr_printf(&str, "%f w ", border_width);
    dstr_printf(&str, "%f %f m ", x[0], y[0]);
    for (int i = 1; i < count; i++) {
        dstr_printf(&str, "%f %f l ", x[i], y[i]);
    }
    dstr_printf(&str, "h f ");

    ret = pdf_add_stream(pdf, page, dstr_data(&str));
    dstr_free(&str);

    return ret;
}

static const struct {
    uint32_t code;
    char ch;
} code_128a_encoding[] = {
    {0x212222, ' '},  {0x222122, '!'},  {0x222221, '"'},   {0x121223, '#'},
    {0x121322, '$'},  {0x131222, '%'},  {0x122213, '&'},   {0x122312, '\''},
    {0x132212, '('},  {0x221213, ')'},  {0x221312, '*'},   {0x231212, '+'},
    {0x112232, ','},  {0x122132, '-'},  {0x122231, '.'},   {0x113222, '/'},
    {0x123122, '0'},  {0x123221, '1'},  {0x223211, '2'},   {0x221132, '3'},
    {0x221231, '4'},  {0x213212, '5'},  {0x223112, '6'},   {0x312131, '7'},
    {0x311222, '8'},  {0x321122, '9'},  {0x321221, ':'},   {0x312212, ';'},
    {0x322112, '<'},  {0x322211, '='},  {0x212123, '>'},   {0x212321, '?'},
    {0x232121, '@'},  {0x111323, 'A'},  {0x131123, 'B'},   {0x131321, 'C'},
    {0x112313, 'D'},  {0x132113, 'E'},  {0x132311, 'F'},   {0x211313, 'G'},
    {0x231113, 'H'},  {0x231311, 'I'},  {0x112133, 'J'},   {0x112331, 'K'},
    {0x132131, 'L'},  {0x113123, 'M'},  {0x113321, 'N'},   {0x133121, 'O'},
    {0x313121, 'P'},  {0x211331, 'Q'},  {0x231131, 'R'},   {0x213113, 'S'},
    {0x213311, 'T'},  {0x213131, 'U'},  {0x311123, 'V'},   {0x311321, 'W'},
    {0x331121, 'X'},  {0x312113, 'Y'},  {0x312311, 'Z'},   {0x332111, '['},
    {0x314111, '\\'}, {0x221411, ']'},  {0x431111, '^'},   {0x111224, '_'},
    {0x111422, '`'},  {0x121124, 'a'},  {0x121421, 'b'},   {0x141122, 'c'},
    {0x141221, 'd'},  {0x112214, 'e'},  {0x112412, 'f'},   {0x122114, 'g'},
    {0x122411, 'h'},  {0x142112, 'i'},  {0x142211, 'j'},   {0x241211, 'k'},
    {0x221114, 'l'},  {0x413111, 'm'},  {0x241112, 'n'},   {0x134111, 'o'},
    {0x111242, 'p'},  {0x121142, 'q'},  {0x121241, 'r'},   {0x114212, 's'},
    {0x124112, 't'},  {0x124211, 'u'},  {0x411212, 'v'},   {0x421112, 'w'},
    {0x421211, 'x'},  {0x212141, 'y'},  {0x214121, 'z'},   {0x412121, '{'},
    {0x111143, '|'},  {0x111341, '}'},  {0x131141, '~'},   {0x114113, '\0'},
    {0x114311, '\0'}, {0x411113, '\0'}, {0x411311, '\0'},  {0x113141, '\0'},
    {0x114131, '\0'}, {0x311141, '\0'}, {0x411131, '\0'},  {0x211412, '\0'},
    {0x211214, '\0'}, {0x211232, '\0'}, {0x2331112, '\0'},
};

static int find_128_encoding(char ch)
{
    for (int i = 0; i < (int)ARRAY_SIZE(code_128a_encoding); i++) {
        if (code_128a_encoding[i].ch == ch)
            return i;
    }
    return -1;
}

static float pdf_barcode_128a_ch(struct pdf_doc *pdf, struct pdf_object *page,
                                 float x, float y, float width, float height,
                                 uint32_t colour, int index, int code_len)
{
    uint32_t code = code_128a_encoding[index].code;
    float line_width = width / 11.0f;

    for (int i = 0; i < code_len; i++) {
        uint8_t shift = (code_len - 1 - i) * 4;
        uint8_t mask = (code >> shift) & 0xf;

        if (!(i % 2))
            pdf_add_filled_rectangle(pdf, page, x, y, line_width * mask,
                                     height, 0, colour, PDF_TRANSPARENT);
        x += line_width * mask;
    }
    return x;
}

static int pdf_add_barcode_128a(struct pdf_doc *pdf, struct pdf_object *page,
                                float x, float y, float width, float height,
                                const char *string, uint32_t colour)
{
    const char *s;
    size_t len = strlen(string) + 3;
    float char_width = width / len;
    int checksum, i;

    if (char_width / 11.0f <= 0)
        return pdf_set_err(pdf, -EINVAL,
                           "Insufficient width to draw barcode");

    for (s = string; *s; s++)
        if (find_128_encoding(*s) < 0)
            return pdf_set_err(pdf, -EINVAL, "Invalid barcode character 0x%x",
                               *s);

    x = pdf_barcode_128a_ch(pdf, page, x, y, char_width, height, colour, 104,
                            6);
    checksum = 104;

    for (i = 1, s = string; *s; s++, i++) {
        int index = find_128_encoding(*s);
        // This should be impossible, due to the checks above, but confirm
        // here anyway to stop coverity complaining
        if (index < 0)
            return pdf_set_err(pdf, -EINVAL,
                               "Invalid 128a barcode character 0x%x", *s);
        x = pdf_barcode_128a_ch(pdf, page, x, y, char_width, height, colour,
                                index, 6);
        checksum += index * i;
    }
    x = pdf_barcode_128a_ch(pdf, page, x, y, char_width, height, colour,
                            checksum % 103, 6);
    pdf_barcode_128a_ch(pdf, page, x, y, char_width, height, colour, 106, 7);
    return 0;
}

/* Code 39 character encoding. Each 4-bit value indicates:
 * 0 => wide bar
 * 1 => narrow bar
 * 2 => wide space
 */
static const struct {
    int code;
    char ch;
} code_39_encoding[] = {
    {0x012110, '1'}, {0x102110, '2'}, {0x002111, '3'},
    {0x112010, '4'}, {0x012011, '5'}, {0x102011, '6'},
    {0x112100, '7'}, {0x012101, '8'}, {0x102101, '9'},
    {0x112001, '0'}, {0x011210, 'A'}, {0x101210, 'B'},
    {0x001211, 'C'}, {0x110210, 'D'}, {0x010211, 'E'},
    {0x100211, 'F'}, {0x111200, 'G'}, {0x011201, 'H'},
    {0x101201, 'I'}, {0x110201, 'J'}, {0x011120, 'K'},
    {0x101120, 'L'}, {0x001121, 'M'}, {0x110120, 'N'},
    {0x010121, 'O'}, {0x100121, 'P'}, {0x111020, 'Q'},
    {0x011021, 'R'}, {0x101021, 'S'}, {0x110021, 'T'},
    {0x021110, 'U'}, {0x120110, 'V'}, {0x020111, 'W'},
    {0x121010, 'X'}, {0x021011, 'Y'}, {0x120011, 'Z'},
    {0x121100, '-'}, {0x021101, '.'}, {0x120101, ' '},
    {0x121001, '*'}, // 'stop' character
};

static int find_39_encoding(char ch)
{
    for (int i = 0; i < (int)ARRAY_SIZE(code_39_encoding); i++) {
        if (code_39_encoding[i].ch == ch) {
            return code_39_encoding[i].code;
        }
    }
    return -1;
}

static int pdf_barcode_39_ch(struct pdf_doc *pdf, struct pdf_object *page,
                             float x, float y, float char_width, float height,
                             uint32_t colour, char ch, float *new_x)
{
    float nw = char_width / 12.0f;
    float ww = char_width / 4.0f;
    int code = 0;

    code = find_39_encoding(ch);
    if (code < 0)
        return pdf_set_err(pdf, -EINVAL, "Invalid Code 39 character %c 0x%x",
                           ch, ch);

    for (int i = 5; i >= 0; i--) {
        int pattern = (code >> i * 4) & 0xf;
        if (pattern == 0) { // wide
            if (pdf_add_filled_rectangle(pdf, page, x, y, ww - 1, height, 0,
                                         colour, PDF_TRANSPARENT) < 0)
                return pdf->errval;
            x += ww;
        }
        if (pattern == 1) { // narrow
            if (pdf_add_filled_rectangle(pdf, page, x, y, nw - 1, height, 0,
                                         colour, PDF_TRANSPARENT) < 0)
                return pdf->errval;
            x += nw;
        }
        if (pattern == 2) { // space
            x += nw;
        }
    }
    if (new_x)
        *new_x = x;
    return 0;
}

static int pdf_add_barcode_39(struct pdf_doc *pdf, struct pdf_object *page,
                              float x, float y, float width, float height,
                              const char *string, uint32_t colour)
{
    size_t len = strlen(string);
    float char_width = width / (len + 2);
    int e;

    e = pdf_barcode_39_ch(pdf, page, x, y, char_width, height, colour, '*',
                          &x);
    if (e < 0)
        return e;

    while (string && *string) {
        e = pdf_barcode_39_ch(pdf, page, x, y, char_width, height, colour,
                              *string, &x);
        if (e < 0)
            return e;
        string++;
    }

    e = pdf_barcode_39_ch(pdf, page, x, y, char_width, height, colour, '*',
                          NULL);
    if (e < 0)
        return e;

    return 0;
}

/* EAN/UPC character encoding. Each 4-bit value indicates width in x units.
 * Elements are SBSB (Space, Bar, Space, Bar) for LHS digits
 * Elements are inverted for RHS digits
 */
static const int code_eanupc_encoding[] = {
    0x3211, // 0
    0x2221, // 1
    0x2122, // 2
    0x1411, // 3
    0x1132, // 4
    0x1231, // 5
    0x1114, // 6
    0x1312, // 7
    0x1213, // 8
    0x3112, // 9
};

/**
 * List of different barcode guard patterns that are supported
 */
enum {
    GUARD_NORMAL,  //!< Produce normal guard pattern
    GUARD_CENTRE,  //!< Produce centre guard pattern
    GUARD_SPECIAL, //!< Produce special guard pattern
    GUARD_ADDON,
    GUARD_ADDON_DELIN,
};

static const int code_eanupc_aux_encoding[] = {
    0x150, // Normal guard
    0x554, // Centre guard
    0x555, // Special guard
    0x160, // Add-on guard
    0x500, // Add-on delineator
};

static const int set_ean13_encoding[] = {
    0x00, // 0
    0x34, // 1
    0x2c, // 2
    0x1c, // 3
    0x32, // 4
    0x26, // 5
    0x0e, // 6
    0x2a, // 7
    0x1a, // 8
    0x16, // 9
};

static const int set_upce_encoding[] = {
    0x07, // 0
    0x0b, // 1
    0x13, // 2
    0x23, // 3
    0x0d, // 4
    0x19, // 5
    0x31, // 6
    0x15, // 7
    0x25, // 8
    0x29, // 9
};

#define EANUPC_X PDF_MM_TO_POINT(0.330f)

static const struct {
    unsigned modules;
    float height_bar;
    float height_outer;
    unsigned quiet_left;
    unsigned quiet_right;
} eanupc_dimensions[] = {
    {113, PDF_MM_TO_POINT(22.85), PDF_MM_TO_POINT(25.93), 11, 7}, // EAN-13
    {113, PDF_MM_TO_POINT(22.85), PDF_MM_TO_POINT(25.93), 9, 9},  // UPC-A
    {81, PDF_MM_TO_POINT(18.23), PDF_MM_TO_POINT(21.31), 7, 7},   // EAN-8
    {67, PDF_MM_TO_POINT(22.85), PDF_MM_TO_POINT(25.93), 9, 7},   // UPC-E
};

static void pdf_barcode_eanupc_calc_dims(int type, float width, float height,
                                         float *x_off, float *y_off,
                                         float *new_width, float *new_height,
                                         float *x, float *bar_height,
                                         float *bar_ext, float *font_size)
{
    float aspectBarcode, aspectRect, scale;

    aspectRect = width / height;
    aspectBarcode = eanupc_dimensions[type - PDF_BARCODE_EAN13].modules *
                    EANUPC_X /
                    eanupc_dimensions[type - PDF_BARCODE_EAN13].height_outer;
    if (aspectRect > aspectBarcode) {
        *new_height = height;
        *new_width = height * aspectBarcode;
    } else if (aspectRect < aspectBarcode) {
        *new_width = width;
        *new_height = width / aspectBarcode;
    } else {
        *new_width = width;
        *new_height = height;
    }
    scale = *new_height /
            eanupc_dimensions[type - PDF_BARCODE_EAN13].height_outer;

    *x = *new_width / eanupc_dimensions[type - PDF_BARCODE_EAN13].modules;
    *bar_ext = *x * 5;
    *bar_height =
        eanupc_dimensions[type - PDF_BARCODE_EAN13].height_bar * scale;
    *font_size = 8.0f * scale;
    *x_off = (width - *new_width) / 2.0f;
    *y_off = (height - *new_height) / 2.0f;
}

static int pdf_barcode_eanupc_ch(struct pdf_doc *pdf, struct pdf_object *page,
                                 float x, float y, float x_width,
                                 float height, uint32_t colour, char ch,
                                 int set, float *new_x)
{
    if ('0' > ch || ch > '9')
        return pdf_set_err(pdf, -EINVAL, "Invalid EAN/UPC character %c 0x%x",
                           ch, ch);

    int code;
    code = code_eanupc_encoding[ch - '0'];

    for (int i = 3; i >= 0; i--) {
        int shift = (set == 1 ? 3 - i : i) * 4;
        int bar = (set == 2 && i & 0x1) || (set != 2 && (i & 0x1) == 0);
        float width = (float)((code >> shift) & 0xf);

        switch (ch) {
        case '1':
        case '2':
            if ((set == 0 && bar) || (set != 0 && !bar)) {
                width -= 1.0f / 13.0f;
            } else {
                width += 1.0f / 13.0f;
            }
            break;

        case '7':
        case '8':
            if ((set == 0 && bar) || (set != 0 && !bar)) {
                width += 1.0f / 13.0f;
            } else {
                width -= 1.0f / 13.0f;
            }
            break;
        }

        width *= x_width;
        if (bar) {
            if (pdf_add_filled_rectangle(pdf, page, x, y, width, height, 0,
                                         colour, PDF_TRANSPARENT) < 0)
                return pdf->errval;
        }
        x += width;
    }
    if (new_x)
        *new_x = x;
    return 0;
}

static int pdf_barcode_eanupc_aux(struct pdf_doc *pdf,
                                  struct pdf_object *page, float x, float y,
                                  float x_width, float height,
                                  uint32_t colour, int guard_type,
                                  float *new_x)
{
    int code = code_eanupc_aux_encoding[guard_type];

    for (int i = 5; i >= 0; i--) {
        int value = code >> i * 2 & 0x3;
        if (value) {
            if ((i & 0x1) == 0) {
                if (pdf_add_filled_rectangle(pdf, page, x, y, x_width * value,
                                             height, 0, colour,
                                             PDF_TRANSPARENT) < 0)
                    return pdf->errval;
            }
            x += x_width * value;
        }
    }
    if (new_x)
        *new_x = x;
    return 0;
}

static int pdf_add_barcode_ean13(struct pdf_doc *pdf, struct pdf_object *page,
                                 float x, float y, float width, float height,
                                 const char *string, uint32_t colour)
{
    if (!string)
        return 0;

    size_t len = strlen(string);
    int lead = 0;
    if (len == 13) {
        char ch = string[0];
        if (!isdigit(*string))
            return pdf_set_err(pdf, -EINVAL,
                               "Invalid EAN13 character %c 0x%x", ch, ch);
        lead = ch - '0';
        ++string;
    } else if (len != 12)
        return pdf_set_err(pdf, -EINVAL, "Invalid EAN13 string length %lu",
                           len);

    /* Scale and calculate dimensions */
    float x_off, y_off, new_width, new_height, x_width, bar_height, bar_ext,
        font;

    pdf_barcode_eanupc_calc_dims(PDF_BARCODE_EAN13, width, height, &x_off,
                                 &y_off, &new_width, &new_height, &x_width,
                                 &bar_height, &bar_ext, &font);

    x += x_off;
    y += y_off;
    float bar_y = y + new_height - bar_height;

    int e;
    const char *save_font = pdf->current_font->font.name;
    e = pdf_set_font(pdf, "Courier"); /* Built-in monospace font */
    if (e < 0)
        return e;

    char text[2];
    text[1] = 0;
    text[0] = lead + '0';
    e = pdf_add_text(pdf, page, text, font, x, y, colour);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    x += eanupc_dimensions[0].quiet_left * x_width;
    e = pdf_barcode_eanupc_aux(pdf, page, x, bar_y - bar_ext, x_width,
                               bar_height + bar_ext, colour, GUARD_NORMAL,
                               &x);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    for (int i = 0; i != 6; i++) {
        text[0] = *string;
        e = pdf_add_text_wrap(pdf, page, text, font, x, y, 0, colour,
                              7 * x_width, PDF_ALIGN_CENTER, NULL);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }

        int set = (set_ean13_encoding[lead] & 1 << i) ? 1 : 0;
        e = pdf_barcode_eanupc_ch(pdf, page, x, bar_y, x_width, bar_height,
                                  colour, *string, set, &x);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }
        string++;
    }

    e = pdf_barcode_eanupc_aux(pdf, page, x, bar_y - bar_ext, x_width,
                               bar_height + bar_ext, colour, GUARD_CENTRE,
                               &x);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    for (int i = 0; i != 6; i++) {
        text[0] = *string;
        e = pdf_add_text_wrap(pdf, page, text, font, x, y, 0, colour,
                              7 * x_width, PDF_ALIGN_CENTER, NULL);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }

        e = pdf_barcode_eanupc_ch(pdf, page, x, bar_y, x_width, bar_height,
                                  colour, *string, 2, &x);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }
        string++;
    }

    e = pdf_barcode_eanupc_aux(pdf, page, x, bar_y - bar_ext, x_width,
                               bar_height + bar_ext, colour, GUARD_NORMAL,
                               &x);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    text[0] = '>';
    x += eanupc_dimensions[0].quiet_right * x_width -
         604.0f * font / (14.0f * 72.0f);
    e = pdf_add_text(pdf, page, text, font, x, y, colour);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }
    pdf_set_font(pdf, save_font);
    return 0;
}

static int pdf_add_barcode_upca(struct pdf_doc *pdf, struct pdf_object *page,
                                float x, float y, float width, float height,
                                const char *string, uint32_t colour)
{
    if (!string)
        return 0;

    size_t len = strlen(string);
    if (len != 12)
        return pdf_set_err(pdf, -EINVAL, "Invalid UPCA string length %lu",
                           len);

    /* Scale and calculate dimensions */
    float x_off, y_off, new_width, new_height;
    float x_width, bar_height, bar_ext, font;

    pdf_barcode_eanupc_calc_dims(PDF_BARCODE_UPCA, width, height, &x_off,
                                 &y_off, &new_width, &new_height, &x_width,
                                 &bar_height, &bar_ext, &font);

    x += x_off;
    y += y_off;
    float bar_y = y + new_height - bar_height;

    int e;
    const char *save_font = pdf->current_font->font.name;
    e = pdf_set_font(pdf, "Courier");
    if (e < 0)
        return e;

    char text[2];
    text[1] = 0;
    text[0] = *string;
    e = pdf_add_text(pdf, page, text, font * 4.0f / 7.0f, x, y, colour);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    x += eanupc_dimensions[1].quiet_left * x_width;
    e = pdf_barcode_eanupc_aux(pdf, page, x, bar_y - bar_ext, x_width,
                               bar_height + bar_ext, colour, GUARD_NORMAL,
                               &x);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    for (int i = 0; i != 6; i++) {
        text[0] = *string;
        if (i) {
            e = pdf_add_text_wrap(pdf, page, text, font, x, y, 0, colour,
                                  7 * x_width, PDF_ALIGN_CENTER, NULL);
            if (e < 0) {
                pdf_set_font(pdf, save_font);
                return e;
            }
        }

        e = pdf_barcode_eanupc_ch(pdf, page, x, bar_y - (i ? 0 : bar_ext),
                                  x_width, bar_height + (i ? 0 : bar_ext),
                                  colour, *string, 0, &x);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }
        string++;
    }

    e = pdf_barcode_eanupc_aux(pdf, page, x, bar_y - bar_ext, x_width,
                               bar_height + bar_ext, colour, GUARD_CENTRE,
                               &x);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    for (int i = 0; i != 6; i++) {
        text[0] = *string;
        if (i != 5) {
            e = pdf_add_text_wrap(pdf, page, text, font, x, y, 0, colour,
                                  7 * x_width, PDF_ALIGN_CENTER, NULL);
            if (e < 0) {
                pdf_set_font(pdf, save_font);
                return e;
            }
        }

        e = pdf_barcode_eanupc_ch(
            pdf, page, x, bar_y - (i != 5 ? 0 : bar_ext), x_width,
            bar_height + (i != 5 ? 0 : bar_ext), colour, *string, 2, &x);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }
        string++;
    }

    e = pdf_barcode_eanupc_aux(pdf, page, x, bar_y - bar_ext, x_width,
                               bar_height + bar_ext, colour, GUARD_NORMAL,
                               &x);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    text[0] = *(string - 1);

    x += eanupc_dimensions[1].quiet_right * x_width -
         604.0f * font * 4.0f / 7.0f / (14.0f * 72.0f);
    e = pdf_add_text(pdf, page, text, font * 4.0f / 7.0f, x, y, colour);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }
    pdf_set_font(pdf, save_font);
    return 0;
}

static int pdf_add_barcode_ean8(struct pdf_doc *pdf, struct pdf_object *page,
                                float x, float y, float width, float height,
                                const char *string, uint32_t colour)
{
    if (!string)
        return 0;

    size_t len = strlen(string);
    if (len != 8)
        return pdf_set_err(pdf, -EINVAL, "Invalid EAN8 string length %lu",
                           len);

    /* Scale and calculate dimensions */
    float x_off, y_off, new_width, new_height, x_width, bar_height, bar_ext,
        font;

    pdf_barcode_eanupc_calc_dims(PDF_BARCODE_EAN8, width, height, &x_off,
                                 &y_off, &new_width, &new_height, &x_width,
                                 &bar_height, &bar_ext, &font);

    x += x_off;
    y += y_off;
    float bar_y = y + new_height - bar_height;

    int e;
    const char *save_font = pdf->current_font->font.name;
    e = pdf_set_font(pdf, "Courier"); /* Built-in monospace font */
    if (e < 0)
        return e;

    char text[2];
    text[1] = 0;
    text[0] = '<';
    e = pdf_add_text(pdf, page, text, font, x, y, colour);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    x += eanupc_dimensions[2].quiet_left * x_width;
    e = pdf_barcode_eanupc_aux(pdf, page, x, bar_y - bar_ext, x_width,
                               bar_height + bar_ext, colour, GUARD_NORMAL,
                               &x);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    for (int i = 0; i != 4; i++) {
        text[0] = *string;
        e = pdf_add_text_wrap(pdf, page, text, font, x, y, 0, colour,
                              7 * x_width, PDF_ALIGN_CENTER, NULL);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }

        e = pdf_barcode_eanupc_ch(pdf, page, x, bar_y, x_width, bar_height,
                                  colour, *string, 0, &x);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }
        string++;
    }

    e = pdf_barcode_eanupc_aux(pdf, page, x, bar_y - bar_ext, x_width,
                               bar_height + bar_ext, colour, GUARD_CENTRE,
                               &x);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    for (int i = 0; i != 4; i++) {
        text[0] = *string;
        e = pdf_add_text_wrap(pdf, page, text, font, x, y, 0, colour,
                              7 * x_width, PDF_ALIGN_CENTER, NULL);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }

        e = pdf_barcode_eanupc_ch(pdf, page, x, bar_y, x_width, bar_height,
                                  colour, *string, 2, &x);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }
        string++;
    }

    e = pdf_barcode_eanupc_aux(pdf, page, x, bar_y - bar_ext, x_width,
                               bar_height + bar_ext, colour, GUARD_NORMAL,
                               &x);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    text[0] = '>';
    x += eanupc_dimensions[0].quiet_right * x_width -
         604.0f * font / (14.0f * 72.0f);
    e = pdf_add_text(pdf, page, text, font, x, y, colour);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }
    pdf_set_font(pdf, save_font);
    return 0;
}

static int pdf_add_barcode_upce(struct pdf_doc *pdf, struct pdf_object *page,
                                float x, float y, float width, float height,
                                const char *string, uint32_t colour)
{
    if (!string)
        return 0;

    size_t len = strlen(string);
    if (len != 12)
        return pdf_set_err(pdf, -EINVAL, "Invalid UPCE string length %lu",
                           len);

    if (*string != '0')
        return pdf_set_err(pdf, -EINVAL, "Invalid UPCE char %c at start",
                           *string);

    for (size_t i = 0; i < len; i++) {
        if (!isdigit(string[i]))
            return pdf_set_err(pdf, -EINVAL, "Invalid UPCE char 0x%x at %zd",
                               string[i], i);
    }

    /* Scale and calculate dimensions */
    float x_off, y_off, new_width, new_height;
    float x_width, bar_height, bar_ext, font;

    pdf_barcode_eanupc_calc_dims(PDF_BARCODE_UPCE, width, height, &x_off,
                                 &y_off, &new_width, &new_height, &x_width,
                                 &bar_height, &bar_ext, &font);

    x += x_off;
    y += y_off;
    float bar_y = y + new_height - bar_height;

    int e;
    const char *save_font = pdf->current_font->font.name;
    e = pdf_set_font(pdf, "Courier");
    if (e < 0)
        return e;

    char text[2];
    text[1] = 0;
    text[0] = string[0];
    e = pdf_add_text(pdf, page, text, font * 4.0f / 7.0f, x, y, colour);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    x += eanupc_dimensions[2].quiet_left * x_width;
    e = pdf_barcode_eanupc_aux(pdf, page, x, bar_y, x_width, bar_height,
                               colour, GUARD_NORMAL, &x);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    char X[6];
    if (string[5] && memcmp(string + 6, "0000", 4) == 0 &&
        '5' <= string[10] && string[10] <= '9') {
        memcpy(X, string + 1, 5);
        X[5] = string[10];
    } else if (string[4] && memcmp(string + 5, "00000", 5) == 0) {
        memcpy(X, string + 1, 4);
        X[4] = string[11];
        X[5] = 4;
    } else if ('0' <= string[3] && string[3] <= '2' &&
               memcmp(string + 4, "0000", 4) == 0) {
        X[0] = string[1];
        X[1] = string[2];
        X[2] = string[8];
        X[3] = string[9];
        X[4] = string[10];
        X[5] = string[3];
    } else if ('3' <= string[3] && string[3] <= '9' &&
               memcmp(string + 4, "00000", 5) == 0) {
        memcpy(X, string + 1, 3);
        X[3] = string[9];
        X[4] = string[10];
        X[5] = 3;
    } else {
        pdf_set_font(pdf, save_font);
        return pdf_set_err(pdf, -EINVAL, "Invalid UPCE string format");
    }

    for (int i = 0; i != 6; i++) {
        text[0] = X[i];
        e = pdf_add_text_wrap(pdf, page, text, font, x, y, 0, colour,
                              7 * x_width, PDF_ALIGN_CENTER, NULL);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }

        int set = (set_upce_encoding[string[11] - '0'] & 1 << i) ? 1 : 0;
        e = pdf_barcode_eanupc_ch(pdf, page, x, bar_y, x_width, bar_height,
                                  colour, X[i], set, &x);
        if (e < 0) {
            pdf_set_font(pdf, save_font);
            return e;
        }
    }

    e = pdf_barcode_eanupc_aux(pdf, page, x, bar_y, x_width, bar_height,
                               colour, GUARD_SPECIAL, &x);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    text[0] = string[11];
    x += eanupc_dimensions[0].quiet_right * x_width -
         604.0f * font * 4.0f / 7.0f / (14.0f * 72.0f);
    e = pdf_add_text(pdf, page, text, font * 4.0f / 7.0f, x, y, colour);
    if (e < 0) {
        pdf_set_font(pdf, save_font);
        return e;
    }

    pdf_set_font(pdf, save_font);
    return 0;
}

int pdf_add_barcode(struct pdf_doc *pdf, struct pdf_object *page, int code,
                    float x, float y, float width, float height,
                    const char *string, uint32_t colour)
{
    if (!string || !*string)
        return 0;
    switch (code) {
    case PDF_BARCODE_128A:
        return pdf_add_barcode_128a(pdf, page, x, y, width, height, string,
                                    colour);
    case PDF_BARCODE_39:
        return pdf_add_barcode_39(pdf, page, x, y, width, height, string,
                                  colour);
    case PDF_BARCODE_EAN13:
        return pdf_add_barcode_ean13(pdf, page, x, y, width, height, string,
                                     colour);
    case PDF_BARCODE_UPCA:
        return pdf_add_barcode_upca(pdf, page, x, y, width, height, string,
                                    colour);
    case PDF_BARCODE_EAN8:
        return pdf_add_barcode_ean8(pdf, page, x, y, width, height, string,
                                    colour);
    case PDF_BARCODE_UPCE:
        return pdf_add_barcode_upce(pdf, page, x, y, width, height, string,
                                    colour);
    default:
        return pdf_set_err(pdf, -EINVAL, "Invalid barcode code %d", code);
    }
}

static pdf_object *pdf_add_raw_grayscale8(struct pdf_doc *pdf,
                                          const uint8_t *data, uint32_t width,
                                          uint32_t height)
{
    struct pdf_object *obj;
    size_t len;
    const char *endstream = ">\r\nendstream\r\n";
    struct dstr str = INIT_DSTR;
    size_t data_len = (size_t)width * (size_t)height;

    dstr_printf(&str,
                "<<\r\n"
                "  /Type /XObject\r\n"
                "  /Name /Image%d\r\n"
                "  /Subtype /Image\r\n"
                "  /ColorSpace /DeviceGray\r\n"
                "  /Height %d\r\n"
                "  /Width %d\r\n"
                "  /BitsPerComponent 8\r\n"
                "  /Length %zu\r\n"
                ">>stream\r\n",
                flexarray_size(&pdf->objects), height, width, data_len + 1);

    len = dstr_len(&str) + data_len + strlen(endstream) + 1;
    if (dstr_ensure(&str, len) < 0) {
        dstr_free(&str);
        pdf_set_err(pdf, -ENOMEM,
                    "Unable to allocate %zu bytes memory for image", len);
        return NULL;
    }
    dstr_append_data(&str, data, data_len);
    dstr_append(&str, endstream);

    obj = pdf_add_object(pdf, OBJ_image);
    if (!obj) {
        dstr_free(&str);
        return NULL;
    }
    obj->stream.stream = str;

    return obj;
}

static struct pdf_object *pdf_add_raw_rgb24(struct pdf_doc *pdf,
                                            const uint8_t *data,
                                            uint32_t width, uint32_t height)
{
    struct pdf_object *obj;
    size_t len;
    const char *endstream = ">\r\nendstream\r\n";
    struct dstr str = INIT_DSTR;
    size_t data_len = (size_t)width * (size_t)height * 3;

    dstr_printf(&str,
                "<<\r\n"
                "  /Type /XObject\r\n"
                "  /Name /Image%d\r\n"
                "  /Subtype /Image\r\n"
                "  /ColorSpace /DeviceRGB\r\n"
                "  /Height %d\r\n"
                "  /Width %d\r\n"
                "  /BitsPerComponent 8\r\n"
                "  /Length %zu\r\n"
                ">>stream\r\n",
                flexarray_size(&pdf->objects), height, width, data_len + 1);

    len = dstr_len(&str) + data_len + strlen(endstream) + 1;
    if (dstr_ensure(&str, len) < 0) {
        dstr_free(&str);
        pdf_set_err(pdf, -ENOMEM,
                    "Unable to allocate %zu bytes memory for image", len);
        return NULL;
    }
    dstr_append_data(&str, data, data_len);
    dstr_append(&str, endstream);

    obj = pdf_add_object(pdf, OBJ_image);
    if (!obj) {
        dstr_free(&str);
        return NULL;
    }
    obj->stream.stream = str;

    return obj;
}

static uint8_t *get_file(struct pdf_doc *pdf, const char *file_name,
                         size_t *length)
{
    FILE *fp;
    uint8_t *file_data;
    struct stat buf;
    off_t len;

    if ((fp = fopen(file_name, "rb")) == NULL) {
        pdf_set_err(pdf, -errno, "Unable to open %s: %s", file_name,
                    strerror(errno));
        return NULL;
    }

    if (fstat(fileno(fp), &buf) < 0) {
        pdf_set_err(pdf, -errno, "Unable to access %s: %s", file_name,
                    strerror(errno));
        fclose(fp);
        return NULL;
    }

    len = buf.st_size;

    file_data = (uint8_t *)malloc(len);
    if (!file_data) {
        pdf_set_err(pdf, -ENOMEM, "Unable to allocate: %d", (int)len);
        fclose(fp);
        return NULL;
    }

    if (fread(file_data, len, 1, fp) != 1) {
        pdf_set_err(pdf, -errno, "Unable to read full data: %s", file_name);
        free(file_data);
        fclose(fp);
        return NULL;
    }

    fclose(fp);

    *length = len;

    return file_data;
}

static struct pdf_object *
pdf_add_raw_jpeg_data(struct pdf_doc *pdf, const struct pdf_img_info *info,
                      const uint8_t *jpeg_data, size_t len)
{
    struct pdf_object *obj = pdf_add_object(pdf, OBJ_image);
    if (!obj)
        return NULL;

    dstr_printf(&obj->stream.stream,
                "<<\r\n"
                "  /Type /XObject\r\n"
                "  /Name /Image%d\r\n"
                "  /Subtype /Image\r\n"
                "  /ColorSpace %s\r\n"
                "  /Width %d\r\n"
                "  /Height %d\r\n"
                "  /BitsPerComponent 8\r\n"
                "  /Filter /DCTDecode\r\n"
                "  /Length %zu\r\n"
                ">>stream\r\n",
                flexarray_size(&pdf->objects),
                (info->jpeg.ncolours == 1) ? "/DeviceGray" : "/DeviceRGB",
                info->width, info->height, len);
    dstr_append_data(&obj->stream.stream, jpeg_data, len);

    dstr_printf(&obj->stream.stream, "\r\nendstream\r\n");

    return obj;
}

/**
 * Get the display dimensions of an image, respecting the images aspect ratio
 * if only one desired display dimension is defined.
 * The pdf parameter is only used for setting the error value.
 */
static int get_img_display_dimensions(struct pdf_doc *pdf, uint32_t img_width,
                                      uint32_t img_height,
                                      float *display_width,
                                      float *display_height)
{
    if (!display_height || !display_width) {
        return pdf_set_err(
            pdf, -EINVAL,
            "display_width and display_height may not be null pointers");
    }

    const float display_width_in = *display_width;
    const float display_height_in = *display_height;

    if (display_width_in < 0 && display_height_in < 0) {
        return pdf_set_err(pdf, -EINVAL,
                           "Unable to determine image display dimensions, "
                           "display_width and display_height are both < 0");
    }
    if (img_width == 0 || img_height == 0) {
        return pdf_set_err(pdf, -EINVAL,
                           "Invalid image dimensions received, the loaded "
                           "image appears to be empty.");
    }

    if (display_width_in < 0) {
        // Set width, keeping aspect ratio
        *display_width = display_height_in * ((float)img_width / img_height);
    } else if (display_height_in < 0) {
        // Set height, keeping aspect ratio
        *display_height = display_width_in * ((float)img_height / img_width);
    }
    return 0;
}

static int pdf_add_image(struct pdf_doc *pdf, struct pdf_object *page,
                         struct pdf_object *image, float x, float y,
                         float width, float height)
{
    int ret;
    struct dstr str = INIT_DSTR;

    if (!page)
        page = pdf_find_last_object(pdf, OBJ_page);

    if (!page)
        return pdf_set_err(pdf, -EINVAL, "Invalid pdf page");

    if (image->type != OBJ_image)
        return pdf_set_err(pdf, -EINVAL,
                           "adding an image, but wrong object type %d",
                           image->type);

    if (image->stream.page != NULL)
        return pdf_set_err(pdf, -EEXIST, "image already on a page");

    image->stream.page = page;

    dstr_append(&str, "q ");
    dstr_printf(&str, "%f 0 0 %f %f %f cm ", width, height, x, y);
    dstr_printf(&str, "/Image%d Do ", image->index);
    dstr_append(&str, "Q");

    ret = pdf_add_stream(pdf, page, dstr_data(&str));
    dstr_free(&str);
    return ret;
}

// Works like fgets, except it's for a fixed in-memory buffer of data
static size_t dgets(const uint8_t *data, size_t *pos, size_t len, char *line,
                    size_t line_len)
{
    size_t line_pos = 0;

    if (*pos >= len)
        return 0;

    if (line_len == 0)
        return 0;

    while ((*pos) < len) {
        if (line_pos < line_len - 1) {
            // Reject non-ascii data
            if (data[*pos] & 0x80) {
                return 0;
            }
            line[line_pos] = data[*pos];
            line_pos++;
        }
        if (data[*pos] == '\n') {
            (*pos)++;
            break;
        }
        (*pos)++;
    }

    line[line_pos] = '\0';

    return *pos;
}

static int parse_ppm_header(struct pdf_img_info *info, const uint8_t *data,
                            size_t length, char *err_msg,
                            size_t err_msg_length)
{
    char line[1024];
    memset(line, '\0', sizeof(line));
    size_t pos = 0;

    // Load the PPM file
    if (!dgets(data, &pos, length, line, sizeof(line) - 1)) {
        snprintf(err_msg, err_msg_length, "Invalid PPM file");
        return -EINVAL;
    }

    // Determine number of color channels (Also, we only support binary ppms)
    int ncolors;
    if (strncmp(line, "P6", 2) == 0) {
        info->ppm.color_space = PPM_BINARY_COLOR_RGB;
        ncolors = 3;
    } else if (strncmp(line, "P5", 2) == 0) {
        info->ppm.color_space = PPM_BINARY_COLOR_GRAY;
        ncolors = 1;
    } else {
        snprintf(err_msg, err_msg_length,
                 "Only binary PPM files (grayscale, RGB) supported");
        return -EINVAL;
    }

    // Skip comments before header
    do {
        if (!dgets(data, &pos, length, line, sizeof(line) - 1)) {
            snprintf(err_msg, err_msg_length, "Unable to find PPM header");
            return -EINVAL;
        }
    } while (line[0] == '#');

    // Read image dimensions
    if (sscanf(line, "%u %u\n", &(info->width), &(info->height)) != 2) {
        snprintf(err_msg, err_msg_length, "Unable to find PPM size");
        return -EINVAL;
    }
    if (info->width > MAX_IMAGE_WIDTH || info->height > MAX_IMAGE_HEIGHT) {
        snprintf(err_msg, err_msg_length, "Invalid width/height: %ux%u",
                 info->width, info->height);
        return -EINVAL;
    }
    info->ppm.size = (size_t)(info->width * info->height * ncolors);
    info->ppm.data_begin_pos = pos;

    return 0;
}

static int pdf_add_ppm_data(struct pdf_doc *pdf, struct pdf_object *page,
                            float x, float y, float display_width,
                            float display_height,
                            const struct pdf_img_info *info,
                            const uint8_t *ppm_data, size_t len)
{
    char line[1024];
    // We start reading at the position delivered by parse_ppm_header,
    // since we already parsed the header of the file there.
    size_t pos = info->ppm.data_begin_pos;

    /* Skip over the byte-size line */
    if (!dgets(ppm_data, &pos, len, line, sizeof(line) - 1))
        return pdf_set_err(pdf, -EINVAL, "No byte-size line in PPM file");

    /* Try and limit the memory usage to sane images */
    if (info->width > MAX_IMAGE_WIDTH || info->height > MAX_IMAGE_HEIGHT) {
        return pdf_set_err(pdf, -EINVAL,
                           "Invalid width/height in PPM file: %ux%u",
                           info->width, info->height);
    }

    if (info->ppm.size > len - pos) {
        return pdf_set_err(pdf, -EINVAL, "Insufficient image data available");
    }

    switch (info->ppm.color_space) {
    case PPM_BINARY_COLOR_GRAY:
        return pdf_add_grayscale8(pdf, page, x, y, display_width,
                                  display_height, &ppm_data[pos], info->width,
                                  info->height);
        break;

    case PPM_BINARY_COLOR_RGB:
        return pdf_add_rgb24(pdf, page, x, y, display_width, display_height,
                             &ppm_data[pos], info->width, info->height);
        break;

    default:
        return pdf_set_err(pdf, -EINVAL,
                           "Invalid color space in ppm file: %i",
                           info->ppm.color_space);
        break;
    }
}

static int parse_jpeg_header(struct pdf_img_info *info, const uint8_t *data,
                             size_t length, char *err_msg,
                             size_t err_msg_length)
{
    // See http://www.videotechnology.com/jpeg/j1.html for details
    if (length >= 4 && data[0] == 0xFF && data[1] == 0xD8) {
        for (size_t i = 2; i < length; i++) {
            if (data[i] != 0xff) {
                break;
            }
            while (++i < length && data[i] == 0xff)
                ;
            if (i + 2 >= length) {
                break;
            }
            int len = data[i + 1] * 256 + data[i + 2];
            /* Search for SOFn marker and decode jpeg details */
            if ((data[i] & 0xf4) == 0xc0) {
                if (len >= 9 && i + len + 1 < length) {
                    info->height = data[i + 4] * 256 + data[i + 5];
                    info->width = data[i + 6] * 256 + data[i + 7];
                    info->jpeg.ncolours = data[i + 8];
                    return 0;
                }
                break;
            }
            i += len;
        }
    }
    snprintf(err_msg, err_msg_length, "Error parsing JPEG header");
    return -EINVAL;
}

static int pdf_add_jpeg_data(struct pdf_doc *pdf, struct pdf_object *page,
                             float x, float y, float display_width,
                             float display_height,
                             const struct pdf_img_info *info,
                             const uint8_t *jpeg_data, size_t len)
{
    struct pdf_object *obj;

    obj = pdf_add_raw_jpeg_data(pdf, info, jpeg_data, len);
    if (!obj)
        return pdf->errval;

    if (get_img_display_dimensions(pdf, info->width, info->height,
                                   &display_width, &display_height)) {
        return pdf->errval;
    }
    return pdf_add_image(pdf, page, obj, x, y, display_width, display_height);
}

int pdf_add_rgb24(struct pdf_doc *pdf, struct pdf_object *page, float x,
                  float y, float display_width, float display_height,
                  const uint8_t *data, uint32_t width, uint32_t height)
{
    struct pdf_object *obj;

    obj = pdf_add_raw_rgb24(pdf, data, width, height);
    if (!obj)
        return pdf->errval;

    if (get_img_display_dimensions(pdf, width, height, &display_width,
                                   &display_height)) {
        return pdf->errval;
    }
    return pdf_add_image(pdf, page, obj, x, y, display_width, display_height);
}

int pdf_add_grayscale8(struct pdf_doc *pdf, struct pdf_object *page, float x,
                       float y, float display_width, float display_height,
                       const uint8_t *data, uint32_t width, uint32_t height)
{
    struct pdf_object *obj;

    obj = pdf_add_raw_grayscale8(pdf, data, width, height);
    if (!obj)
        return pdf->errval;

    if (get_img_display_dimensions(pdf, width, height, &display_width,
                                   &display_height)) {
        return pdf->errval;
    }
    return pdf_add_image(pdf, page, obj, x, y, display_width, display_height);
}

static int parse_png_header(struct pdf_img_info *info, const uint8_t *data,
                            size_t length, char *err_msg,
                            size_t err_msg_length)
{
    if (length <= sizeof(png_signature)) {
        snprintf(err_msg, err_msg_length, "PNG file too short");
        return -EINVAL;
    }

    if (memcmp(data, png_signature, sizeof(png_signature))) {
        snprintf(err_msg, err_msg_length, "File is not correct PNG file");
        return -EINVAL;
    }

    // process first PNG chunk
    uint32_t pos = sizeof(png_signature);
    const struct png_chunk *chunk = (const struct png_chunk *)&data[pos];
    pos += sizeof(struct png_chunk);
    if (pos > length) {
        snprintf(err_msg, err_msg_length, "PNG file too short");
        return -EINVAL;
    }
    if (strncmp(chunk->type, png_chunk_header, 4) == 0) {
        // header found, process width and height, check errors
        struct png_header *header = &info->png;

        if (pos + sizeof(struct png_header) > length) {
            snprintf(err_msg, err_msg_length, "PNG file too short");
            return -EINVAL;
        }

        memcpy(header, &data[pos], sizeof(struct png_header));
        if (header->deflate != 0) {
            snprintf(err_msg, err_msg_length, "Deflate wrong in PNG header");
            return -EINVAL;
        }
        if (header->bitDepth == 0) {
            snprintf(err_msg, err_msg_length, "PNG file has zero bit depth");
            return -EINVAL;
        }
        // ensure the width and height values have the proper byte order
        // and copy them into the info struct.
        header->width = ntoh32(header->width);
        header->height = ntoh32(header->height);
        info->width = header->width;
        info->height = header->height;
        return 0;
    }
    snprintf(err_msg, err_msg_length, "Failed to read PNG file header");
    return -EINVAL;
}

static int pdf_add_png_data(struct pdf_doc *pdf, struct pdf_object *page,
                            float x, float y, float display_width,
                            float display_height,
                            const struct pdf_img_info *img_info,
                            const uint8_t *png_data, size_t png_data_length)
{
    // indicates if we return an error or add the img at the
    // end of the function
    bool success = false;

    // string stream used for writing color space (and palette) info
    // into the pdf
    struct dstr colour_space = INIT_DSTR;

    struct pdf_object *obj = NULL;
    uint8_t *final_data = NULL;
    int written = 0;
    uint32_t pos;
    uint8_t *png_data_temp = NULL;
    size_t png_data_total_length = 0;
    uint8_t ncolours;

    // Stores palette information for indexed PNGs
    struct rgb_value *palette_buffer = NULL;
    size_t palette_buffer_length = 0;

    const struct png_header *header = &img_info->png;

    // Father info from png header
    switch (header->colorType) {
    case PNG_COLOR_GREYSCALE:
        ncolours = 1;
        break;
    case PNG_COLOR_RGB:
        ncolours = 3;
        break;
    case PNG_COLOR_INDEXED:
        ncolours = 1;
        break;
    // PNG_COLOR_RGBA and PNG_COLOR_GREYSCALE_A are unsupported
    default:
        pdf_set_err(pdf, -EINVAL, "PNG has unsupported color type: %d",
                    header->colorType);
        goto free_buffers;
        break;
    }

    /* process PNG chunks */
    pos = sizeof(png_signature);

    while (1) {
        const struct png_chunk *chunk;

        chunk = (const struct png_chunk *)&png_data[pos];
        pos += sizeof(struct png_chunk);

        if (pos > png_data_length - 4) {
            pdf_set_err(pdf, -EINVAL, "PNG file too short");
            goto free_buffers;
        }
        const uint32_t chunk_length = ntoh32(chunk->length);
        // chunk length + 4-bytes of CRC
        if (chunk_length > png_data_length - pos - 4) {
            pdf_set_err(pdf, -EINVAL, "PNG chunk exceeds file: %d vs %ld",
                        chunk_length, png_data_length - pos - 4);
            goto free_buffers;
        }
        if (strncmp(chunk->type, png_chunk_header, 4) == 0) {
            // Ignoring the header, since it was parsed
            // before calling this function.
        } else if (strncmp(chunk->type, png_chunk_palette, 4) == 0) {
            // Palette chunk
            if (header->colorType == PNG_COLOR_INDEXED) {
                // palette chunk is needed for indexed images
                if (palette_buffer) {
                    pdf_set_err(pdf, -EINVAL,
                                "PNG contains multiple palette chunks");
                    goto free_buffers;
                }
                if (chunk_length % 3 != 0) {
                    pdf_set_err(pdf, -EINVAL,
                                "PNG format error: palette chunk length is "
                                "not divisbly by 3!");
                    goto free_buffers;
                }
                palette_buffer_length = (size_t)(chunk_length / 3);
                if (palette_buffer_length > 256 ||
                    palette_buffer_length == 0) {
                    pdf_set_err(pdf, -EINVAL,
                                "PNG palette length invalid: %zd",
                                palette_buffer_length);
                    goto free_buffers;
                }
                palette_buffer = (struct rgb_value *)malloc(
                    palette_buffer_length * sizeof(struct rgb_value));
                if (!palette_buffer) {
                    pdf_set_err(pdf, -ENOMEM,
                                "Could not allocate memory for indexed color "
                                "palette (%zu bytes)",
                                palette_buffer_length *
                                    sizeof(struct rgb_value));
                    goto free_buffers;
                }
                for (size_t i = 0; i < palette_buffer_length; i++) {
                    size_t offset = (i * 3) + pos;
                    palette_buffer[i].red = png_data[offset];
                    palette_buffer[i].green = png_data[offset + 1];
                    palette_buffer[i].blue = png_data[offset + 2];
                }
            } else if (header->colorType == PNG_COLOR_RGB ||
                       header->colorType == PNG_COLOR_RGBA) {
                // palette chunk is optional for RGB(A) images
                // but we do not process them
            } else {
                pdf_set_err(pdf, -EINVAL,
                            "Unexpected palette chunk for color type %d",
                            header->colorType);
                goto free_buffers;
            }
        } else if (strncmp(chunk->type, png_chunk_data, 4) == 0) {
            if (chunk_length > 0 && chunk_length < png_data_length - pos) {
                uint8_t *data = (uint8_t *)realloc(
                    png_data_temp, png_data_total_length + chunk_length);
                // (uint8_t *)realloc(info.data, info.length + chunk_length);
                if (!data) {
                    pdf_set_err(pdf, -ENOMEM, "No memory for PNG data");
                    goto free_buffers;
                }
                png_data_temp = data;
                memcpy(&png_data_temp[png_data_total_length], &png_data[pos],
                       chunk_length);
                png_data_total_length += chunk_length;
            }
        } else if (strncmp(chunk->type, png_chunk_end, 4) == 0) {
            /* end of file, exit */
            break;
        }

        if (chunk_length >= png_data_length) {
            pdf_set_err(pdf, -EINVAL, "PNG chunk length larger than file");
            goto free_buffers;
        }

        pos += chunk_length;     // add chunk length
        pos += sizeof(uint32_t); // add CRC length
    }

    /* if no length was found */
    if (png_data_total_length == 0) {
        pdf_set_err(pdf, -EINVAL, "PNG file has zero length");
        goto free_buffers;
    }

    switch (header->colorType) {
    case PNG_COLOR_GREYSCALE:
        dstr_append(&colour_space, "/DeviceGray");
        break;
    case PNG_COLOR_RGB:
        dstr_append(&colour_space, "/DeviceRGB");
        break;
    case PNG_COLOR_INDEXED:
        if (palette_buffer_length == 0) {
            pdf_set_err(pdf, -EINVAL, "Indexed PNG contains no palette");
            goto free_buffers;
        }
        // Write the color palette to the color_palette buffer
        dstr_printf(&colour_space,
                    "[ /Indexed\r\n"
                    "  /DeviceRGB\r\n"
                    "  %zu\r\n"
                    "  <",
                    palette_buffer_length - 1);
        // write individual paletter values
        // the index value for every RGB value is determined by its position
        // (0, 1, 2, ...)
        for (size_t i = 0; i < palette_buffer_length; i++) {
            dstr_printf(&colour_space, "%02X%02X%02X ", palette_buffer[i].red,
                        palette_buffer[i].green, palette_buffer[i].blue);
        }
        dstr_append(&colour_space, ">\r\n]");
        break;

    default:
        pdf_set_err(pdf, -EINVAL,
                    "Cannot map PNG color type %d to PDF color space",
                    header->colorType);
        goto free_buffers;
        break;
    }

    final_data = (uint8_t *)malloc(png_data_total_length + 1024 +
                                   dstr_len(&colour_space));
    if (!final_data) {
        pdf_set_err(pdf, -ENOMEM, "Unable to allocate PNG data %zu",
                    png_data_total_length + 1024 + dstr_len(&colour_space));
        goto free_buffers;
    }

    // Write image information to PDF
    written =
        sprintf((char *)final_data,
                "<<\r\n"
                "  /Type /XObject\r\n"
                "  /Name /Image%d\r\n"
                "  /Subtype /Image\r\n"
                "  /ColorSpace %s\r\n"
                "  /Width %u\r\n"
                "  /Height %u\r\n"
                "  /Interpolate true\r\n"
                "  /BitsPerComponent %u\r\n"
                "  /Filter /FlateDecode\r\n"
                "  /DecodeParms << /Predictor 15 /Colors %d "
                "/BitsPerComponent %u /Columns %u >>\r\n"
                "  /Length %zu\r\n"
                ">>stream\r\n",
                flexarray_size(&pdf->objects), dstr_data(&colour_space),
                header->width, header->height, header->bitDepth, ncolours,
                header->bitDepth, header->width, png_data_total_length);

    memcpy(&final_data[written], png_data_temp, png_data_total_length);
    written += png_data_total_length;
    written += sprintf((char *)&final_data[written], "\r\nendstream\r\n");

    obj = pdf_add_object(pdf, OBJ_image);
    if (!obj) {
        goto free_buffers;
    }

    dstr_append_data(&obj->stream.stream, final_data, written);

    if (get_img_display_dimensions(pdf, header->width, header->height,
                                   &display_width, &display_height)) {
        goto free_buffers;
    }
    success = true;

free_buffers:
    if (final_data)
        free(final_data);
    if (palette_buffer)
        free(palette_buffer);
    if (png_data_temp)
        free(png_data_temp);
    dstr_free(&colour_space);

    if (success)
        return pdf_add_image(pdf, page, obj, x, y, display_width,
                             display_height);
    else
        return pdf->errval;
}

static int parse_bmp_header(struct pdf_img_info *info, const uint8_t *data,
                            size_t data_length, char *err_msg,
                            size_t err_msg_length)
{
    if (data_length < sizeof(bmp_signature) + sizeof(struct bmp_header)) {
        snprintf(err_msg, err_msg_length, "File is too short");
        return -EINVAL;
    }

    if (memcmp(data, bmp_signature, sizeof(bmp_signature))) {
        snprintf(err_msg, err_msg_length, "File is not correct BMP file");
        return -EINVAL;
    }
    memcpy(&info->bmp, &data[sizeof(bmp_signature)],
           sizeof(struct bmp_header));
    if (info->bmp.biWidth < 0) {
        snprintf(err_msg, err_msg_length, "BMP has negative width");
        return -EINVAL;
    }
    if (info->bmp.biHeight == INT_MIN) {
        snprintf(err_msg, err_msg_length, "BMP height overflow");
        return -EINVAL;
    }
    info->width = info->bmp.biWidth;
    // biHeight might be negative (positive indicates vertically mirrored
    // lines)
    info->height = abs(info->bmp.biHeight);
    return 0;
}

static int pdf_add_bmp_data(struct pdf_doc *pdf, struct pdf_object *page,
                            float x, float y, float display_width,
                            float display_height,
                            const struct pdf_img_info *info,
                            const uint8_t *data, const size_t len)
{
    const struct bmp_header *header = &info->bmp;
    uint8_t *bmp_data = NULL;
    uint32_t bpp;
    size_t data_len;
    int retval;
    const uint32_t width = info->width;
    const uint32_t height = info->height;

    if (header->bfSize != len)
        return pdf_set_err(pdf, -EINVAL,
                           "BMP file seems to have wrong length");
    if (header->biSize != 40)
        return pdf_set_err(pdf, -EINVAL, "Wrong BMP header: biSize");
    if (header->biCompression != 0)
        return pdf_set_err(pdf, -EINVAL, "Wrong BMP compression value: %d",
                           header->biCompression);
    if (header->biWidth > MAX_IMAGE_WIDTH || header->biWidth <= 0 ||
        width > MAX_IMAGE_WIDTH || width == 0)
        return pdf_set_err(pdf, -EINVAL, "BMP has invalid width: %d",
                           header->biWidth);
    if (header->biHeight > MAX_IMAGE_HEIGHT ||
        header->biHeight < -MAX_IMAGE_HEIGHT || header->biHeight == 0 ||
        height > MAX_IMAGE_HEIGHT || height == 0)
        return pdf_set_err(pdf, -EINVAL, "BMP has invalid height: %d",
                           header->biHeight);
    if (header->biBitCount != 24 && header->biBitCount != 32)
        return pdf_set_err(pdf, -EINVAL, "Unsupported BMP bitdepth: %d",
                           header->biBitCount);
    bpp = header->biBitCount / 8;
    /* BMP rows are 4-bytes padded! */
    uint32_t stride = (width * bpp + 3) & ~3;
    data_len = (size_t)width * (size_t)height * 3;

    if (header->bfOffBits >= len)
        return pdf_set_err(pdf, -EINVAL, "Invalid BMP image offset");

    if (len - header->bfOffBits < (size_t)height * stride)
        return pdf_set_err(pdf, -EINVAL, "Wrong BMP image size");

    if (bpp == 3) {
        /* 24 bits: change R and B colors */
        bmp_data = (uint8_t *)malloc(data_len);
        if (!bmp_data)
            return pdf_set_err(pdf, -ENOMEM,
                               "Insufficient memory for bitmap");
        for (uint32_t pos = 0; pos < width * height; pos++) {
            uint32_t src_pos = header->bfOffBits + (pos / width) * stride +
                               (pos % width) * 3;

            bmp_data[pos * 3] = data[src_pos + 2];
            bmp_data[pos * 3 + 1] = data[src_pos + 1];
            bmp_data[pos * 3 + 2] = data[src_pos];
        }
    } else if (bpp == 4) {
        /* 32 bits: change R and B colors, remove key color */
        int offs = 0;
        bmp_data = (uint8_t *)malloc(data_len);
        if (!bmp_data)
            return pdf_set_err(pdf, -ENOMEM,
                               "Insufficient memory for bitmap");

        for (uint32_t pos = 0; pos < width * height * 4; pos += 4) {
            bmp_data[offs] = data[header->bfOffBits + pos + 2];
            bmp_data[offs + 1] = data[header->bfOffBits + pos + 1];
            bmp_data[offs + 2] = data[header->bfOffBits + pos];
            offs += 3;
        }
    } else {
        return pdf_set_err(pdf, -EINVAL, "Unsupported BMP bitdepth: %d",
                           header->biBitCount);
    }
    if (header->biHeight >= 0) {
        // BMP has vertically mirrored representation of lines, so swap them
        uint8_t *line = (uint8_t *)malloc(width * 3);
        if (!line) {
            free(bmp_data);
            return pdf_set_err(pdf, -ENOMEM,
                               "Unable to allocate memory for bitmap mirror");
        }
        for (uint32_t pos = 0; pos < (height / 2); pos++) {
            memcpy(line, &bmp_data[pos * width * 3], width * 3);
            memcpy(&bmp_data[pos * width * 3],
                   &bmp_data[(height - pos - 1) * width * 3], width * 3);
            memcpy(&bmp_data[(height - pos - 1) * width * 3], line,
                   width * 3);
        }
        free(line);
    }

    retval = pdf_add_rgb24(pdf, page, x, y, display_width, display_height,
                           bmp_data, width, height);
    free(bmp_data);

    return retval;
}

static int determine_image_format(const uint8_t *data, size_t length)
{
    if (length >= sizeof(png_signature) &&
        memcmp(data, png_signature, sizeof(png_signature)) == 0)
        return IMAGE_PNG;
    if (length >= sizeof(bmp_signature) &&
        memcmp(data, bmp_signature, sizeof(bmp_signature)) == 0)
        return IMAGE_BMP;
    if (length >= sizeof(jpeg_signature) &&
        memcmp(data, jpeg_signature, sizeof(jpeg_signature)) == 0)
        return IMAGE_JPG;
    if (length >= sizeof(ppm_signature) &&
        memcmp(data, ppm_signature, sizeof(ppm_signature)) == 0)
        return IMAGE_PPM;
    if (length >= sizeof(pgm_signature) &&
        memcmp(data, pgm_signature, sizeof(pgm_signature)) == 0)
        return IMAGE_PPM;

    return IMAGE_UNKNOWN;
}

int pdf_parse_image_header(struct pdf_img_info *info, const uint8_t *data,
                           size_t length, char *err_msg,
                           size_t err_msg_length)

{
    const int image_format = determine_image_format(data, length);
    info->image_format = image_format;
    switch (image_format) {
    case IMAGE_PNG:
        return parse_png_header(info, data, length, err_msg, err_msg_length);
    case IMAGE_BMP:
        return parse_bmp_header(info, data, length, err_msg, err_msg_length);
    case IMAGE_JPG:
        return parse_jpeg_header(info, data, length, err_msg, err_msg_length);
    case IMAGE_PPM:
        return parse_ppm_header(info, data, length, err_msg, err_msg_length);

    case IMAGE_UNKNOWN:
    default:
        snprintf(err_msg, err_msg_length, "Unknown file format");
        return -EINVAL;
    }
}

int pdf_add_image_data(struct pdf_doc *pdf, struct pdf_object *page, float x,
                       float y, float display_width, float display_height,
                       const uint8_t *data, size_t len)
{
    struct pdf_img_info info = {
        .image_format = IMAGE_UNKNOWN,
        .width = 0,
        .height = 0,
        .jpeg = {0},
    };

    int ret = pdf_parse_image_header(&info, data, len, pdf->errstr,
                                     sizeof(pdf->errstr));
    if (ret)
        return ret;

    // Try and determine which image format it is based on the content
    switch (info.image_format) {
    case IMAGE_PNG:
        return pdf_add_png_data(pdf, page, x, y, display_width,
                                display_height, &info, data, len);
    case IMAGE_BMP:
        return pdf_add_bmp_data(pdf, page, x, y, display_width,
                                display_height, &info, data, len);
    case IMAGE_JPG:
        return pdf_add_jpeg_data(pdf, page, x, y, display_width,
                                 display_height, &info, data, len);
    case IMAGE_PPM:
        return pdf_add_ppm_data(pdf, page, x, y, display_width,
                                display_height, &info, data, len);

    // This case should be caught in parse_image_header, but is checked
    // here again for safety
    case IMAGE_UNKNOWN:
    default:
        return pdf_set_err(pdf, -EINVAL, "Unable to determine image format");
    }
}

int pdf_add_image_file(struct pdf_doc *pdf, struct pdf_object *page, float x,
                       float y, float display_width, float display_height,
                       const char *image_filename)
{
    size_t len;
    uint8_t *data;
    int ret = 0;

    data = get_file(pdf, image_filename, &len);
    if (data == NULL)
        return pdf_get_errval(pdf);

    ret = pdf_add_image_data(pdf, page, x, y, display_width, display_height,
                             data, len);
    free(data);
    return ret;
}

int pdf_save_encrypted(struct pdf_doc *pdf, const char *filename,
                       const char *password)
{
    FILE *fp;
    int e;

    if (filename == NULL)
        fp = stdout;
    else if ((fp = fopen(filename, "wb")) == NULL)
        return pdf_set_err(pdf, -errno, "Unable to open '%s': %s", filename,
                           strerror(errno));

    e = pdf_save_file_encrypted(pdf, fp, password);

    if (fp != stdout) {
        if (fclose(fp) != 0)
            return pdf_set_err(pdf, -errno, "Unable to close '%s': %s",
                               filename, strerror(errno));
    }

    return e;
}
