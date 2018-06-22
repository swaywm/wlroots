#include <unistd.h>
#include <wlr/render/dmabuf.h>

void wlr_dmabuf_attributes_finish( struct wlr_dmabuf_attributes *attribs) {
	for (int i = 0; i < attribs->n_planes; ++i) {
		close(attribs->fd[i]);
		attribs->fd[i] = -1;
	}
	attribs->n_planes = 0;
}
