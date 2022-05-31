#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>

#define MAX_SYMBOLS 64
#define MEMINFO_PATH "/proc/meminfo"
#define BATTERY_CAPACITY_PATH "/sys/class/power_supply/BAT0/capacity"
#define BATTERY_STATUS_PATH "/sys/class/power_supply/BAT0/status"
#define MILLIS_TO_MICROS(M) M * 1000 > 1000000 ? 1000000 : M * 1000 // usleep range check

static char* ram_status(void);
static char* battery_status(void);
static char* layout_status(void);
static char* date_status(void);

char* (*status_providers[])(void) = {
	ram_status,
	battery_status,
	layout_status,
	date_status
};

Display* display;
Window window;
XkbDescRec* keyboard;

struct {
	int fd;               // file descriptor for /proc/meminfo
	char* mem_total;      // null terminated string with numbers only from first line in /proc/meminfo
	char* mem_total_fmt;  // formatted 'mem_total' - '7.67 GiB', '526 MiB' etc.
	char* buffer;         // allocated once in setup(), used for subsequent reads
	unsigned char size;   // length of single line in /proc/meminfo, used for allocating 'buffer'
} ram;

struct {
	int capacity_fd;
	int status_fd;
	char buffer[10];
} battery;

struct {
	unsigned char changed; // set by dwm
	char* buffer;
	unsigned char size;
} layout;

void signal_handler(int, siginfo_t* info, void*) {
	layout.changed = info -> si_value.sival_int;
}

// UTILS BEGIN

// remove 'target' string from 'str' string
// doesn't work with string literals passed to 'str', fails with segfault due to modification of .rodata segment
char* str_cut(char* str, char* target) {
	int str_length = strlen(str);
	int target_length = strlen(target);

	if (target_length > str_length) {
		return str;
	}

	for (int a = 0; a < str_length; a++) {
		if (str[a] == *target) {
			for (int j = 1; j < target_length; j++) {
				if (str[a + j] != target[j]) {
					a += j - 1; // skip checked bytes
					goto not_found;
				}
			}

			// dest - index of first 'target' character
			// src  - index of first character after 'target'
			// n    - length of string after 'target', plus terminating byte
			memcpy(str + a, str + a + target_length, str_length - target_length - a + 1);
			return str;

			not_found:
		}
	}

	return str;
}

// remove whitespaces at the begining and at the end of 'str'
// doesn't work with string literals, fails with segfault due to modification of .rodata segment
char* str_trim(char* str) {
	int str_length = strlen(str);

	if (*str != ' ' && str[str_length - 1] != ' ') {
		return str;
	}

	for (int a = 0; a < str_length; a++) {
		if (str[a] != ' ') {
			memcpy(str, str + a, str_length - a + 1);
			str_length -= a;
			break;
		}
	}

	for (int a = str_length - 1; a >= 0; a--) {
		if (str[a] != ' ') {
			str[a + 1] = 0;
			break;
		}
	}

	return str;
}

ssize_t read_all(int fd, void* ptr, size_t amount) {
	ssize_t total = 0, b;

	while ((b = read(fd, ptr + total, amount - total))) {
		if (b == -1) {
			return b;
		} else if ((total += b) == amount) {
			return total;
		}
	}

	return total;
}

void format_kb(int kb, char* buffer, int size) {
	if (kb >= 1024 * 1024) {
		snprintf(buffer, size, "%.2f GiB", kb / (float) (1024 * 1024));
	} else if (kb >= 1024) {
		snprintf(buffer, size, "%d MiB", kb / 1024);
	} else {
		snprintf(buffer, size, "%d KiB", kb);
	}
}

// UTILS END

// HANDLERS START

char* ram_status(void) {
	int mem_free, buffers, cached, sreclaimable;
	char mem_used[ram.size];

	fsync(ram.fd);
	lseek(ram.fd, ram.size, SEEK_SET); // MemFree

	if (read_all(ram.fd, ram.buffer, ram.size) == -1) {
		perror("ram_status mem_free read error");
		return "";
	}

	ram.buffer[ram.size - 1] = 0; // replace \n
	str_cut(ram.buffer, "MemFree:");
	str_cut(ram.buffer, "kB");
	str_trim(ram.buffer);
	mem_free = atoi(ram.buffer);

	lseek(ram.fd, ram.size * 3, SEEK_SET); // Buffers

	if (read_all(ram.fd, ram.buffer, ram.size) == -1) {
		perror("ram_status buffers read error");
		return "";
	}

	ram.buffer[ram.size - 1] = 0; // replace \n
	str_cut(ram.buffer, "Buffers:");
	str_cut(ram.buffer, "kB");
	str_trim(ram.buffer);
	buffers = atoi(ram.buffer);

	if (read_all(ram.fd, ram.buffer, ram.size) == -1) {
		perror("ram_status cached read error");
		return "";
	}

	ram.buffer[ram.size - 1] = 0; // replace \n
	str_cut(ram.buffer, "Cached:");
	str_cut(ram.buffer, "kB");
	str_trim(ram.buffer);
	cached = atoi(ram.buffer);

	lseek(ram.fd, ram.size * 23, SEEK_SET); // SReclaimable

	if (read_all(ram.fd, ram.buffer, ram.size) == -1) {
		perror("ram_status sreclaimable read error");
		return "";
	}

	ram.buffer[ram.size - 1] = 0; // replace \n
	str_cut(ram.buffer, "SReclaimable:");
	str_cut(ram.buffer, "kB");
	str_trim(ram.buffer);
	sreclaimable = atoi(ram.buffer);

	format_kb(
			(atoi(ram.mem_total) - mem_free) - (buffers + cached + sreclaimable), // used memory
			mem_used, 
			ram.size
	);
	snprintf(ram.buffer, ram.size, "%s/%s", mem_used, ram.mem_total_fmt);

	return ram.buffer; 
}

