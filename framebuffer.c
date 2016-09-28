typedef struct
{
	uint8_t *pixels;
	uint32_t width, height;
	uint32_t bytesPerPixel;
} framebuffer_t;

static void framebuffer_init(framebuffer_t *framebuffer, int width, int height, int bytesPerPixel)
{
	framebuffer->width = width;
	framebuffer->height = height;
	framebuffer->bytesPerPixel = bytesPerPixel;
	framebuffer->pixels = calloc(width * height, bytesPerPixel);
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
