#include "render/gles2.h"
#include <GLES2/gl2.h>

// Colored quads
const GLchar quad_vertex_src[] =
"uniform mat4 proj;"
"uniform vec4 color;"
"attribute vec2 pos;"
"attribute vec2 texcoord;"
"varying vec4 v_color;"
"varying vec2 v_texcoord;"
"mat4 transpose(in mat4 inMatrix) {"
"    vec4 i0 = inMatrix[0];"
"    vec4 i1 = inMatrix[1];"
"    vec4 i2 = inMatrix[2];"
"    vec4 i3 = inMatrix[3];"
"    mat4 outMatrix = mat4("
"                 vec4(i0.x, i1.x, i2.x, i3.x),"
"                 vec4(i0.y, i1.y, i2.y, i3.y),"
"                 vec4(i0.z, i1.z, i2.z, i3.z),"
"                 vec4(i0.w, i1.w, i2.w, i3.w)"
"                 );"
"    return outMatrix;"
"}"
"void main() {"
"  gl_Position = transpose(proj) * vec4(pos, 0.0, 1.0);"
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

// Colored ellipses
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
"mat4 transpose(in mat4 inMatrix) {"
"    vec4 i0 = inMatrix[0];"
"    vec4 i1 = inMatrix[1];"
"    vec4 i2 = inMatrix[2];"
"    vec4 i3 = inMatrix[3];"
"    mat4 outMatrix = mat4("
"                 vec4(i0.x, i1.x, i2.x, i3.x),"
"                 vec4(i0.y, i1.y, i2.y, i3.y),"
"                 vec4(i0.z, i1.z, i2.z, i3.z),"
"                 vec4(i0.w, i1.w, i2.w, i3.w)"
"                 );"
""
"    return outMatrix;"
"}"
"void main() {"
"	gl_Position = transpose(proj) * vec4(pos, 0.0, 1.0);"
"	v_texcoord = texcoord;"
"}";

const GLchar fragment_src_rgba[] =
"precision mediump float;"
"varying vec2 v_texcoord;"
"uniform sampler2D tex;"
"uniform float alpha;"
"void main() {"
"	gl_FragColor = alpha * texture2D(tex, v_texcoord);"
"}";

const GLchar fragment_src_rgbx[] =
"precision mediump float;"
"varying vec2 v_texcoord;"
"uniform sampler2D tex;"
"uniform float alpha;"
"void main() {"
"   gl_FragColor.rgb = alpha * texture2D(tex, v_texcoord).rgb;"
"   gl_FragColor.a = alpha;"
"}";
