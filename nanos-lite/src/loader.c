#include <proc.h>
#include <elf.h>
#include <fs.h>
#include <alloca.h>

#ifdef __LP64__
# define Elf_Ehdr Elf64_Ehdr
# define Elf_Phdr Elf64_Phdr
#else
# define Elf_Ehdr Elf32_Ehdr
# define Elf_Phdr Elf32_Phdr
#endif

#if defined(__ISA_AM_NATIVE__) || defined(__ISA_NATIVE__)
# define EXPECT_TYPE EM_X86_64
#elif defined(__ISA_X86__)
# define EXPECT_TYPE EM_386
#elif defined(__ISA_MIPS32__)
# define EXPECT_TYPE EM_MIPS
#elif defined(__ISA_RISCV32__) || defined(__ISA_RISCV64__)
# define EXPECT_TYPE EM_RISCV
#elif defined(__ISA_LOONGARCH32R__)
# ifdef EM_LOONGARCH
#  define EXPECT_TYPE EM_LOONGARCH
# else
#  error Unsupported ISA
# endif
#else
# error Unsupported ISA
#endif

static uintptr_t loader(PCB *pcb, const char *filename) {
  (void)pcb;

  if (filename == NULL) {
    filename = "/bin/dummy";
  }

  int fd = fs_open(filename, 0, 0);

  Elf_Ehdr ehdr;
  fs_read(fd, &ehdr, sizeof(ehdr));

  assert(*(uint32_t *)ehdr.e_ident == 0x464c457f);
  assert(ehdr.e_machine == EXPECT_TYPE);

  for (int i = 0; i < ehdr.e_phnum; i++) {
    Elf_Phdr phdr;
    fs_lseek(fd, ehdr.e_phoff + i * ehdr.e_phentsize, SEEK_SET);
    fs_read(fd, &phdr, sizeof(phdr));

    if (phdr.p_type != PT_LOAD) continue;

    fs_lseek(fd, phdr.p_offset, SEEK_SET);
    fs_read(fd, (void *)phdr.p_vaddr, phdr.p_filesz);
    memset((void *)(phdr.p_vaddr + phdr.p_filesz), 0, phdr.p_memsz - phdr.p_filesz);
  }

  fs_close(fd);
  return ehdr.e_entry;
}

static uintptr_t build_user_stack(char *const argv[], char *const envp[]) {
  const int stack_pages = STACK_SIZE / PGSIZE;
  uint8_t *stack = (uint8_t *)new_page(stack_pages);
  uintptr_t top = (uintptr_t)stack + STACK_SIZE;
  uintptr_t sp = top;

  int argc = 0;
  int envc = 0;
  if (argv != NULL) {
    while (argv[argc] != NULL) argc++;
  }
  if (envp != NULL) {
    while (envp[envc] != NULL) envc++;
  }

  char **argv_store = argc > 0 ? (char **)alloca(sizeof(char *) * argc) : NULL;
  char **envp_store = envc > 0 ? (char **)alloca(sizeof(char *) * envc) : NULL;

  for (int i = envc - 1; i >= 0; i--) {
    size_t len = strlen(envp[i]) + 1;
    sp -= len;
    memcpy((void *)sp, envp[i], len);
    envp_store[i] = (char *)sp;
  }
  for (int i = argc - 1; i >= 0; i--) {
    size_t len = strlen(argv[i]) + 1;
    sp -= len;
    memcpy((void *)sp, argv[i], len);
    argv_store[i] = (char *)sp;
  }

  sp &= ~(uintptr_t)(sizeof(uintptr_t) - 1);

  sp -= sizeof(uintptr_t);
  *(uintptr_t *)sp = 0;
  for (int i = envc - 1; i >= 0; i--) {
    sp -= sizeof(uintptr_t);
    *(uintptr_t *)sp = (uintptr_t)envp_store[i];
  }

  sp -= sizeof(uintptr_t);
  *(uintptr_t *)sp = 0;
  for (int i = argc - 1; i >= 0; i--) {
    sp -= sizeof(uintptr_t);
    *(uintptr_t *)sp = (uintptr_t)argv_store[i];
  }

  sp -= sizeof(uintptr_t);
  *(uintptr_t *)sp = argc;
  return sp;
}

void naive_uload(PCB *pcb, const char *filename) {
  uintptr_t entry = loader(pcb, filename);
  Log("Jump to entry = %p", entry);
  ((void(*)())entry) ();
}

void context_uload(PCB *pcb, const char *filename, char *const argv[], char *const envp[]) {
  uintptr_t entry = loader(pcb, filename);
  Area kstack = { .start = pcb->stack, .end = pcb->stack + STACK_SIZE };
  pcb->cp = ucontext(NULL, kstack, (void *)entry);
  pcb->cp->GPRx = build_user_stack(argv, envp);
}
