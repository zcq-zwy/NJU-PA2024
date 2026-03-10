#include <proc.h>
#include <elf.h>
#include <fs.h>
#include <alloca.h>

#ifdef __LP64__
# define Elf_Ehdr Elf64_Ehdr
# define Elf_Phdr Elf64_Phdr
# define Elf_Dyn  Elf64_Dyn
# define Elf_Rela Elf64_Rela
# define Elf_Sym  Elf64_Sym
# define ELF_R_TYPE ELF64_R_TYPE
# define ELF_R_SYM  ELF64_R_SYM
#else
# define Elf_Ehdr Elf32_Ehdr
# define Elf_Phdr Elf32_Phdr
# define Elf_Dyn  Elf32_Dyn
# define Elf_Rela Elf32_Rela
# define Elf_Sym  Elf32_Sym
# define ELF_R_TYPE ELF32_R_TYPE
# define ELF_R_SYM  ELF32_R_SYM
#endif

#if !defined(R_RISCV_32)
# define R_RISCV_32 1
#endif
#if !defined(R_RISCV_RELATIVE)
# define R_RISCV_RELATIVE 3
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

static void load_segment(int fd, const Elf_Phdr *phdr, uintptr_t load_bias) {
  fs_lseek(fd, phdr->p_offset, SEEK_SET);
  fs_read(fd, (void *)(load_bias + phdr->p_vaddr), phdr->p_filesz);
  memset((void *)(load_bias + phdr->p_vaddr + phdr->p_filesz), 0, phdr->p_memsz - phdr->p_filesz);
}

#ifdef HAS_VME
static uint8_t *map_user(PCB *pcb, uintptr_t va, size_t size, int prot) {
  assert(pcb != NULL && pcb->as.ptr != NULL);
  assert(size > 0);

  uintptr_t va_start = ROUNDDOWN(va, PGSIZE);
  uintptr_t va_end = ROUNDUP(va + size, PGSIZE);
  size_t nr_page = (va_end - va_start) / PGSIZE;
  uint8_t *pa = (uint8_t *)new_page(nr_page);

  for (size_t i = 0; i < nr_page; i++) {
    map(&pcb->as, (void *)(va_start + i * PGSIZE), pa + i * PGSIZE, prot);
  }

  return pa + (va - va_start);
}
#endif

static void relocate_dyn(uintptr_t access_bias, uintptr_t runtime_bias, const Elf_Phdr *dyn_phdr) {
  if (dyn_phdr == NULL) return;

  Elf_Dyn *dyn = (Elf_Dyn *)(access_bias + dyn_phdr->p_vaddr);
  uintptr_t rela_addr = 0;
  uintptr_t symtab_addr = 0;
  size_t rela_size = 0;
  size_t rela_ent = sizeof(Elf_Rela);
  size_t sym_ent = sizeof(Elf_Sym);

  for (; dyn->d_tag != DT_NULL; dyn++) {
    switch (dyn->d_tag) {
      case DT_RELA: rela_addr = dyn->d_un.d_ptr; break;
      case DT_RELASZ: rela_size = dyn->d_un.d_val; break;
      case DT_RELAENT: rela_ent = dyn->d_un.d_val; break;
      case DT_SYMTAB: symtab_addr = dyn->d_un.d_ptr; break;
      case DT_SYMENT: sym_ent = dyn->d_un.d_val; break;
    }
  }

  if (rela_addr == 0 || rela_size == 0) return;
  assert(rela_ent == sizeof(Elf_Rela));
  assert(sym_ent == sizeof(Elf_Sym));

  Elf_Sym *symtab = (symtab_addr == 0 ? NULL : (Elf_Sym *)(access_bias + symtab_addr));
  size_t nr_rela = rela_size / rela_ent;
  Elf_Rela *rela = (Elf_Rela *)(access_bias + rela_addr);
  for (size_t i = 0; i < nr_rela; i++) {
    switch (ELF_R_TYPE(rela[i].r_info)) {
      case R_RISCV_RELATIVE:
        *(uintptr_t *)(access_bias + rela[i].r_offset) = runtime_bias + rela[i].r_addend;
        break;
      case R_RISCV_32: {
        assert(symtab != NULL);
        Elf_Sym *sym = &symtab[ELF_R_SYM(rela[i].r_info)];
        *(uint32_t *)(access_bias + rela[i].r_offset) = (uint32_t)(runtime_bias + sym->st_value + rela[i].r_addend);
        break;
      }
      default:
        panic("unsupported PIE relocation type = %d", (int)ELF_R_TYPE(rela[i].r_info));
    }
  }
}

