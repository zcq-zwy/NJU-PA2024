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

#include "sdb.h"

#define NR_WP 32

typedef struct watchpoint {
  int NO;
  struct watchpoint *next;

  /* TODO: Add more members if necessary */
  char expr[256];   // 监视表达式
  word_t last_val;  // 上一次表达式值

} WP;

static WP wp_pool[NR_WP] = {};
static WP *head = NULL, *free_ = NULL;

void init_wp_pool() {
  int i;
  for (i = 0; i < NR_WP; i ++) {
    wp_pool[i].NO = i;
    wp_pool[i].next = (i == NR_WP - 1 ? NULL : &wp_pool[i + 1]);
  }

  head = NULL;
  free_ = wp_pool;
}

// 从空闲链表取一个监视点，插入已使用链表头
  static WP* new_wp(void) {
    assert(free_ != NULL);
    WP *wp = free_;
    free_ = free_->next;

    wp->next = head;
    head = wp;
    return wp;
  }

  // 从已使用链表删除指定监视点，并归还到空闲链表
  static void free_wp(WP *wp) {
    assert(wp != NULL);

    if (head == wp) {
      head = wp->next;
    } else {
      WP *p = head;
      while (p != NULL && p->next != wp) p = p->next;
      assert(p != NULL);
      p->next = wp->next;
    }

    wp->next = free_;
    free_ = wp;
  }

  bool wp_add(const char *expr_str) {
    if (expr_str == NULL) return false;

    WP *wp = new_wp();

    // 保存表达式字符串
    strncpy(wp->expr, expr_str, sizeof(wp->expr) - 1);
    wp->expr[sizeof(wp->expr) - 1] = '\0';

    // 计算初始值
    bool success = true;
    wp->last_val = expr(wp->expr, &success);
    if (!success) {
      printf("Bad expression for watchpoint: %s\n", wp->expr);
      free_wp(wp);
      return false;
    }

    printf("Watchpoint %d: %s = " FMT_WORD "\n", wp->NO, wp->expr, wp->last_val);
    return true;
  }

  bool wp_del(int no) {
    WP *p = head;
    while (p != NULL) {
      if (p->NO == no) {
        free_wp(p);
        return true;
      }
      p = p->next;
    }
    return false;
  }

/* TODO: Implement the functionality of watchpoint */

void wp_display(void) {
    if (head == NULL) {
      printf("No watchpoints.\n");
      return;
    }

    printf("Num\tValue\t\tExpr\n");
    for (WP *p = head; p != NULL; p = p->next) {
      printf("%d\t" FMT_WORD "\t%s\n", p->NO, p->last_val, p->expr);
    }
  }

  bool wp_check(void) {
    bool triggered = false;

    for (WP *p = head; p != NULL; p = p->next) {
      bool success = true;
      word_t new_val = expr(p->expr, &success);
      if (!success) continue;

      if (new_val != p->last_val) {
        printf("Watchpoint %d triggered: %s\n", p->NO, p->expr);
        printf("Old value = " FMT_WORD "\n", p->last_val);
        printf("New value = " FMT_WORD "\n", new_val);
        p->last_val = new_val;
        triggered = true;
      }
    }

    return triggered;
  }

