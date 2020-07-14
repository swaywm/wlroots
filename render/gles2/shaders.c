#include <GLES2/gl2.h>
#include "render/gles2.h"

// Colored quads
const GLchar quad_vertex_src[] =
"uniform mat3 proj;\n"
"uniform vec4 color;\n"
"attribute vec2 pos;\n"
"attribute vec2 texcoord;\n"
"varying vec4 v_color;\n"
"varying vec2 v_texcoord;\n"
"\n"
"void main() {\n"
"	gl_Position = vec4(proj * vec3(pos, 1.0), 1.0);\n"
"	v_color = color;\n"
"	v_texcoord = texcoord;\n"
"}\n";

const GLchar quad_fragment_src[] =
"precision mediump float;\n"
"varying vec4 v_color;\n"
"varying vec2 v_texcoord;\n"
"\n"
"void main() {\n"
"	gl_FragColor = v_color;\n"
"}\n";

// Colored ellipses
const GLchar ellipse_fragment_src[] =
"precision mediump float;\n"
"varying vec4 v_color;\n"
"varying vec2 v_texcoord;\n"
"\n"
"void main() {\n"
"	float l = length(v_texcoord - vec2(0.5, 0.5));\n"
"	if (l > 0.5) {\n"
"		discard;\n"
"	}\n"
"	gl_FragColor = v_color;\n"
"}\n";

// Textured quads
const GLchar tex_vertex_src[] =
"uniform mat3 proj;\n"
"uniform bool invert_y;\n"
"attribute vec2 pos;\n"
"attribute vec2 texcoord;\n"
"varying vec2 v_texcoord;\n"
"\n"
"void main() {\n"
"	gl_Position = vec4(proj * vec3(pos, 1.0), 1.0);\n"
"	if (invert_y) {\n"
"		v_texcoord = vec2(texcoord.x, 1.0 - texcoord.y);\n"
"	} else {\n"
"		v_texcoord = texcoord;\n"
"	}\n"
"}\n";

static const GLchar tex_fragment_head[] =
"#extension GL_OES_texture_3D : require\n"
"\n"
"precision mediump float;\n"
"varying vec2 v_texcoord;\n"
"uniform sampler2D tex;\n"
"uniform float alpha;\n";

static const GLchar tex_fragment_util[] =
"uniform   mediump sampler3D  color_table;\n"
"uniform   bool        color_enable;\n"
"#define offset " COLOR_OFFSET "\n"
"\n"
"#ifdef GL_OES_texture_3D\n"
"vec4 lookup(vec4 c) {\n"
"	if(! color_enable)\n"
"		return c;\n"
"	vec4 m = texture3D(color_table, offset + c.rgb * (1.0 - offset*2.0));\n"
"	return vec4(m.rgb, c.a);\n"
"}\n"
"#else\n"
"#define lookup(c) c\n"
"#endif\n"
;

struct wlr_gles2_tex_shader tex_fragment_src_rgba = {
	.head = tex_fragment_head,
	.util = tex_fragment_util,
	.main = "gl_FragColor = lookup(texture2D(tex, v_texcoord)) * alpha;"
};

struct wlr_gles2_tex_shader tex_fragment_src_rgbx = {
	.head = tex_fragment_head,
	.util = tex_fragment_util,
	.main = "gl_FragColor = lookup(vec4(texture2D(tex, v_texcoord).rgb, 1.0)) * alpha;"
};

struct wlr_gles2_tex_shader tex_fragment_src_external = {
	.head = "#extension GL_OES_EGL_image_external : require\n"
		"#extension GL_OES_texture_3D : enable\n"
		"\n"
		"precision mediump float;\n"
		"varying vec2 v_texcoord;\n"
		"uniform samplerExternalOES texture0;\n"
		"uniform float alpha;\n",
	.util = tex_fragment_util,
	.main = "gl_FragColor = lookup(texture2D(texture0, v_texcoord)) * alpha;\n"
};
