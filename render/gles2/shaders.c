#include "render/gles2.h"
#include <GLES2/gl2.h>

// Colored quads
const GLchar quad_vertex_src[] =
"uniform mat4 proj;\n"
"uniform vec4 color;\n"
"attribute vec2 pos;\n"
"attribute vec2 texcoord;\n"
"varying vec4 v_color;\n"
"varying vec2 v_texcoord;\n"
"void main() {\n"
"	gl_Position = proj * vec4(pos, 0.0, 1.0);\n"
"	v_color = color;\n"
"	v_texcoord = texcoord;\n"
"}\n";

const GLchar quad_fragment_src[] =
"precision mediump float;\n"
"varying vec4 v_color;\n"
"varying vec2 v_texcoord;\n"
"void main() {\n"
"	gl_FragColor = v_color;\n"
"}\n";

// Colored ellipses
const GLchar ellipse_fragment_src[] =
"precision mediump float;\n"
"varying vec4 v_color;\n"
"varying vec2 v_texcoord;\n"
"void main() {\n"
"	float l = length(v_texcoord - vec2(0.5, 0.5));\n"
"	if (l > 0.5) discard;\n"
"	gl_FragColor = v_color;\n"
"}\n";

// Textured quads
const GLchar vertex_src[] =
"uniform mat4 proj;\n"
"attribute vec2 pos;\n"
"attribute vec2 texcoord;\n"
"varying vec2 v_texcoord;\n"
"void main() {\n"
"	gl_Position = proj * vec4(pos, 0.0, 1.0);\n"
"	v_texcoord = texcoord;\n"
"}\n";

const GLchar fragment_src_rgba[] =
"precision mediump float;\n"
"varying vec2 v_texcoord;\n"
"uniform sampler2D tex;\n"
"uniform float alpha;\n"
"void main() {\n"
"	gl_FragColor = alpha * texture2D(tex, v_texcoord);\n"
"}\n";

const GLchar fragment_src_rgbx[] =
"precision mediump float;\n"
"varying vec2 v_texcoord;\n"
"uniform sampler2D tex;\n"
"uniform float alpha;\n"
"void main() {\n"
"	gl_FragColor.rgb = alpha * texture2D(tex, v_texcoord).rgb;\n"
"	gl_FragColor.a = alpha;\n"
"}";

const GLchar fragment_src_external[] =
"#extension GL_OES_EGL_image_external : require\n"
"precision mediump float;\n"
"varying vec2 v_texcoord;\n"
"uniform samplerExternalOES texture0;\n"
"void main() {\n"
"	vec4 col = texture2D(texture0, v_texcoord);\n"
"	gl_FragColor = vec4(col.rgb, col.a);\n"
"}\n";
