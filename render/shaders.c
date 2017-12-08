#include <GLES2/gl2.h>

const GLchar poly_vert_src[] =
"uniform mat3 proj;\n"
"attribute vec2 pos;\n"
"void main() {\n"
"	vec3 new_pos = proj * vec3(pos, 1.0);\n"
"	gl_Position = vec4(new_pos.xy, 0.0, 1.0);\n"
"}\n";

const GLchar poly_frag_src[] =
"precision mediump float;\n"
"uniform vec4 color;\n"
"void main() {\n"
"	gl_FragColor = color;\n"
"}\n";

const GLchar tex_vert_src[] =
"uniform mat3 proj;\n"
"attribute vec2 pos;\n"
"attribute vec2 texcoord;\n"
"varying vec2 v_texcoord;\n"
"void main() {\n"
"	vec3 new_pos = proj * vec3(pos, 1.0);\n"
"	gl_Position = vec4(new_pos.xy, 0.0, 1.0);\n"
"	v_texcoord = texcoord;\n"
"}\n";

const GLchar tex_frag_src[] =
"precision mediump float;\n"
"varying vec2 v_texcoord;\n"
"uniform sampler2D tex;\n"
"void main() {\n"
"	gl_FragColor = texture2D(tex, v_texcoord);\n"
"}\n";

const GLchar ext_frag_src[] =
"#extension GL_OES_EGL_image_external : require\n"
"precision mediump float;\n"
"varying vec2 v_texcoord;\n"
"uniform samplerExternalOES tex;\n"
"void main() {\n"
"	gl_FragColor = texture2D(tex, v_texcoord);\n"
"}\n";