static int loader(PCB *pcb, const char *filename, uintptr_t *entry) {
  if (filename == NULL) {
    filename = "/bin/dummy";
  }

  int fd = fs_open(filename, 0, 0);
  if (fd < 0) return fd;

  Elf_Ehdr ehdr;
  fs_read(fd, &ehdr, sizeof(ehdr));

  assert(*(uint32_t *)ehdr.e_ident == 0x464c457f);
  assert(ehdr.e_machine == EXPECT_TYPE);

  Elf_Phdr *phdrs = (Elf_Phdr *)alloca(ehdr.e_phnum * sizeof(Elf_Phdr));
  fs_lseek(fd, ehdr.e_phoff, SEEK_SET);
  fs_read(fd, phdrs, ehdr.e_phnum * sizeof(Elf_Phdr));

  uintptr_t user_bias = 0;
  uintptr_t host_bias = 0;
  uintptr_t lo = (uintptr_t)-1;
  uintptr_t hi = 0;
  uintptr_t map_lo = 0;
  uintptr_t map_hi = 0;
  Elf_Phdr *dyn_phdr = NULL;
#ifdef HAS_VME
  bool use_vme = (pcb != NULL && pcb->as.ptr != NULL);
#else
  bool use_vme = false;
#endif

  if (ehdr.e_type == ET_DYN) {
    for (int i = 0; i < ehdr.e_phnum; i++) {
      if (phdrs[i].p_type == PT_LOAD) {
        if (phdrs[i].p_vaddr < lo) lo = phdrs[i].p_vaddr;
        if (phdrs[i].p_vaddr + phdrs[i].p_memsz > hi) hi = phdrs[i].p_vaddr + phdrs[i].p_memsz;
      } else if (phdrs[i].p_type == PT_DYNAMIC) {
        dyn_phdr = &phdrs[i];
      }
    }

    assert(lo != (uintptr_t)-1);
    map_lo = ROUNDDOWN(lo, PGSIZE);
    map_hi = ROUNDUP(hi, PGSIZE);
#ifdef HAS_VME
    if (use_vme) {
      size_t nr_page = (map_hi - map_lo) / PGSIZE;
      uint8_t *image = (uint8_t *)new_page(nr_page);
      user_bias = (uintptr_t)pcb->as.area.start - map_lo;
      host_bias = (uintptr_t)image - map_lo;
      for (size_t i = 0; i < nr_page; i++) {
        map(&pcb->as, (void *)(user_bias + map_lo + i * PGSIZE), image + i * PGSIZE, MMAP_READ | MMAP_WRITE);
      }
    } else
#endif
    {
      user_bias = (uintptr_t)new_page((map_hi - map_lo) / PGSIZE) - map_lo;
      host_bias = user_bias;
    }
  }

  for (int i = 0; i < ehdr.e_phnum; i++) {
    if (phdrs[i].p_type != PT_LOAD) continue;
#ifdef HAS_VME
    if (use_vme) {
      uint8_t *seg = NULL;
      if (ehdr.e_type == ET_DYN) {
        seg = (uint8_t *)(host_bias + phdrs[i].p_vaddr);
      } else {
        uintptr_t seg_va = user_bias + phdrs[i].p_vaddr;
        seg = map_user(pcb, seg_va, phdrs[i].p_memsz, MMAP_READ | MMAP_WRITE);
      }
      fs_lseek(fd, phdrs[i].p_offset, SEEK_SET);
      fs_read(fd, seg, phdrs[i].p_filesz);
      memset(seg + phdrs[i].p_filesz, 0, phdrs[i].p_memsz - phdrs[i].p_filesz);
    } else
#endif
    {
      load_segment(fd, &phdrs[i], user_bias);
    }
    uintptr_t seg_hi = user_bias + phdrs[i].p_vaddr + phdrs[i].p_memsz;
    if (seg_hi > pcb->max_brk) pcb->max_brk = seg_hi;
  }

  if (ehdr.e_type == ET_DYN) {
    relocate_dyn(host_bias, user_bias, dyn_phdr);
  } else {
    assert(ehdr.e_type == ET_EXEC);
  }

  fs_close(fd);
  *entry = user_bias + ehdr.e_entry;
  pcb->max_brk = ROUNDUP(pcb->max_brk, PGSIZE);
  return 0;
}

