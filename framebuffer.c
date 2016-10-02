#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

typedef struct
{
	uint8_t *pixels;
	uint32_t width, height;
	uint32_t bytesPerPixel;
	uint8_t ttf_buffer[1<<25];
	stbtt_fontinfo font;
} framebuffer_t;

static void framebuffer_init(framebuffer_t *framebuffer, int width, int height, int bytesPerPixel)
{
	framebuffer->width = width;
	framebuffer->height = height;
	framebuffer->bytesPerPixel = bytesPerPixel;
	framebuffer->pixels = calloc(width * height, bytesPerPixel);

	FILE *file = fopen("Anonymous_Pro.ttf", "rb");
	if (!file || !fread(framebuffer->ttf_buffer, 1, 1<<25, file))
	{
		fprintf(stderr, "ERROR: Could not open font file.\n");
		exit(1);
	}
	fclose(file);
	
	stbtt_InitFont(&framebuffer->font, framebuffer->ttf_buffer, stbtt_GetFontOffsetForIndex(framebuffer->ttf_buffer, 0));
}

static void framebuffer_free(framebuffer_t *framebuffer)
{
	free(framebuffer->pixels);
	framebuffer->pixels = 0;
}

static void framebuffer_fade_out(framebuffer_t *framebuffer)
{
	uint8_t *pixel = framebuffer->pixels;
	for (uint32_t i = 0; i < framebuffer->width * framebuffer->height; i++)
	{
		pixel[0] = pixel[0] ? pixel[0] - 1 : pixel[0];
		pixel[1] = pixel[1] ? pixel[1] - 1 : pixel[1];
		pixel[2] = pixel[2] ? pixel[2] - 1 : pixel[2];
		pixel += framebuffer->bytesPerPixel;
	}
}

static void framebuffer_draw_rect(
	framebuffer_t *framebuffer,
	int x, int y, int w, int h,
	uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	uint32_t na = 255 - a;
	uint32_t ar = a * (uint32_t)r;
	uint32_t ag = a * (uint32_t)g;
	uint32_t ab = a * (uint32_t)b;
	for (int cy = y; cy < y + h; cy++)
	{
		uint8_t *p = framebuffer->pixels + (cy * framebuffer->width + x) * framebuffer->bytesPerPixel;
		for (int cx = x; cx < x + w; cx++)
		{
			p[0] = (uint8_t)((ar + na * (uint32_t)p[0]) >> 8);
			p[1] = (uint8_t)((ag + na * (uint32_t)p[1]) >> 8);
			p[2] = (uint8_t)((ab + na * (uint32_t)p[2]) >> 8);
			p += framebuffer->bytesPerPixel;
		}
	}
}

static void framebuffer_measure_text(
	framebuffer_t *framebuffer,
	char *text, int size, int *w, int *h)
{
	float scale = stbtt_ScaleForPixelHeight(&framebuffer->font, size);
	int ascent, descent;
	stbtt_GetFontVMetrics(&framebuffer->font, &ascent, &descent, 0);
	float xpos = 2;
	while (*text)
	{
		int advance, lsb;
		stbtt_GetCodepointHMetrics(&framebuffer->font, *text, &advance, &lsb);
		xpos += advance * scale;
		if (text[1])
			xpos += scale * stbtt_GetCodepointKernAdvance(&framebuffer->font, text[0], text[1]);
		text++;
	}
	*w = xpos + 2;
	*h = (int)ceilf(scale * (ascent - descent));
}

static void framebuffer_write_text(
	framebuffer_t *framebuffer,
	int x, int y, char *text, int size,
	uint8_t r, uint8_t g, uint8_t b)
{
	int screen_w = strlen(text) * size + 10;
	int screen_h = size + 10;
	uint8_t *screen = alloca(screen_w * screen_h);
	memset(screen, 0, screen_w * screen_h);

	float scale = stbtt_ScaleForPixelHeight(&framebuffer->font, size);
	int ascent;
	stbtt_GetFontVMetrics(&framebuffer->font, &ascent, 0, 0);
	int baseline = (int)(ascent * scale);
	float xpos = 2;
	while (*text)
	{
		int advance, lsb, x0, y0, x1, y1;
		float x_shift = xpos - (float)floor(xpos);
		stbtt_GetCodepointHMetrics(&framebuffer->font, *text, &advance, &lsb);
		stbtt_GetCodepointBitmapBoxSubpixel(&framebuffer->font, *text, scale, scale, x_shift, 0, &x0, &y0, &x1, &y1);
		stbtt_MakeCodepointBitmapSubpixel(&framebuffer->font, &screen[(baseline + y0) * screen_w + (int)xpos + x0], x1 - x0, y1 - y0, screen_w, scale, scale, x_shift, 0, *text);
		xpos += advance * scale;
		if (text[1])
			xpos += scale * stbtt_GetCodepointKernAdvance(&framebuffer->font, text[0], text[1]);
		text++;
	}
	
	for (int cy = y; cy < y + screen_h; cy++)
	{
		uint8_t *p = framebuffer->pixels + (cy * framebuffer->width + x) * framebuffer->bytesPerPixel;
		for (int cx = x; cx < x + screen_w; cx++)
		{
			uint32_t a = *screen++;
			if (a)
			{
				uint32_t na = 255 - a;
				p[0] = (uint8_t)((a * (uint32_t)r + na * (uint32_t)p[0]) >> 8);
				p[1] = (uint8_t)((a * (uint32_t)g + na * (uint32_t)p[1]) >> 8);
				p[2] = (uint8_t)((a * (uint32_t)b + na * (uint32_t)p[2]) >> 8);
			}
			p += framebuffer->bytesPerPixel;
		}
	}
}

static void framebuffer_write_text_with_background(
	framebuffer_t *framebuffer,
	int x, int y, char *text, int size,
	uint8_t r, uint8_t g, uint8_t b,
	uint8_t br, uint8_t bg, uint8_t bb, uint8_t ba)
{
	int w, h;
	framebuffer_measure_text(framebuffer, text, size, &w, &h);
	framebuffer_draw_rect(framebuffer, x, y, w, h, br, bg, bb, ba);
	framebuffer_write_text(framebuffer, x, y, text, size, r, g, b);
}
