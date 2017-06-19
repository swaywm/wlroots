#include "render/gles3.h"
#include <GLES3/gl3.h>

// Colored quads
const GLchar quad_vertex_src[] =
"uniform mat4 proj;"
"uniform vec4 color;"
"attribute vec2 pos;"
"attribute vec2 texcoord;"
"varying vec4 v_color;"
"varying vec2 v_texcoord;"
"void main() {"
"  gl_Position = proj * vec4(pos, 0.0, 1.0);"
"  v_color = color;"
"  v_texcoord = texcoord;"
"}";

const GLchar quad_fragment_src[] =
"precision mediump float;"
"varying vec4 v_color;"
"varying vec2 v_texcoord;"
"void main() {"
"  gl_FragColor = v_color;"
"}";

// Colored ellipses (TODO)

const GLchar ellipse_fragment_src[] =
"precision mediump float;"
"varying vec4 v_color;"
"varying vec2 v_texcoord;"
"void main() {"
"  float l = length(v_texcoord - vec2(0.5, 0.5));"
"  if (l > 0.5) discard;"
"  gl_FragColor = v_color;"
"}";

// Textured quads
const GLchar vertex_src[] =
"uniform mat4 proj;"
"attribute vec2 pos;"
"attribute vec2 texcoord;"
"varying vec2 v_texcoord;"
"void main() {"
"	gl_Position = proj * vec4(pos, 0.0, 1.0);"
"	v_texcoord = texcoord;"
"}";

const GLchar fragment_src_RGB[] =
"precision mediump float;"
"varying vec2 v_texcoord;"
"uniform sampler2D tex;"
"void main() {"
"	gl_FragColor = vec4(texture2D(tex, v_texcoord).rgb, 1.0);"
"}";

const GLchar fragment_src_RGBA[] =
"precision mediump float;"
"varying vec2 v_texcoord;"
"uniform sampler2D tex;"
"void main() {"
"	gl_FragColor = texture2D(tex, v_texcoord);"
"}";
