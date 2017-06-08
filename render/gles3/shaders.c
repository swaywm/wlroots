#include "render/gles3.h"
#include <GLES3/gl3.h>

const GLchar vertex_src[] =
"uniform mat4 proj;\n"
"attribute vec2 pos;\n"
"attribute vec2 texcoord;\n"
"varying vec2 v_texcoord;\n"
"void main() {\n"
"	gl_Position = proj * vec4(pos, 0.0, 1.0);\n"
"	v_texcoord = texcoord;\n"
"}\n";

const GLchar fragment_src_RGB[] =
"precision mediump float;\n"
"varying vec2 v_texcoord;\n"
"uniform sampler2D tex;\n"
"void main() {\n"
"	gl_FragColor = vec4(texture2D(tex, v_texcoord).rgb, 1.0);\n"
"}\n";

const GLchar fragment_src_RGBA[] =
"precision mediump float;\n"
"varying vec2 v_texcoord;\n"
"uniform sampler2D tex;\n"
"void main() {\n"
"	gl_FragColor = texture2D(tex, v_texcoord);\n"
"}\n";