static uintptr_t build_user_stack(PCB *pcb, char *const argv[], char *const envp[]) {
  const int stack_pages = STACK_SIZE / PGSIZE;
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

#ifdef HAS_VME
  if (pcb != NULL && pcb->as.ptr != NULL) {
    uintptr_t ustack_end = (uintptr_t)pcb->as.area.end;
    uintptr_t ustack_start = ustack_end - STACK_SIZE;
    uint8_t *stack = (uint8_t *)new_page(stack_pages);
    for (int i = 0; i < stack_pages; i++) {
      map(&pcb->as, (void *)(ustack_start + i * PGSIZE), stack + i * PGSIZE, MMAP_READ | MMAP_WRITE);
    }

    uintptr_t sp = ustack_end;
    uintptr_t ksp = (uintptr_t)stack + STACK_SIZE;

    for (int i = envc - 1; i >= 0; i--) {
      size_t len = strlen(envp[i]) + 1;
      sp -= len;
      ksp -= len;
      memcpy((void *)ksp, envp[i], len);
      envp_store[i] = (char *)sp;
    }
    for (int i = argc - 1; i >= 0; i--) {
      size_t len = strlen(argv[i]) + 1;
      sp -= len;
      ksp -= len;
      memcpy((void *)ksp, argv[i], len);
      argv_store[i] = (char *)sp;
    }

    sp &= ~(uintptr_t)(sizeof(uintptr_t) - 1);
    ksp &= ~(uintptr_t)(sizeof(uintptr_t) - 1);

    sp -= sizeof(uintptr_t);
    ksp -= sizeof(uintptr_t);
    *(uintptr_t *)ksp = 0;
    for (int i = envc - 1; i >= 0; i--) {
      sp -= sizeof(uintptr_t);
      ksp -= sizeof(uintptr_t);
      *(uintptr_t *)ksp = (uintptr_t)envp_store[i];
    }

    sp -= sizeof(uintptr_t);
    ksp -= sizeof(uintptr_t);
    *(uintptr_t *)ksp = 0;
    for (int i = argc - 1; i >= 0; i--) {
      sp -= sizeof(uintptr_t);
      ksp -= sizeof(uintptr_t);
      *(uintptr_t *)ksp = (uintptr_t)argv_store[i];
    }

    sp -= sizeof(uintptr_t);
    ksp -= sizeof(uintptr_t);
    *(uintptr_t *)ksp = argc;
    return sp;
  }
#endif

  uint8_t *stack = (uint8_t *)new_page(stack_pages);
  uintptr_t top = (uintptr_t)stack + STACK_SIZE;
  uintptr_t sp = top;

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
  uintptr_t entry = 0;
  int ret = loader(pcb, filename, &entry);
  assert(ret == 0);
  Log("Jump to entry = %p", (void *)entry);
  ((void(*)())entry) ();
}

int context_uload(PCB *pcb, const char *filename, char *const argv[], char *const envp[]) {
  uintptr_t entry = 0;
  pcb->max_brk = 0;
#ifdef HAS_VME
  protect(&pcb->as);
#endif
  int ret = loader(pcb, filename, &entry);
  if (ret < 0) return ret;
  Area kstack = { .start = pcb->stack, .end = pcb->stack + STACK_SIZE };
#ifdef HAS_VME
  pcb->cp = ucontext(&pcb->as, kstack, (void *)entry);
#else
  pcb->cp = ucontext(NULL, kstack, (void *)entry);
#endif
  pcb->cp->GPRx = build_user_stack(pcb, argv, envp);
  return 0;
}
