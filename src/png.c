#include <sys/time.h>
#include <limits.h>
#include <stdlib.h>
#include <ttypt/stele.h>
#include <png.h>

#include "tex.h"

unsigned
pngi_load(const char *filename)
{
	FILE *fp = fopen(filename, "rb");
	unsigned char header[8];
	png_structp png;
	uint8_t *data;
	uint32_t w, h;
	png_bytep *rows;
	int color_type, bit_depth;
	unsigned ref;

	CBUG(!fp, "fopen");

	fread(header, 1, 8, fp);
	CBUG(png_sig_cmp(header, 0, 8), "Not a PNG file");

	 png = png_create_read_struct(
			 PNG_LIBPNG_VER_STRING,
			 NULL, NULL, NULL);

	 CBUG(!png, "png_create_read_struct");

	 png_infop info = png_create_info_struct(png);
	 CBUG(!info, "png_create_info_struct");

	 CBUG(setjmp(png_jmpbuf(png)), "setjmp");

	 png_init_io(png, fp);
	 png_set_sig_bytes(png, 8);
	 png_read_info(png, info);

	 w = png_get_image_width(png, info);
	 h = png_get_image_height(png, info);
	 ref = img_new(&data, filename, w, h, IMG_LOAD);

	 color_type = png_get_color_type(png, info);
	 bit_depth = png_get_bit_depth(png, info);

	 if (bit_depth == 16)
		 png_set_strip_16(png);

	 if (color_type == PNG_COLOR_TYPE_PALETTE)
		 png_set_palette_to_rgb(png);

	 if (color_type == PNG_COLOR_TYPE_GRAY
			 && bit_depth < 8)
		 png_set_expand_gray_1_2_4_to_8(png);

	 if (png_get_valid(png, info, PNG_INFO_tRNS))
		 png_set_tRNS_to_alpha(png);

	 png_set_filler(png, 0xFF, PNG_FILLER_AFTER);


	 png_read_update_info(png, info);

	 rows = malloc(sizeof(png_bytep) * h);

	 for (uint32_t y = 0; y < h; y++)
		 rows[y] = malloc(png_get_rowbytes(png,
					 info));

	 png_read_image(png, rows);

	 uint8_t *pos = data;

	 for (uint32_t y = 0; y < h; y++) {
		 png_bytep row = rows[y];

		 for (uint32_t x = 0; x < w; x++, pos += 4) {
			 png_byte *pixel = &(row[x * 4]);
			 pos[0] = pixel[0];
			 pos[1] = pixel[1];
			 pos[2] = pixel[2];
			 pos[3] = pixel[3];
		 }
	 }

	 lam_tex_upd(ref, 0, 0, w, h, data);

	 fclose(fp);
	 png_destroy_read_struct(&png, &info, NULL);

	 for (uint32_t y = 0; y < h; y++)
		 free(rows[y]);

	 free(rows);

	 return ref;
}

int
pngi_save(const char *filename,
           const uint8_t *data,
           uint32_t w, uint32_t h)
{
    FILE *fp = fopen(filename, "wb");
    png_structp png = NULL;
    png_infop info = NULL;
    png_bytep *rows = NULL;

    CBUG(!fp, "fopen");

    png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    CBUG(!png, "png_create_write_struct");

    info = png_create_info_struct(png);
    CBUG(!info, "png_create_info_struct");

    CBUG(setjmp(png_jmpbuf(png)), "setjmp");

    png_init_io(png, fp);

    png_set_IHDR(png, info,
                 w, h,
                 8,                         /* bit depth */
                 PNG_COLOR_TYPE_RGBA,       /* RGBA */
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);

    png_write_info(png, info);

    rows = malloc(sizeof(png_bytep) * h);
    CBUG(!rows, "malloc rows");

    for (uint32_t y = 0; y < h; y++)
        rows[y] = (png_bytep)(data + (size_t)y * w * 4);

    png_write_image(png, rows);
    png_write_end(png, NULL);

    free(rows);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return 0;
}

void
png_construct(void) {
	img_be_load("png", pngi_load, pngi_save);
}

__attribute__((constructor))
static void
construct(void) {
	png_construct();
}
