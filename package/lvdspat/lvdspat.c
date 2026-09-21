// Paint a known pattern on one display output, through KMS.
//
// The tester reads pixels back out of the FPGA by coordinate, so what it needs
// on screen is a picture whose every pixel it can predict. fbdev cannot give
// it: DRM's fbdev emulation hands the same framebuffer to every CRTC, which is
// why the 800x480 panel shows a corner of a 1920x1080 buffer at a stride of
// 7680 while HDMI shows the whole thing. KMS has no such limit - each CRTC
// takes its own framebuffer - so this paints one output and leaves the others
// alone.
//
// The mode is left in place only while this process lives: DRM tears the
// framebuffer down when the last handle closes. So it paints, says so, and
// waits to be killed while the measuring happens.

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

static volatile sig_atomic_t stop;
static void on_signal(int s) { (void)s; stop = 1; }

struct fb {
	uint32_t handle, fb_id, pitch;
	uint64_t size;
	uint8_t *map;
};

static int fb_create(int fd, uint32_t w, uint32_t h, struct fb *out)
{
	struct drm_mode_create_dumb creq;
	struct drm_mode_map_dumb mreq;

	memset(&creq, 0, sizeof(creq));
	creq.width = w;
	creq.height = h;
	creq.bpp = 32;
	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
		fprintf(stderr, "create dumb %ux%u: %s\n", w, h, strerror(errno));
		return -1;
	}
	out->handle = creq.handle;
	out->pitch  = creq.pitch;
	out->size   = creq.size;

	if (drmModeAddFB(fd, w, h, 24, 32, creq.pitch, creq.handle,
			 &out->fb_id) < 0) {
		fprintf(stderr, "add fb: %s\n", strerror(errno));
		return -1;
	}

	memset(&mreq, 0, sizeof(mreq));
	mreq.handle = creq.handle;
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
		fprintf(stderr, "map dumb: %s\n", strerror(errno));
		return -1;
	}
	out->map = mmap(0, out->size, PROT_READ | PROT_WRITE, MAP_SHARED,
			fd, mreq.offset);
	if (out->map == MAP_FAILED) {
		fprintf(stderr, "mmap: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

// The patterns. Each says exactly what a given x,y should be, because that is
// the point of the exercise - the FPGA is asked for one pixel and the answer
// has to be checkable.
enum pattern { PAT_SOLID, PAT_COL, PAT_ROW, PAT_RAMPX, PAT_RAMPY, PAT_GRID };

static uint32_t pixel_of(enum pattern p, uint32_t x, uint32_t y,
			 uint32_t mx, uint32_t my, uint32_t rgb)
{
	switch (p) {
	case PAT_SOLID: return rgb;
	case PAT_COL:   return (x == mx) ? rgb : 0;
	case PAT_ROW:   return (y == my) ? rgb : 0;
	// A ramp gives every column a different value, so one sweep says
	// whether the whole line maps straight through, not just its ends.
	case PAT_RAMPX: return ((x & 0xff) << 16) | ((x & 0xff) << 8) | (x & 0xff);
	case PAT_RAMPY: return ((y & 0xff) << 16) | ((y & 0xff) << 8) | (y & 0xff);
	// Corners and centre lines at once, for a quick look by eye.
	case PAT_GRID:
		if (x == 0 || y == 0 || x == mx || y == my) return rgb;
		if (x == mx / 2 || y == my / 2) return rgb;
		return 0;
	}
	return 0;
}

static void paint(struct fb *f, uint32_t w, uint32_t h, enum pattern p,
		  uint32_t mx, uint32_t my, uint32_t rgb)
{
	uint32_t x, y;
	for (y = 0; y < h; y++) {
		uint32_t *row = (uint32_t *)(f->map + (uint64_t)y * f->pitch);
		for (x = 0; x < w; x++)
			row[x] = pixel_of(p, x, y, mx, my, rgb);
	}
}

static void usage(const char *me)
{
	fprintf(stderr,
	 "usage: %s [-D dev] [-c connector] [-p pattern] [-x N] [-y N] [-r RRGGBB]\n"
	 "  -D  dri device, default /dev/dri/card0\n"
	 "  -c  connector name or substring, default DPI (the lvds panel)\n"
	 "  -p  solid | col | row | rampx | rampy | grid   default col\n"
	 "  -x  column for col and grid, default last one\n"
	 "  -y  row for row, default last one\n"
	 "  -r  colour as hex RRGGBB, default ffffff\n"
	 "Paints and then waits: the mode only lives as long as this process.\n",
	 me);
}

int main(int argc, char **argv)
{
	const char *dev = "/dev/dri/card0", *want = "DPI", *pname = "col";
	long argx = -1, argy = -1;
	uint32_t rgb = 0xffffff;
	enum pattern pat = PAT_COL;
	int fd, i, opt;
	drmModeRes *res;
	drmModeConnector *conn = NULL;
	drmModeEncoder *enc = NULL;
	drmModeModeInfo mode;
	struct fb f;
	uint32_t crtc_id = 0;

	while ((opt = getopt(argc, argv, "D:c:p:x:y:r:h")) != -1) {
		switch (opt) {
		case 'D': dev = optarg; break;
		case 'c': want = optarg; break;
		case 'p': pname = optarg; break;
		case 'x': argx = strtol(optarg, NULL, 0); break;
		case 'y': argy = strtol(optarg, NULL, 0); break;
		case 'r': rgb = (uint32_t)strtoul(optarg, NULL, 16); break;
		default:  usage(argv[0]); return 2;
		}
	}

	if      (!strcmp(pname, "solid")) pat = PAT_SOLID;
	else if (!strcmp(pname, "col"))   pat = PAT_COL;
	else if (!strcmp(pname, "row"))   pat = PAT_ROW;
	else if (!strcmp(pname, "rampx")) pat = PAT_RAMPX;
	else if (!strcmp(pname, "rampy")) pat = PAT_RAMPY;
	else if (!strcmp(pname, "grid"))  pat = PAT_GRID;
	else { usage(argv[0]); return 2; }

	fd = open(dev, O_RDWR | O_CLOEXEC);
	if (fd < 0) { fprintf(stderr, "%s: %s\n", dev, strerror(errno)); return 1; }

	// Without this the kernel refuses to let us set a mode, and says so only
	// as EACCES from the ioctl far below.
	if (drmSetMaster(fd) < 0)
		fprintf(stderr, "warning: not drm master (%s), setcrtc may fail\n",
			strerror(errno));

	res = drmModeGetResources(fd);
	if (!res) { fprintf(stderr, "get resources: %s\n", strerror(errno)); return 1; }

	for (i = 0; i < res->count_connectors; i++) {
		drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
		char name[64];
		if (!c) continue;
		snprintf(name, sizeof(name), "%s-%u",
			 drmModeGetConnectorTypeName(c->connector_type) ?: "?",
			 c->connector_type_id);
		if (c->connection == DRM_MODE_CONNECTED && c->count_modes &&
		    strstr(name, want)) {
			conn = c;
			printf("connector %s, %u modes\n", name, c->count_modes);
			break;
		}
		drmModeFreeConnector(c);
	}
	if (!conn) { fprintf(stderr, "no connected connector matching '%s'\n", want); return 1; }

	mode = conn->modes[0];
	printf("mode %ux%u@%u\n", mode.hdisplay, mode.vdisplay, mode.vrefresh);

	// Prefer the crtc already driving this connector, so a working display
	// is not stolen from another output that is using a different one.
	if (conn->encoder_id) {
		enc = drmModeGetEncoder(fd, conn->encoder_id);
		if (enc && enc->crtc_id) crtc_id = enc->crtc_id;
	}
	if (!crtc_id) {
		for (i = 0; i < conn->count_encoders && !crtc_id; i++) {
			drmModeEncoder *e = drmModeGetEncoder(fd, conn->encoders[i]);
			int j;
			if (!e) continue;
			for (j = 0; j < res->count_crtcs; j++)
				if (e->possible_crtcs & (1u << j)) {
					crtc_id = res->crtcs[j];
					break;
				}
			drmModeFreeEncoder(e);
		}
	}
	if (!crtc_id) { fprintf(stderr, "no usable crtc\n"); return 1; }
	printf("crtc %u\n", crtc_id);

	if (fb_create(fd, mode.hdisplay, mode.vdisplay, &f) < 0) return 1;

	{
		uint32_t mx = (argx >= 0) ? (uint32_t)argx : mode.hdisplay - 1;
		uint32_t my = (argy >= 0) ? (uint32_t)argy : mode.vdisplay - 1;
		paint(&f, mode.hdisplay, mode.vdisplay, pat, mx, my, rgb);
		printf("pattern %s", pname);
		if (pat == PAT_COL || pat == PAT_GRID) printf(" x=%u", mx);
		if (pat == PAT_ROW)                    printf(" y=%u", my);
		if (pat != PAT_RAMPX && pat != PAT_RAMPY) printf(" rgb=%06x", rgb);
		printf("\n");
	}

	if (drmModeSetCrtc(fd, crtc_id, f.fb_id, 0, 0,
			   &conn->connector_id, 1, &mode) < 0) {
		fprintf(stderr, "set crtc: %s\n", strerror(errno));
		return 1;
	}

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	printf("painted; holding (pid %d), signal to stop\n", (int)getpid());
	fflush(stdout);
	while (!stop) pause();
	printf("stopping\n");
	return 0;
}
