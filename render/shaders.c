#include <GLES2/gl2.h>

const GLchar poly_vert_src[] =
"uniform mat3 proj;\n"
"attribute vec3 pos;\n"
"void main() {\n"
"	vec2 xy = (proj * vec3(pos.xy, 1.0)).xy;\n"
"	gl_Position = vec4(xy, pos.z, 1.0);\n"
"}\n";

const GLchar poly_frag_src[] =
"precision mediump float;\n"
"uniform vec4 color;\n"
"void main() {\n"
"	gl_FragColor = color;\n"
"}\n";

const GLchar tex_vert_src[] =
"uniform mat3 proj;\n"
"attribute vec3 pos;\n"
"attribute vec2 texcoord;\n"
"varying vec2 v_texcoord;\n"
"void main() {\n"
"	vec2 xy = (proj * vec3(pos.xy, 1.0)).xy;\n"
"	gl_Position = vec4(xy, pos.z, 1.0);\n"
"	v_texcoord = texcoord;\n"
"}\n";

const GLchar tex_frag_src[] =
"#extension GL_OES_EGL_image_external : require\n"
"precision mediump float;\n"
"varying vec2 v_texcoord;\n"
"uniform samplerExternalOES texture0;\n"
"void main() {\n"
"	gl_FragColor = texture2D(texture0, v_texcoord);\n"
"}\n";
