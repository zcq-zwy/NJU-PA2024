#include <stdio.h>
#include <string.h>

#include <isa.h>
#include <cpu/cpu.h>
#include <cpu/difftest.h>
#include <memory/paddr.h>
#include <device/snapshot.h>
#include <utils.h>

#define SNAPSHOT_MAGIC "NEMUSNP1"
#define SNAPSHOT_VERSION 1u

typedef struct {
  char magic[8];
  uint32_t version;
  uint32_t pmem_size;
  uint32_t io_space_size;
  uint32_t cpu_size;
  uint32_t keyboard_size;
  uint32_t audio_size;
  uint32_t difftest_enabled;
} SnapshotHeader;

static bool write_full(FILE *fp, const void *buf, size_t size) {
  return fwrite(buf, 1, size, fp) == size;
}

static bool read_full(FILE *fp, void *buf, size_t size) {
  return fread(buf, 1, size, fp) == size;
}

bool snapshot_save(const char *path) {
  FILE *fp = fopen(path, "wb");
  if (fp == NULL) {
    perror("save");
    return false;
  }

  KeyboardSnapshot keyboard = {};
  AudioSnapshot audio = {};
  size_t io_space_size = snapshot_io_space_size();
  SnapshotHeader hdr = {
    .version = SNAPSHOT_VERSION,
    .pmem_size = CONFIG_MSIZE,
    .io_space_size = io_space_size,
    .cpu_size = sizeof(cpu),
    .keyboard_size = sizeof(keyboard),
    .audio_size = sizeof(audio),
    .difftest_enabled = difftest_is_enabled(),
  };
  memcpy(hdr.magic, SNAPSHOT_MAGIC, sizeof(hdr.magic));

  keyboard_snapshot_save(&keyboard);
  audio_snapshot_save(&audio);

  uint8_t *io_buf = malloc(io_space_size);
  Assert(io_buf != NULL, "snapshot: can not allocate io buffer");
  snapshot_io_space_save(io_buf, io_space_size);

  bool ok =
    write_full(fp, &hdr, sizeof(hdr)) &&
    write_full(fp, &cpu, sizeof(cpu)) &&
    write_full(fp, &nemu_state, sizeof(nemu_state)) &&
    write_full(fp, &keyboard, sizeof(keyboard)) &&
    write_full(fp, &audio, sizeof(audio)) &&
    write_full(fp, guest_to_host(PMEM_LEFT), CONFIG_MSIZE) &&
    write_full(fp, io_buf, io_space_size);

  free(io_buf);
  fclose(fp);
  if (!ok) {
    printf("Failed to write snapshot to %s\n", path);
    return false;
  }
  printf("Snapshot saved to %s\n", path);
  return true;
}

bool snapshot_load(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    perror("load");
    return false;
  }

  SnapshotHeader hdr = {};
  CPU_state cpu_saved = {};
  NEMUState nemu_saved = {};
  KeyboardSnapshot keyboard = {};
  AudioSnapshot audio = {};

  bool ok = read_full(fp, &hdr, sizeof(hdr));
  if (!ok || memcmp(hdr.magic, SNAPSHOT_MAGIC, sizeof(hdr.magic)) != 0) {
    printf("Invalid snapshot file: %s\n", path);
    fclose(fp);
    return false;
  }

  if (hdr.version != SNAPSHOT_VERSION || hdr.pmem_size != CONFIG_MSIZE ||
      hdr.io_space_size != snapshot_io_space_size() || hdr.cpu_size != sizeof(cpu) ||
      hdr.keyboard_size != sizeof(keyboard) || hdr.audio_size != sizeof(audio)) {
    printf("Snapshot is incompatible with current NEMU build: %s\n", path);
    fclose(fp);
    return false;
  }

  uint8_t *io_buf = malloc(hdr.io_space_size);
  Assert(io_buf != NULL, "snapshot: can not allocate io buffer");
  ok =
    read_full(fp, &cpu_saved, sizeof(cpu_saved)) &&
    read_full(fp, &nemu_saved, sizeof(nemu_saved)) &&
    read_full(fp, &keyboard, sizeof(keyboard)) &&
    read_full(fp, &audio, sizeof(audio)) &&
    read_full(fp, guest_to_host(PMEM_LEFT), CONFIG_MSIZE) &&
    read_full(fp, io_buf, hdr.io_space_size);
  fclose(fp);

  if (!ok) {
    free(io_buf);
    printf("Failed to load snapshot from %s\n", path);
    return false;
  }

  difftest_detach();
  cpu = cpu_saved;
  nemu_state = nemu_saved;
  snapshot_io_space_load(io_buf, hdr.io_space_size);
  free(io_buf);

  keyboard_snapshot_load(&keyboard);
  audio_snapshot_load(&audio);

  nemu_state.state = NEMU_STOP;
  if (hdr.difftest_enabled) {
    difftest_attach();
  }

  printf("Snapshot loaded from %s\n", path);
  return true;
}
