typedef struct
{
	uint8_t* index_html;
	int index_html_len;
	uint32_t buckets[8][8][8];
} histogram_t;

static void histogram_init(histogram_t *histogram)
{
	const char http_ok[] = "HTTP/1.1 200 OK\r\n\r\n";

	FILE *file = fopen("index.html", "rt");
	if (!file)
	{
		fprintf(stderr, "Could not open index.html!\n");
		return;
	}
	fseek(file, 0, SEEK_END);
	long len = ftell(file);
	fseek(file, 0, SEEK_SET);

	histogram->index_html_len = len + sizeof(http_ok) - 1;
	histogram->index_html = malloc(histogram->index_html_len);
	memcpy(histogram->index_html, http_ok, sizeof(http_ok) - 1);
	int read = fread(histogram->index_html + sizeof(http_ok) - 1, 1, len, file);
	if (!read)
		fprintf(stderr, "Could not read index.html!\n");
	fclose(file);
}

static void histogram_free(histogram_t *histogram)
{
	free(histogram->index_html);
	histogram->index_html = 0;
}

static void histogram_update(histogram_t *histogram)
{
	uint32_t *buckets = histogram->buckets[0][0];
	for (int i = 0; i < 8 * 8 * 8; i++)
		buckets[i] *= 0.99;
}
