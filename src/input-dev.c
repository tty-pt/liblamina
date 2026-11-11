#include "input.h"

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#ifndef EV_BUFSZ
#define EV_BUFSZ 256
#endif

lam_input_t lam_input_dev;

typedef struct {
	int fd;
	dev_t rdev;
	char path[PATH_MAX];
} idev_t;

static int epfd = -1, inofd = -1, grab_all = 0;
static idev_t devs[128];
static int ndev = 0;
static struct termios orig_termios;

#include <GLFW/glfw3.h>
#include <linux/input-event-codes.h>

static int keymap[KEY_MAX + 1];

static const struct { int key; int glfw; } map_pairs[] = {
	{ KEY_A, GLFW_KEY_A }, { KEY_B, GLFW_KEY_B }, { KEY_C, GLFW_KEY_C },
	{ KEY_D, GLFW_KEY_D }, { KEY_E, GLFW_KEY_E }, { KEY_F, GLFW_KEY_F },
	{ KEY_G, GLFW_KEY_G }, { KEY_H, GLFW_KEY_H }, { KEY_I, GLFW_KEY_I },
	{ KEY_J, GLFW_KEY_J }, { KEY_K, GLFW_KEY_K }, { KEY_L, GLFW_KEY_L },
	{ KEY_M, GLFW_KEY_M }, { KEY_N, GLFW_KEY_N }, { KEY_O, GLFW_KEY_O },
	{ KEY_P, GLFW_KEY_P }, { KEY_Q, GLFW_KEY_Q }, { KEY_R, GLFW_KEY_R },
	{ KEY_S, GLFW_KEY_S }, { KEY_T, GLFW_KEY_T }, { KEY_U, GLFW_KEY_U },
	{ KEY_V, GLFW_KEY_V }, { KEY_W, GLFW_KEY_W }, { KEY_X, GLFW_KEY_X },
	{ KEY_Y, GLFW_KEY_Y }, { KEY_Z, GLFW_KEY_Z },
	{ KEY_UP, GLFW_KEY_UP }, { KEY_DOWN, GLFW_KEY_DOWN },
	{ KEY_LEFT, GLFW_KEY_LEFT }, { KEY_RIGHT, GLFW_KEY_RIGHT },
	{ KEY_SPACE, GLFW_KEY_SPACE }, { KEY_TAB, GLFW_KEY_TAB },
	{ KEY_ENTER, GLFW_KEY_ENTER }, { KEY_ESC, GLFW_KEY_ESCAPE },
};

__attribute__((constructor))
static void init_keymap(void)
{
	for (int i = 0; i <= KEY_MAX; i++)
		keymap[i] = -1;

	for (size_t i = 0; i < sizeof(map_pairs)/sizeof(map_pairs[0]); i++)
		keymap[map_pairs[i].key] = map_pairs[i].glfw;
}


static int input_index(dev_t r) {
	for (int i = 0; i < ndev; i++)
		if (devs[i].rdev == r)
			return i;
	return -1;
}

static int input_open(const char *path) {
	struct stat st;

	if (stat(path, &st) < 0)
		return -1;

	if (!S_ISCHR(st.st_mode))
		return -1;

	if (input_index(st.st_rdev) >= 0)
		return 0;

	int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return -1;

	if (grab_all)
		ioctl(fd, EVIOCGRAB, 1);

	struct epoll_event ev = {
		.events = EPOLLIN | EPOLLET,
		.data.fd = fd,
	};

	if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0)
		goto fail;

	idev_t *d = &devs[ndev++];
	d->fd = fd;
	d->rdev = st.st_rdev;
	strncpy(d->path, path, sizeof(d->path) - 1);

	d->path[sizeof(d->path) - 1] = 0;
	return 0;

fail:
	close(fd);
	return -1;
}

static void input_iscan(void) {
	DIR *dir = opendir("/dev/input");
	struct dirent *e;

	if (!dir)
		return;

	while ((e = readdir(dir))) {
		char p[PATH_MAX];

		if (strncmp(e->d_name, "event", 5) != 0)
			continue;

		snprintf(p, sizeof(p), "/dev/input/%s",
				e->d_name);

		input_open(p);
	}

	closedir(dir);
}


