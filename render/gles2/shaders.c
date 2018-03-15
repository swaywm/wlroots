#include <GLES2/gl2.h>
#include "render/gles2.h"

// Colored quads
const GLchar quad_vertex_src[] =
"uniform mat3 proj;"
"uniform vec4 color;"
"attribute vec2 pos;"
"attribute vec2 texcoord;"
"varying vec4 v_color;"
"varying vec2 v_texcoord;"
""
"void main() {"
"	gl_Position = vec4(proj * vec3(pos, 1.0), 1.0);"
"	v_color = color;"
"	v_texcoord = texcoord;"
"}";

const GLchar quad_fragment_src[] =
"precision mediump float;"
"varying vec4 v_color;"
"varying vec2 v_texcoord;"
""
"void main() {"
"  gl_FragColor = v_color;"
"}";

// Colored ellipses
const GLchar ellipse_fragment_src[] =
"precision mediump float;"
"varying vec4 v_color;"
"varying vec2 v_texcoord;"
""
"void main() {"
"  float l = length(v_texcoord - vec2(0.5, 0.5));"
"  if (l > 0.5) discard;"
"  gl_FragColor = v_color;"
"}";

// Textured quads
const GLchar vertex_src[] =
"uniform mat3 proj;"
"uniform bool invert_y;"
"attribute vec2 pos;"
"attribute vec2 texcoord;"
"varying vec2 v_texcoord;"
""
"void main() {"
"  gl_Position = vec4(proj * vec3(pos, 1.0), 1.0);"
"  if (invert_y) {"
"    v_texcoord = vec2(texcoord.s, 1.0 - texcoord.t);"
"  } else {"
"    v_texcoord = texcoord;"
"  }"
"}";

const GLchar fragment_src_rgba[] =
"precision mediump float;"
"varying vec2 v_texcoord;"
"uniform sampler2D tex;"
"uniform float alpha;"
""
"void main() {"
"	gl_FragColor = alpha * texture2D(tex, v_texcoord);"
"}";

const GLchar fragment_src_rgbx[] =
"precision mediump float;"
"varying vec2 v_texcoord;"
"uniform sampler2D tex;"
"uniform float alpha;"
""
"void main() {"
"	gl_FragColor.rgb = alpha * texture2D(tex, v_texcoord).rgb;"
"	gl_FragColor.a = alpha;"
"}";

const GLchar fragment_src_external[] =
"#extension GL_OES_EGL_image_external : require\n"
"precision mediump float;"
"varying vec2 v_texcoord;"
"uniform samplerExternalOES texture0;"
""
"void main() {"
"	vec4 col = texture2D(texture0, v_texcoord);"
"	gl_FragColor = vec4(col.rgb, col.a);"
"}";