char* battery_status(void) {
	if (battery.capacity_fd == -1) {
		return "";
	}

	int charging, b;
	char capacity[5] = { 0 };

	memset(battery.buffer, 0, sizeof(battery.buffer));

	if (read_all(battery.status_fd, battery.buffer, sizeof(battery.buffer)) == -1) {
		perror("battery_status read "BATTERY_STATUS_PATH" error");
		return "";
	}

	charging = !strcmp(battery.buffer, "Charging\n");

	if ((b = read_all(battery.capacity_fd, capacity, sizeof(capacity))) == -1) {
		perror("battery_status read "BATTERY_CAPACITY_PATH" error");
		return "";
	}

	capacity[b - 1] = 0; // replace \n
	snprintf(battery.buffer, sizeof(battery.buffer), "%c%s %%", charging ? '+' : ' ', capacity);

	lseek(battery.status_fd, 0, SEEK_SET);
	lseek(battery.capacity_fd, 0, SEEK_SET);

	return battery.buffer;
}

char* layout_status(void) {
	char* symbols;

	if (layout.changed) {
		layout.changed = 0;

		XkbGetNames(display, XkbSymbolsNameMask, keyboard);
		symbols = XGetAtomName(display, keyboard -> names -> symbols);
		strcpy(layout.buffer, symbols);
		XFree(symbols);
		XkbFreeNames(keyboard, XkbSymbolsNameMask, False);

		strcpy(layout.buffer, strchr(layout.buffer, '+') + 1);
		layout.buffer[2] = 0; // xkb symbols contains only 2 chars - us, cz, de etc.
	}

	return layout.buffer;
}

char* date_status(void) {
	static char buffer[28];

	strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S %a %b", localtime(&(time_t) { time(NULL) }));

	return buffer;
}

// HANDLERS END

// SETUP START

void ram_setup(void) {
	int read_size = 32, amount;
	char* tmp;

	if ((ram.fd = open(MEMINFO_PATH, O_RDONLY)) == -1) {
		perror("setup_ram open "MEMINFO_PATH" error");
		exit(-1);
	}

	if (!(tmp = malloc(read_size))) {
		puts("setup_ram tmp malloc error");
		exit(-1);
	}

	while (1) {
		if ((amount = read_all(ram.fd, tmp, read_size)) == -1) {
			perror("setup_ram tmp read error");
			exit(-1);
		}

		for (int a = 0; a < amount; a++) {
			if (tmp[a] == '\n') { // calculate line length to avoid reading of entire file
				ram.size = a + 1;
				free(tmp);
				goto mem_total_fmt;
			}
		}

		lseek(ram.fd, 0, SEEK_SET);
		read_size *= 2;
		if (!(tmp = realloc(tmp, read_size))) {
			puts("setup_ram tmp realloc error");
			exit(-1);
		}
	}

	mem_total_fmt:
	if (!(ram.buffer = malloc(ram.size))) {
		puts("setup_ram ram.buffer malloc error");
		exit(-1);
	}

	lseek(ram.fd, 0, SEEK_SET);

	if (read_all(ram.fd, ram.buffer, ram.size) == -1) {
		perror("setup_ram ram.mem_total read error");
		exit(-1);
	}

	ram.buffer[ram.size - 1] = 0; // replace \n
	str_cut(ram.buffer, "MemTotal:");
	str_cut(ram.buffer, "kB");
	str_trim(ram.buffer);

	if (!(ram.mem_total = malloc(strlen(ram.buffer) + 1))) {
		puts("setup_ram ram.mem_total malloc error");
		exit(-1);
	}

	strcpy(ram.mem_total, ram.buffer);
	format_kb(atoi(ram.mem_total), ram.buffer, ram.size);

	if (!(ram.mem_total_fmt = malloc(strlen(ram.buffer) + 1))) {
		puts("setup_ram ram.mem_total_fmt malloc error");
		exit(-1);
	}

	strcpy(ram.mem_total_fmt, ram.buffer);
}

