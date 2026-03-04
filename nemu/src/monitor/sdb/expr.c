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

#include <isa.h>

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <regex.h>

// 1) token 类型
enum {
  TK_NOTYPE = 256, TK_EQ,
    TK_NEQ, TK_AND,      
    TK_DEC, TK_HEX,      // 十进制、十六进制常量
    TK_REG,              // 寄存器名，例如 $pc / $a0

  /* TODO: Add more token types */

};

static struct rule {
  const char *regex;
  int token_type;
} rules[] = {

  /* TODO: Add more rules.
   * Pay attention to the precedence level of different rules.
   */
   // 2) 规则（顺序很重要）
   // \\+ \\* \\( 这些要双反斜杠（C 字符串转义）
  // - HEX 要放在 DEC 前面，避免 0x10 先被 [0-9]+ 匹配成 0
    {" +", TK_NOTYPE},             // 空格
    {"==", TK_EQ},                 // 相等
    {"!=", TK_NEQ},                // 不等
    {"&&", TK_AND},                // 逻辑与

    {"\\+", '+'},                  // +
    {"-", '-'},                    // -
    {"\\*", '*'},                  // *
    {"/", '/'},                    // /
    {"\\(", '('},                  // (
    {"\\)", ')'},                  // )

    {"0[xX][0-9a-fA-F]+", TK_HEX}, // 十六进制常量
    {"[0-9]+", TK_DEC},            // 十进制常量
    {"\\$?[a-zA-Z][a-zA-Z0-9]*", TK_REG}, // 寄存器名: $pc / pc / $a0
};

#define NR_REGEX ARRLEN(rules)

static regex_t re[NR_REGEX] = {};

/* Rules are used for many times.
 * Therefore we compile them only once before any usage.
 */
void init_regex() {
  int i;
  char error_msg[128];
  int ret;

  for (i = 0; i < NR_REGEX; i ++) {
    ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
    if (ret != 0) {
      regerror(ret, &re[i], error_msg, 128);
      panic("regex compilation failed: %s\n%s", error_msg, rules[i].regex);
    }
  }
}

typedef struct token {
  int type;
  char str[32];
} Token;

static Token tokens[32] __attribute__((used)) = {};
static int nr_token __attribute__((used))  = 0;

static bool make_token(char *e) {
  int position = 0;
  int i;
  regmatch_t pmatch;

  nr_token = 0;

  while (e[position] != '\0') {
    /* Try all rules one by one. */
    for (i = 0; i < NR_REGEX; i ++) {
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
        char *substr_start = e + position;
        int substr_len = pmatch.rm_eo;

        Log("match rules[%d] = \"%s\" at position %d with len %d: %.*s",
            i, rules[i].regex, position, substr_len, substr_len, substr_start);

        position += substr_len;

        /* TODO: Now a new token is recognized with rules[i]. Add codes
         * to record the token in the array `tokens'. For certain types
         * of tokens, some extra actions should be performed.
         */
        // 3) 识别后写入 tokens[]
       switch (rules[i].token_type) {
        case TK_NOTYPE:
      // 空白符不进入 token 数组
        break;

        default:
      // 防止 token 数组越界
          if (nr_token >= ARRLEN(tokens)) {
            printf("too many tokens\n");
            return false;
      }

      // 记录 token 类型
      tokens[nr_token].type = rules[i].token_type;

      // 仅对需要保存字面值的 token 复制字符串
      if (rules[i].token_type == TK_DEC ||
          rules[i].token_type == TK_HEX ||
          rules[i].token_type == TK_REG) {
        int n = substr_len;
        if (n >= (int)sizeof(tokens[nr_token].str)) {
          n = (int)sizeof(tokens[nr_token].str) - 1;
        }
        memcpy(tokens[nr_token].str, substr_start, n);
        tokens[nr_token].str[n] = '\0';
      }

      nr_token++;
      break;
  }

        break;
      }
    }

    if (i == NR_REGEX) {
      printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
      return false;
    }
  }

  return true;
}

static int precedence(int type) {
    // 数值越小优先级越低（越可能成为主运算符）
    switch (type) {
      case TK_AND: return 1;           // 最低
      case TK_EQ:
      case TK_NEQ: return 2;
      case '+':
      case '-':    return 3;
      case '*':
      case '/':    return 4;           // 最高
      default:     return 0;           // 非运算符
    }
  }

  static int find_main_op(int p, int q) {
    int op = -1;
    int best_prec = 100;
    int balance = 0;

    for (int i = p; i <= q; i++) {
      int t = tokens[i].type;

      if (t == '(') { balance++; continue; }
      if (t == ')') { balance--; continue; }

      // 括号内部跳过
      if (balance != 0) continue;

      int prec = precedence(t);
      if (prec == 0) continue;

      // 选“最低优先级”；同优先级取最右（满足左结合）
      if (prec <= best_prec) {
        best_prec = prec;
        op = i;
      }
    }

    return op;
  }



static bool check_parentheses(int p, int q) {
    // 先要求两端是 '(' 和 ')'
    if (tokens[p].type != '(' || tokens[q].type != ')') {
      return false;
    }

    int balance = 0;
    for (int i = p; i <= q; i++) {
      if (tokens[i].type == '(') balance++;
      else if (tokens[i].type == ')') balance--;

      // 右括号过多，非法
      if (balance < 0) return false;

      // 如果在 q 之前就回到 0，说明外层括号并没有包住整段
      if (balance == 0 && i < q) return false;
    }

    // 最终必须完全匹配
    return balance == 0;
  }

 static word_t eval(int p, int q, bool *success) {
    // 非法区间
    if (p > q) {
      *success = false;
      return 0;
    }

    // 基础情形：只有一个 token
    if (p == q) {
      if (tokens[p].type == TK_DEC) {
        return (word_t)strtoul(tokens[p].str, NULL, 10);
      }

      if (tokens[p].type == TK_HEX) {
        return (word_t)strtoul(tokens[p].str, NULL, 16);
      }

      if (tokens[p].type == TK_REG) {
        // 兼容 "$pc" 和 "pc" 两种写法
        const char *name = tokens[p].str;
        if (name[0] == '$') name++;

        bool ok = true;
        word_t val = isa_reg_str2val(name, &ok);
        if (!ok) *success = false;
        return val;
      }

      *success = false;
      return 0;
    }
    // 若整段被一对最外层括号包裹，先去掉外层括号再递归
    if (check_parentheses(p, q)) {
      return eval(p + 1, q - 1, success);
    }

    // 在当前层（不进括号）找主运算符
    int op = find_main_op(p, q);
    if (op < 0) {
      *success = false;
      return 0;
    }

    // 递归求左右子表达式
    word_t lhs = eval(p, op - 1, success);
    if (!*success) return 0;
    word_t rhs = eval(op + 1, q, success);
    if (!*success) return 0;

    // 按主运算符计算
    switch (tokens[op].type) {
      case '+': return lhs + rhs;
      case '-': return lhs - rhs;
      case '*': return lhs * rhs;
      case '/':
        if (rhs == 0) {
          *success = false;
          return 0;
        }
        return lhs / rhs;
      case TK_EQ:  return lhs == rhs;
      case TK_NEQ: return lhs != rhs;
      case TK_AND: return lhs && rhs;
      default:
        *success = false;
        return 0;
    }
  }


word_t expr(char *e, bool *success) {
    if (!make_token(e)) {
      *success = false;
      return 0;
    }

    if (nr_token == 0) {
      *success = false;
      return 0;
    }

    *success = true;
    return eval(0, nr_token - 1, success);
  }
