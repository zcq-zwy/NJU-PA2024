/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#ifndef __DEVICE_SNAPSHOT_H__
#define __DEVICE_SNAPSHOT_H__

#include <common.h>

#define KEYBOARD_SNAPSHOT_QUEUE_LEN 1024

typedef struct {
  int key_queue[KEYBOARD_SNAPSHOT_QUEUE_LEN];
  int key_f;
  int key_r;
} KeyboardSnapshot;

typedef struct {
  uint32_t sbuf_rpos;
  uint32_t sbuf_count;
  bool audio_started;
  bool audio_need_prefill;
  uint32_t audio_prefill_bytes;
  uint64_t underflow_events;
  uint64_t underflow_bytes;
  uint32_t last_underflow_report_ms;
} AudioSnapshot;

size_t snapshot_io_space_size();
void snapshot_io_space_save(void *buf, size_t size);
void snapshot_io_space_load(const void *buf, size_t size);

#ifdef CONFIG_HAS_KEYBOARD
void keyboard_snapshot_save(KeyboardSnapshot *out);
void keyboard_snapshot_load(const KeyboardSnapshot *in);
#else
static inline void keyboard_snapshot_save(KeyboardSnapshot *out) { memset(out, 0, sizeof(*out)); }
static inline void keyboard_snapshot_load(const KeyboardSnapshot *in) { (void)in; }
#endif

#ifdef CONFIG_HAS_AUDIO
void audio_snapshot_save(AudioSnapshot *out);
void audio_snapshot_load(const AudioSnapshot *in);
#else
static inline void audio_snapshot_save(AudioSnapshot *out) { memset(out, 0, sizeof(*out)); }
static inline void audio_snapshot_load(const AudioSnapshot *in) { (void)in; }
#endif

#endif