void battery_setup(void) {
	battery.capacity_fd = open(BATTERY_CAPACITY_PATH, O_RDONLY);

	if (battery.capacity_fd == -1 && errno != ENOENT) {
		perror("setup_battery_status open "BATTERY_CAPACITY_PATH" error");
		exit(-1);
	}

	battery.status_fd = open(BATTERY_STATUS_PATH, O_RDONLY);

	if (battery.status_fd == -1 && errno != ENOENT) {
		perror("setup_battery_status open "BATTERY_STATUS_PATH" error");
		exit(-1);
	}
}

void layout_setup(void) {
	char* symbols;

	XkbIgnoreExtension(False);
	XkbQueryExtension(
			display, 
			NULL, 
			NULL, 
			NULL, 
			&(int) { XkbMajorVersion }, 
			&(int) { XkbMinorVersion }
	);

	if (!(keyboard = XkbAllocKeyboard())) {
		puts("setup_keyboard_layout XkbAllocKeyboard error");
		exit(-1);
	}

	XkbGetNames(display, XkbSymbolsNameMask, keyboard);
	symbols = XGetAtomName(display, keyboard -> names -> symbols);
	layout.size = strlen(symbols) + 1;

	if (!(layout.buffer = malloc(layout.size))) {
		puts("setup_keyboard_layout layout.buffer malloc error");
		exit(-1);
	}

	strcpy(layout.buffer, symbols);
	XFree(symbols);
	XkbFreeNames(keyboard, XkbSymbolsNameMask, False);

	strcpy(layout.buffer, strchr(layout.buffer, '+') + 1);
	*strchr(layout.buffer, '+') = 0;
}


void setup(void) {
	struct sigaction sig = {
		.sa_sigaction = signal_handler,
		.sa_flags = SA_SIGINFO
	};

	if (!(display = XOpenDisplay(0))) {
		puts("setup XOpenDisplay error");
		exit(-1);
	}

	if (!(window = RootWindow(display, DefaultScreen(display)))) {
		puts("setup RootWindow error");
		exit(-1);
	}

	if (sigaction(SIGUSR1, &sig, NULL) == -1) {
		perror("setup sigaction error");
		exit(-1);
	}

	ram_setup();
	battery_setup();
	layout_setup();
}

// SETUP END

void run(void) {
	int status_text_size = MAX_SYMBOLS + 4; // plus 4 bsc of appending ' | ' in snprintf(3) and 0 byte at the end
	int current_size = 0, str_length;
	char* status_text = malloc(status_text_size); 
	char* str;
	XTextProperty xtp = {
		.encoding = XA_STRING,
		.format = 8,
		.value = (unsigned char*) status_text // suppress compiler warning
	};

	if (!status_text) {
		puts("run malloc error");
		exit(-1);
	}

	while (1) {
		for (int a = 0; a < sizeof(status_providers) / sizeof(*status_providers); a++) {
			str = status_providers[a]();

			str_trim(str);

			if (!(str_length = strlen(str))) {
				continue;
			}

			snprintf(status_text + current_size, status_text_size - current_size, "%s | ", str);
			current_size += str_length + 3;

			if (current_size - 3 >= MAX_SYMBOLS) {
				break;
			}
		}

		current_size -= 3;
		xtp.nitems = current_size > MAX_SYMBOLS ? MAX_SYMBOLS : current_size;
		current_size = 0;

		XSetTextProperty(display, window, &xtp, XA_WM_NAME);
		XFlush(display);

		usleep(MILLIS_TO_MICROS(50));
	}
}

void check_dwm_pid_link(void) {
	int pid, comm_fd;
	char buffer[22];
	DIR* dir = opendir("/proc");
	struct dirent* ent;

	if (!dir) {
		perror("check_dwm_pid_link opendir error");
		exit(-1);
	}

	errno = 0;

	while ((ent = readdir(dir))) {
		if ((pid = atoi(ent -> d_name))) {
			snprintf(buffer, sizeof(buffer), "/proc/%d/comm", pid);

			if ((comm_fd = open(buffer, O_RDONLY)) == -1) {
				perror("check_dwm_pid_link open error");
				exit(-1);
			}

			if (read_all(comm_fd, buffer, 3) == -1) {
				perror("check_dwm_pid_link read error");
				exit(-1);
			}

			close(comm_fd);
			buffer[3] = 0;

			if (!strcmp(buffer, "dwm")) {
				break;
			}

			pid = 0;
		}
	}

	if (errno) {
		perror("check_dwm_pid_link readdir error");
		exit(-1);
	} else if (!pid) {
		puts("check_dwm_pid_link dwm pid not found");
		exit(-1);
	}

	closedir(dir);
	sigqueue(pid, SIGUSR1, (union sigval) { .sival_int = getpid() });
}

int main() {
	check_dwm_pid_link();
	setup();
	run();
	XCloseDisplay(display);
}

