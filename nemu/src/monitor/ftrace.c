#include <monitor/ftrace.h>
#include <utils.h>
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CONFIG_FTRACE

typedef struct {
  vaddr_t addr;
  vaddr_t end;
  char name[96];
  char owner[64];
} FuncSym;

static FuncSym *funcs = NULL;
static int func_cnt = 0;
static int func_cap = 0;
static bool ftrace_ready = false;
static int call_depth = 0;

static const FuncSym *lookup_func(vaddr_t addr) {
  for (int i = 0; i < func_cnt; i++) {
    if (addr >= funcs[i].addr && addr < funcs[i].end) {
      return &funcs[i];
    }
  }
  return NULL;
}

static const char *base_name(const char *path) {
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

static void print_indent(int depth) {
  for (int i = 0; i < depth; i++) {
    log_write("  ");
  }
}

static void reserve_func_capacity(int extra) {
  int need = func_cnt + extra;
  if (need <= func_cap) return;

  int new_cap = (func_cap == 0 ? 128 : func_cap);
  while (new_cap < need) new_cap *= 2;

  funcs = (FuncSym *)realloc(funcs, sizeof(FuncSym) * new_cap);
  Assert(funcs != NULL, "ftrace: out of memory for function symbols");
  func_cap = new_cap;
}

static void append_func(vaddr_t addr, vaddr_t size, const char *name, const char *owner) {
  reserve_func_capacity(1);
  funcs[func_cnt].addr = addr;
  funcs[func_cnt].end = (size > 0 ? addr + size : addr + 4);
  snprintf(funcs[func_cnt].name, sizeof(funcs[func_cnt].name), "%s", name);
  snprintf(funcs[func_cnt].owner, sizeof(funcs[func_cnt].owner), "%s", owner);
  func_cnt++;
}

static int load_elf32(FILE *fp, const Elf32_Ehdr *eh, const char *owner) {
  Elf32_Shdr *shdrs = (Elf32_Shdr *)malloc(eh->e_shentsize * eh->e_shnum);
  Assert(shdrs != NULL, "ftrace: out of memory for section headers");

  fseek(fp, eh->e_shoff, SEEK_SET);
  Assert(fread(shdrs, eh->e_shentsize, eh->e_shnum, fp) == eh->e_shnum,
      "ftrace: failed to read ELF32 section headers");

  int symidx = -1;
  for (int i = 0; i < eh->e_shnum; i++) {
    if (shdrs[i].sh_type == SHT_SYMTAB) {
      symidx = i;
      break;
    }
  }
  Assert(symidx >= 0, "ftrace: ELF32 has no SHT_SYMTAB");

  Elf32_Shdr symsec = shdrs[symidx];
  Assert(symsec.sh_entsize == sizeof(Elf32_Sym), "ftrace: invalid ELF32 symtab entry size");
  Assert(symsec.sh_link < eh->e_shnum, "ftrace: invalid ELF32 sh_link for symtab");
  Elf32_Shdr strsec = shdrs[symsec.sh_link];

  char *strtab = (char *)malloc(strsec.sh_size);
  Assert(strtab != NULL, "ftrace: out of memory for ELF32 strtab");
  fseek(fp, strsec.sh_offset, SEEK_SET);
  Assert(fread(strtab, 1, strsec.sh_size, fp) == strsec.sh_size,
      "ftrace: failed to read ELF32 strtab");

  int nsyms = symsec.sh_size / sizeof(Elf32_Sym);
  Elf32_Sym *syms = (Elf32_Sym *)malloc(symsec.sh_size);
  Assert(syms != NULL, "ftrace: out of memory for ELF32 symtab");
  fseek(fp, symsec.sh_offset, SEEK_SET);
  Assert(fread(syms, sizeof(Elf32_Sym), nsyms, fp) == (size_t)nsyms,
      "ftrace: failed to read ELF32 symtab");

  int loaded = 0;
  for (int i = 0; i < nsyms; i++) {
    if (ELF32_ST_TYPE(syms[i].st_info) != STT_FUNC) continue;
    if (syms[i].st_name >= strsec.sh_size) continue;

    const char *nm = strtab + syms[i].st_name;
    if (*nm == '\0') continue;

    append_func(syms[i].st_value, syms[i].st_size, nm, owner);
    loaded++;
  }

  free(syms);
  free(strtab);
  free(shdrs);
  return loaded;
}

static int load_elf64(FILE *fp, const Elf64_Ehdr *eh, const char *owner) {
  Elf64_Shdr *shdrs = (Elf64_Shdr *)malloc(eh->e_shentsize * eh->e_shnum);
  Assert(shdrs != NULL, "ftrace: out of memory for section headers");

  fseek(fp, eh->e_shoff, SEEK_SET);
  Assert(fread(shdrs, eh->e_shentsize, eh->e_shnum, fp) == eh->e_shnum,
      "ftrace: failed to read ELF64 section headers");

  int symidx = -1;
  for (int i = 0; i < eh->e_shnum; i++) {
    if (shdrs[i].sh_type == SHT_SYMTAB) {
      symidx = i;
      break;
    }
  }
  Assert(symidx >= 0, "ftrace: ELF64 has no SHT_SYMTAB");

  Elf64_Shdr symsec = shdrs[symidx];
  Assert(symsec.sh_entsize == sizeof(Elf64_Sym), "ftrace: invalid ELF64 symtab entry size");
  Assert(symsec.sh_link < eh->e_shnum, "ftrace: invalid ELF64 sh_link for symtab");
  Elf64_Shdr strsec = shdrs[symsec.sh_link];

  char *strtab = (char *)malloc(strsec.sh_size);
  Assert(strtab != NULL, "ftrace: out of memory for ELF64 strtab");
  fseek(fp, strsec.sh_offset, SEEK_SET);
  Assert(fread(strtab, 1, strsec.sh_size, fp) == strsec.sh_size,
      "ftrace: failed to read ELF64 strtab");

  int nsyms = symsec.sh_size / sizeof(Elf64_Sym);
  Elf64_Sym *syms = (Elf64_Sym *)malloc(symsec.sh_size);
  Assert(syms != NULL, "ftrace: out of memory for ELF64 symtab");
  fseek(fp, symsec.sh_offset, SEEK_SET);
  Assert(fread(syms, sizeof(Elf64_Sym), nsyms, fp) == (size_t)nsyms,
      "ftrace: failed to read ELF64 symtab");

  int loaded = 0;
  for (int i = 0; i < nsyms; i++) {
    if (ELF64_ST_TYPE(syms[i].st_info) != STT_FUNC) continue;
    if (syms[i].st_name >= strsec.sh_size) continue;

    const char *nm = strtab + syms[i].st_name;
    if (*nm == '\0') continue;

    append_func(syms[i].st_value, syms[i].st_size, nm, owner);
    loaded++;
  }

  free(syms);
  free(strtab);
  free(shdrs);
  return loaded;
}

static int load_one_elf(const char *elf_file) {
  FILE *fp = fopen(elf_file, "rb");
  Assert(fp != NULL, "ftrace: cannot open ELF file '%s'", elf_file);

  unsigned char ident[EI_NIDENT];
  Assert(fread(ident, 1, EI_NIDENT, fp) == EI_NIDENT, "ftrace: failed to read ELF ident");
  Assert(ident[EI_MAG0] == ELFMAG0 && ident[EI_MAG1] == ELFMAG1 &&
      ident[EI_MAG2] == ELFMAG2 && ident[EI_MAG3] == ELFMAG3,
      "ftrace: '%s' is not an ELF file", elf_file);

  const char *owner = base_name(elf_file);
  int loaded = 0;

  fseek(fp, 0, SEEK_SET);
  if (ident[EI_CLASS] == ELFCLASS32) {
    Elf32_Ehdr eh;
    Assert(fread(&eh, 1, sizeof(eh), fp) == sizeof(eh), "ftrace: failed to read ELF32 header");
    loaded = load_elf32(fp, &eh, owner);
  }
  else if (ident[EI_CLASS] == ELFCLASS64) {
    Elf64_Ehdr eh;
    Assert(fread(&eh, 1, sizeof(eh), fp) == sizeof(eh), "ftrace: failed to read ELF64 header");
    loaded = load_elf64(fp, &eh, owner);
  }
  else {
    panic("ftrace: unsupported ELF class %d", ident[EI_CLASS]);
  }

  fclose(fp);
  Log("ftrace: loaded %d function symbols from %s", loaded, elf_file);
  return loaded;
}

void init_ftrace(int elf_count, char *elf_files[]) {
  if (elf_count == 0) {
    Log("ftrace: disabled because no ELF file was provided");
    return;
  }

  int total = 0;
  for (int i = 0; i < elf_count; i++) {
    total += load_one_elf(elf_files[i]);
  }

  ftrace_ready = (total > 0);
  Log("ftrace: total loaded %d function symbols from %d ELF(s)", total, elf_count);
}

void ftrace_call(vaddr_t pc, vaddr_t target) {
  if (!ftrace_ready) return;

  const FuncSym *sym = lookup_func(target);
  print_indent(call_depth);
  log_write("ftrace: " FMT_WORD ": call [%s%s%s@" FMT_WORD "]\n",
      pc,
      sym ? sym->owner : "?",
      sym ? ":" : "",
      sym ? sym->name : "?",
      target);
  call_depth++;
}

void ftrace_ret(vaddr_t pc, vaddr_t target) {
  if (!ftrace_ready) return;

  if (call_depth > 0) call_depth--;
  const FuncSym *sym = lookup_func(pc);
  print_indent(call_depth);
  log_write("ftrace: " FMT_WORD ": ret  [%s%s%s] -> " FMT_WORD "\n",
      pc,
      sym ? sym->owner : "?",
      sym ? ":" : "",
      sym ? sym->name : "?",
      target);
}

#else

void init_ftrace(int elf_count, char *elf_files[]) {
  (void)elf_count;
  (void)elf_files;
}

void ftrace_call(vaddr_t pc, vaddr_t target) {
  (void)pc;
  (void)target;
}

void ftrace_ret(vaddr_t pc, vaddr_t target) {
  (void)pc;
  (void)target;
}

#endif
