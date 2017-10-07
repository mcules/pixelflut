#include "common.c"
#include <termios.h>
#include <fcntl.h>
#include "bcm_host.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

static EGLDisplay display;
static EGLSurface surface;
static EGLConfig config;
static EGLContext context;
static DISPMANX_DISPLAY_HANDLE_T dispman_display;
static DISPMANX_UPDATE_HANDLE_T dispman_update;
static DISPMANX_ELEMENT_HANDLE_T dispman_element;
static EGL_DISPMANX_WINDOW_T nativewindow;
static VC_RECT_T dst_rect;
static VC_RECT_T src_rect;
static void LoadOpenGLESWindow(uint32_t width, uint32_t height)
{
	bcm_host_init();

	display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	eglInitialize(display, NULL, NULL);

	static const EGLint attribute_list[] =
	{
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_NONE
	};
	EGLint num_config;
	eglChooseConfig(display, attribute_list, &config, 1, &num_config);

	eglBindAPI(EGL_OPENGL_ES_API);

	static const EGLint context_attributes[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);

	dst_rect.x = 0;
	dst_rect.y = 0;
	dst_rect.width = width;
	dst_rect.height = height;

	src_rect.x = 0;
	src_rect.y = 0;
	src_rect.width = width << 16;
	src_rect.height = height << 16;

	dispman_display = vc_dispmanx_display_open( 0 /* LCD */);
	dispman_update = vc_dispmanx_update_start( 0 );
	dispman_element = vc_dispmanx_element_add(
		dispman_update, dispman_display,
		0/*layer*/, &dst_rect, 0/*src*/,
		&src_rect, DISPMANX_PROTECTION_NONE, 
		0 /*alpha*/, 0/*clamp*/, 0/*transform*/);
	nativewindow.element = dispman_element;
	nativewindow.width = width;
	nativewindow.height = height;
	vc_dispmanx_update_submit_sync(dispman_update);

	surface = eglCreateWindowSurface(display, config, &nativewindow, NULL);
	eglMakeCurrent(display, surface, surface, context);
}

static GLuint LoadShader(GLenum type, const char *shaderSrc)
{
	GLuint shader = glCreateShader(type);
	if(shader == 0) return 0;
	glShaderSource(shader, 1, &shaderSrc, NULL);
	glCompileShader(shader);
	GLint compiled;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if(!compiled)
	{
		GLint infoLen = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
		if(infoLen > 1)
		{
			char* infoLog = malloc(sizeof(char) * infoLen);
			glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
			fprintf(stderr, "Error compiling shader:\n%s\n", infoLog);
			free(infoLog);
		}
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

static GLuint LoadProgram(const char *vp, const char *fp)
{
	GLuint vertexShader = LoadShader(GL_VERTEX_SHADER, vp);
	GLuint fragmentShader = LoadShader(GL_FRAGMENT_SHADER, fp);
	GLuint programObject = glCreateProgram();
	if(programObject == 0) return 0;
	glAttachShader(programObject, vertexShader);
	glAttachShader(programObject, fragmentShader);
	glBindAttribLocation(programObject, 0, "vPosition");
	glBindAttribLocation(programObject, 1, "vTexcoord");
	glLinkProgram(programObject);
	GLint linked;
	glGetProgramiv(programObject, GL_LINK_STATUS, &linked);
	if(!linked)
	{
		GLint infoLen = 0;
		glGetProgramiv(programObject, GL_INFO_LOG_LENGTH, &infoLen);
		if(infoLen > 1)
		{
			char* infoLog = malloc(sizeof(char) * infoLen);
			glGetProgramInfoLog(programObject, infoLen, NULL, infoLog);
			fprintf(stderr, "Error linking program:\n%s\n", infoLog);
			free(infoLog);
		}
		glDeleteProgram(programObject);
		return 0;
	}
	return programObject;
}

// overscan compensation:
#define ox (22.0f / (1920.0f / 2.0f))
#define oy (58.0f / (1080.0f / 2.0f))
static const GLfloat quad_vertices[] =
{
	-1.0f + ox,  1.0f - oy,
	 1.0f - ox,  1.0f - oy,
	-1.0f + ox, -1.0f + oy,
	 1.0f - ox, -1.0f + oy,
	-1.0f + ox, -1.0f + oy,
	 1.0f - ox,  1.0f - oy
};

static const GLfloat quad_texcoord[] =
{
	0.0f, 0.0f,
	1.0f, 0.0f,
	0.0f, 1.0f,
	1.0f, 1.0f,
	0.0f, 1.0f,
	1.0f, 0.0f
};

static const char vertex_shader[] =
	"attribute vec2 vPosition;\n"
	"attribute vec2 vTexcoord;\n"
	"varying vec2 uv;\n"
	"void main()\n"
	"{\n"
	    "gl_Position = vec4(vPosition, 0, 1);\n"
	    "uv = vTexcoord;\n"
	"}\n";
static const char fragment_shader[] =
	"precision mediump float;\n"
	"varying vec2 uv;\n"
	"uniform sampler2D tex;\n"
	"void main()\n"
	"{\n"
	    "gl_FragColor = vec4(texture2D(tex, uv).rgb, 1.0); \n"
	"}\n";

int kbhit(void)
{
	struct termios oldt, newt;
	int ch;
	int oldf;

	tcgetattr(STDIN_FILENO, &oldt);
	newt = oldt;
	newt.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

	ch = getchar();

	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	fcntl(STDIN_FILENO, F_SETFL, oldf);

	if(ch != EOF)
	{
		ungetc(ch, stdin);
		return 1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	uint32_t width = 1920, height = 1080;
	LoadOpenGLESWindow(width, height);

	// initial opengl state
	glViewport(0, 0, width, height);

	// texture
	GLuint texture;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, PIXEL_WIDTH, PIXEL_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	bytesPerPixel = 3;

	// load program
	GLuint programObject = LoadProgram(vertex_shader, fragment_shader);
	glUseProgram(programObject);
	glUniform1i(glGetUniformLocation(programObject, "tex"), 0);

	// load quad data
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad_vertices);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, quad_texcoord);
	glEnableVertexAttribArray(1);

	if (!server_start())
		return 1;

	while(!kbhit())
	{
		update_pixels();
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, PIXEL_WIDTH, PIXEL_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		eglSwapBuffers(display, surface);
	}

	server_stop();
	return 0;
}