void tty_raw(void) {

	struct termios t;

	if (tcgetattr(STDIN_FILENO, &orig_termios) < 0) {
		perror("tcgetattr");
		return;
	}

	t = orig_termios;
	t.c_lflag &= ~(ICANON | ECHO);
	t.c_cc[VMIN]  = 1;
	t.c_cc[VTIME] = 0;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) < 0) {
		perror("tcsetattr");
	}
}

static void tty_restore(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static void input_init(int grab) {
	grab_all = grab ? 1 : 0;
	epfd = epoll_create1(EPOLL_CLOEXEC);
	inofd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	inotify_add_watch(inofd, "/dev/input",
			IN_CREATE | IN_DELETE);

	struct epoll_event ev = {
		.events = EPOLLIN,
		.data.fd = inofd,
	};

	epoll_ctl(epfd, EPOLL_CTL_ADD, inofd, &ev);
	input_iscan();
	tty_raw();
}

static void input_close(int idx) {
	if (idx < 0 || idx >= ndev)
		return;

	epoll_ctl(epfd, EPOLL_CTL_DEL, devs[idx].fd, NULL);
	close(devs[idx].fd);
	devs[idx] = devs[--ndev];
}

static inline void
input_hot_drain_x(struct inotify_event *ie) {
	char path[PATH_MAX];

	snprintf(path, sizeof(path),
			"/dev/input/%s", ie->name);

	if (ie->mask & IN_CREATE)
		input_open(path);

	if (!(ie->mask & IN_DELETE))
		return;

	for (int i=0; i<ndev; i++)
		if (!strcmp(devs[i].path, path)) {
			input_close(i);
			break;
		}
}

// hot plug
static int
input_hot_drain(void)
{
	char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	ssize_t len = read(inofd, buf, sizeof(buf));
	struct inotify_event *ie;

	if (len < 0)
		return 0;

	for (char *p = buf; p < buf + len; p += sizeof(*ie) + ie->len)
	{
		ie = (struct inotify_event *) p;

		if (ie->len && strncmp(ie->name, "event", 5) == 0)
			input_hot_drain_x(ie);
	}

	return 1;
}

static int input_drain(int fd) {
	struct input_event ev[EV_BUFSZ];
	ssize_t r = read(fd, ev, sizeof(ev));

	if (r < 0)
		return 0;

	// dead device
	if (r == 0) {
		for (int i=0; i<ndev; i++)
			if (devs[i].fd == fd) {
				input_close(i);
				break;
			}
		return 0;
	}

	int n = r / (int) sizeof(struct input_event);

	for (int i = 0; i < n; i++) {
		struct input_event *e = &ev[i];

		if (e->type == EV_KEY) {
			int gkey = (e->code <= KEY_MAX) ? keymap[e->code] : -1;
			if (gkey >= 0)
				input_call((unsigned short)gkey,
						e->value ? 1 : 0,
						e->type);
		} else if (e->type == EV_REL || e->type == EV_ABS) {
			input_call(0, 0, e->type); /* sinalizar movimento ou resize */
		}
	}

	return 1;
}

static void input_poll(void) {
	struct epoll_event evs[64];
	int n = epoll_wait(epfd, evs, 64, 0);

	if (n < 0)
		return;

	for (int i = 0; i < n; i++) {
		int fd = evs[i].data.fd;
		if (fd == inofd)
			while (input_hot_drain());
		else
			while (input_drain(fd));
	}
}

static void input_deinit(void) {
	for (int i = 0; i < ndev; i++)
		close(devs[i].fd);

	ndev = 0;

	if (inofd >= 0)
		close(inofd);

	if (epfd >= 0)
		close(epfd);

	inofd = epfd = -1;
	tty_restore();
}

void input_dev_construct(void)
{
	lam_input_dev.init = input_init;
	lam_input_dev.deinit = input_deinit;
	lam_input_dev.poll = input_poll;
}
