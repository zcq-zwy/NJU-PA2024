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
} FuncSym;

static FuncSym *funcs = NULL;
static int func_cnt = 0;
static bool ftrace_ready = false;
static int call_depth = 0;

static const char *lookup_func(vaddr_t addr) {
  for (int i = 0; i < func_cnt; i++) {
    if (addr >= funcs[i].addr && addr < funcs[i].end) {
      return funcs[i].name;
    }
  }
  return "?";
}

static void print_indent(int depth) {
  for (int i = 0; i < depth; i++) {
    log_write("  ");
  }
}

static void load_elf32(FILE *fp, const Elf32_Ehdr *eh) {
  Elf32_Shdr *shdrs = (Elf32_Shdr *)malloc(eh->e_shentsize * eh->e_shnum);
  Assert(shdrs != NULL, "ftrace: out of memory for section headers");

  fseek(fp, eh->e_shoff, SEEK_SET);
  Assert(fread(shdrs, eh->e_shentsize, eh->e_shnum, fp) == eh->e_shnum,
      "ftrace: failed to read ELF32 section headers");

  int symidx = -1;
  for (int i = 0; i < eh->e_shnum; i++) {
    if (shdrs[i].sh_type == SHT_SYMTAB) { symidx = i; break; }
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

  funcs = (FuncSym *)malloc(sizeof(FuncSym) * nsyms);
  Assert(funcs != NULL, "ftrace: out of memory for function symbols");

  for (int i = 0; i < nsyms; i++) {
    if (ELF32_ST_TYPE(syms[i].st_info) != STT_FUNC) continue;
    if (syms[i].st_name >= strsec.sh_size) continue;

    const char *nm = strtab + syms[i].st_name;
    if (*nm == '\0') continue;

    vaddr_t addr = syms[i].st_value;
    vaddr_t sz = syms[i].st_size;
    funcs[func_cnt].addr = addr;
    funcs[func_cnt].end = (sz > 0 ? addr + sz : addr + 4);
    snprintf(funcs[func_cnt].name, sizeof(funcs[func_cnt].name), "%s", nm);
    func_cnt++;
  }

  free(syms);
  free(strtab);
  free(shdrs);
}

static void load_elf64(FILE *fp, const Elf64_Ehdr *eh) {
  Elf64_Shdr *shdrs = (Elf64_Shdr *)malloc(eh->e_shentsize * eh->e_shnum);
  Assert(shdrs != NULL, "ftrace: out of memory for section headers");

  fseek(fp, eh->e_shoff, SEEK_SET);
  Assert(fread(shdrs, eh->e_shentsize, eh->e_shnum, fp) == eh->e_shnum,
      "ftrace: failed to read ELF64 section headers");

  int symidx = -1;
  for (int i = 0; i < eh->e_shnum; i++) {
    if (shdrs[i].sh_type == SHT_SYMTAB) { symidx = i; break; }
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

  funcs = (FuncSym *)malloc(sizeof(FuncSym) * nsyms);
  Assert(funcs != NULL, "ftrace: out of memory for function symbols");

  for (int i = 0; i < nsyms; i++) {
    if (ELF64_ST_TYPE(syms[i].st_info) != STT_FUNC) continue;
    if (syms[i].st_name >= strsec.sh_size) continue;

    const char *nm = strtab + syms[i].st_name;
    if (*nm == '\0') continue;

    vaddr_t addr = syms[i].st_value;
    vaddr_t sz = syms[i].st_size;
    funcs[func_cnt].addr = addr;
    funcs[func_cnt].end = (sz > 0 ? addr + sz : addr + 4);
    snprintf(funcs[func_cnt].name, sizeof(funcs[func_cnt].name), "%s", nm);
    func_cnt++;
  }

  free(syms);
  free(strtab);
  free(shdrs);
}

void init_ftrace(const char *elf_file) {
  if (elf_file == NULL) {
    Log("ftrace: disabled because no ELF file was provided");
    return;
  }

  FILE *fp = fopen(elf_file, "rb");
  Assert(fp != NULL, "ftrace: cannot open ELF file '%s'", elf_file);

  unsigned char ident[EI_NIDENT];
  Assert(fread(ident, 1, EI_NIDENT, fp) == EI_NIDENT, "ftrace: failed to read ELF ident");
  Assert(ident[EI_MAG0] == ELFMAG0 && ident[EI_MAG1] == ELFMAG1 &&
      ident[EI_MAG2] == ELFMAG2 && ident[EI_MAG3] == ELFMAG3,
      "ftrace: '%s' is not an ELF file", elf_file);

  fseek(fp, 0, SEEK_SET);
  if (ident[EI_CLASS] == ELFCLASS32) {
    Elf32_Ehdr eh;
    Assert(fread(&eh, 1, sizeof(eh), fp) == sizeof(eh), "ftrace: failed to read ELF32 header");
    load_elf32(fp, &eh);
  }
  else if (ident[EI_CLASS] == ELFCLASS64) {
    Elf64_Ehdr eh;
    Assert(fread(&eh, 1, sizeof(eh), fp) == sizeof(eh), "ftrace: failed to read ELF64 header");
    load_elf64(fp, &eh);
  }
  else {
    panic("ftrace: unsupported ELF class %d", ident[EI_CLASS]);
  }

  fclose(fp);
  ftrace_ready = true;
  Log("ftrace: loaded %d function symbols from %s", func_cnt, elf_file);
}

void ftrace_call(vaddr_t pc, vaddr_t target) {
  if (!ftrace_ready) return;
  print_indent(call_depth);
  log_write("ftrace: " FMT_WORD ": call [%s@" FMT_WORD "]\n",
      pc, lookup_func(target), target);
  call_depth++;
}

void ftrace_ret(vaddr_t pc, vaddr_t target) {
  if (!ftrace_ready) return;
  if (call_depth > 0) call_depth--;
  print_indent(call_depth);
  log_write("ftrace: " FMT_WORD ": ret  [%s] -> " FMT_WORD "\n",
      pc, lookup_func(pc), target);
}

#else

void init_ftrace(const char *elf_file) {
  (void)elf_file;
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
