#include <nterm.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <SDL.h>

char handle_key(SDL_Event *ev);

static void sh_printf(const char *format, ...) {
  static char buf[256] = {};
  va_list ap;
  va_start(ap, format);
  int len = vsnprintf(buf, 256, format, ap);
  va_end(ap);
  term->write(buf, len);
}

static void sh_banner() {
  sh_printf("Built-in Shell in NTerm (NJU Terminal)\n\n");
}

static void sh_prompt() {
  sh_printf("sh> ");
}

static void sh_help() {
  sh_printf("Commands:\n");
  sh_printf("  help               show this help\n");
  sh_printf("  echo [args...]     print arguments\n");
  sh_printf("  clear              clear the screen\n");
  sh_printf("  pwd                print current path\n");
  sh_printf("  ls                 show common files/apps\n");
  sh_printf("  cat <file>         print file content\n");
  sh_printf("  hexdump <f> [n]    dump first n bytes (default 128)\n");
  sh_printf("  uptime             print milliseconds since start\n");
  sh_printf("  sleep <ms>         busy wait for ms milliseconds\n");
  sh_printf("  exit [code]        exit this program\n");
}

static int sh_parse(char *line, char *argv[], int max_args) {
  int argc = 0;
  char *token = strtok(line, " \t\r\n");
  while (token != NULL && argc < max_args) {
    argv[argc ++] = token;
    token = strtok(NULL, " \t\r\n");
  }
  return argc;
}

static void sh_cat(const char *path) {
  int fd = open(path, O_RDONLY, 0);
  if (fd < 0) {
    sh_printf("cat: can not open %s\n", path);
    return;
  }

  char buf[256];
  int nread;
  while ((nread = read(fd, buf, sizeof(buf))) > 0) {
    term->write(buf, nread);
  }
  close(fd);
  if (nread < 0) {
    sh_printf("cat: read error on %s\n", path);
  }
}

static void sh_hexdump(const char *path, int limit) {
  int fd = open(path, O_RDONLY, 0);
  if (fd < 0) {
    sh_printf("hexdump: can not open %s\n", path);
    return;
  }

  if (limit <= 0) limit = 128;

  unsigned char buf[16];
  int total = 0;
  while (total < limit) {
    int want = (limit - total > (int)sizeof(buf) ? (int)sizeof(buf) : limit - total);
    int nread = read(fd, buf, want);
    if (nread <= 0) break;

    sh_printf("%08x: ", total);
    for (int i = 0; i < 16; i ++) {
      if (i < nread) sh_printf("%02x ", buf[i]);
      else sh_printf("   ");
    }
    sh_printf(" |");
    for (int i = 0; i < nread; i ++) {
      char ch = (buf[i] >= 32 && buf[i] <= 126 ? buf[i] : '.');
      term->write(&ch, 1);
    }
    sh_printf("|\n");
    total += nread;
  }

  close(fd);
}

static void sh_ls() {
  static const char *entries[] = {
    "/bin/menu",
    "/bin/nterm",
    "/bin/nslider",
    "/bin/hello",
    "/bin/timer-test",
    "/bin/event-test",
    "/bin/bmp-test",
    "/share/pictures/projectn.bmp",
    "/share/fonts/Courier-7.bdf",
    "/share/slides/slides-0.bmp",
    "/dev/events",
    "/proc/dispinfo",
    "/dev/fb",
  };
  sh_printf("builtin file list:\n");
  for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i ++) {
    sh_printf("  %s\n", entries[i]);
  }
}

static bool sh_setenv(char *arg) {
  char *eq = strchr(arg, '=');
  if (eq == NULL || eq == arg) return false;

  *eq = 0;
  int ret = setenv(arg, eq + 1, 1);
  *eq = '=';
  if (ret != 0) {
    sh_printf("setenv failed: %s", arg);
    char nl = 10;
    term->write(&nl, 1);
  }
  return true;
}

static void sh_exec(int argc, char *argv[]) {
  int cmd_idx = 0;
  while (cmd_idx < argc && sh_setenv(argv[cmd_idx])) {
    cmd_idx ++;
  }

  if (cmd_idx >= argc) return;

  SDL_CloseAudio();
  execvp(argv[cmd_idx], &argv[cmd_idx]);
  sh_printf("execvp failed: %s (errno=%d)", argv[cmd_idx], errno);
  char nl = 10;
  term->write(&nl, 1);
}

static void sh_sleep(int ms) {
  if (ms < 0) ms = 0;
  uint32_t start = SDL_GetTicks();
  while ((int32_t)(SDL_GetTicks() - start) < ms) {
    refresh_terminal();
  }
}

static void sh_handle_cmd(const char *cmd) {
  char line[256];
  char *argv[16];

  strncpy(line, cmd, sizeof(line) - 1);
  line[sizeof(line) - 1] = '\0';

  int argc = sh_parse(line, argv, 16);
  if (argc == 0) return;

  if (strcmp(argv[0], "help") == 0) {
    sh_help();
  }
  else if (strcmp(argv[0], "echo") == 0) {
    for (int i = 1; i < argc; i ++) {
      if (i != 1) sh_printf(" ");
      sh_printf("%s", argv[i]);
    }
    sh_printf("\n");
  }
  else if (strcmp(argv[0], "clear") == 0 || strcmp(argv[0], "cls") == 0) {
    static const char seq[] = "\033[2J\033[H";
    term->write(seq, sizeof(seq) - 1);
  }
  else if (strcmp(argv[0], "pwd") == 0) {
    sh_printf("/\n");
  }
  else if (strcmp(argv[0], "ls") == 0) {
    sh_ls();
  }
  else if (strcmp(argv[0], "cat") == 0) {
    if (argc < 2) sh_printf("usage: cat <file>\n");
    else {
      sh_cat(argv[1]);
      sh_printf("\n");
    }
  }
  else if (strcmp(argv[0], "hexdump") == 0) {
    if (argc < 2) sh_printf("usage: hexdump <file> [n]\n");
    else sh_hexdump(argv[1], argc >= 3 ? atoi(argv[2]) : 128);
  }
  else if (strcmp(argv[0], "uptime") == 0) {
    sh_printf("%u ms\n", SDL_GetTicks());
  }
  else if (strcmp(argv[0], "sleep") == 0) {
    if (argc < 2) sh_printf("usage: sleep <ms>\n");
    else sh_sleep(atoi(argv[1]));
  }
  else if (strcmp(argv[0], "exit") == 0) {
    int code = (argc >= 2 ? atoi(argv[1]) : 0);
    _exit(code);
  }
  else {
    sh_exec(argc, argv);
  }
}

void builtin_sh_run() {
  if (getenv("PATH") == NULL) setenv("PATH", "/bin", 0);
  sh_banner();
  sh_prompt();

  while (1) {
    SDL_Event ev;
    if (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_KEYUP || ev.type == SDL_KEYDOWN) {
        const char *res = term->keypress(handle_key(&ev));
        if (res) {
          sh_handle_cmd(res);
          sh_prompt();
        }
      }
    }
    refresh_terminal();
  }
}
