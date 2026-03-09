#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

int main(int argc, char *argv[], char *envp[]);
void __libc_init_array(void);
extern char **environ;

void call_main(uintptr_t *args) {
  char *empty[] = { NULL };
  int argc = 0;
  char **argv = empty;
  char **envp = empty;

  if (args != NULL) {
    argc = (int)args[0];
    argv = (char **)(args + 1);
    envp = argv + argc + 1;
  }

  environ = envp;
  __libc_init_array();
  exit(main(argc, argv, envp));
  assert(0);
}
