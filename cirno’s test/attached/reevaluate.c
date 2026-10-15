/* ============================================================================
 *  reevaluate.c  ——  对 solutions_filtered.csv 做"重新评价"
 *
 *  背景：
 *    1) solutions.c   生成 solutions.csv（num1..num4,count,expressions）
 *    2) fliter.c      删掉 count>=100 的组，得到 solutions_filtered.csv
 *    3) mark.c        想给"纯加减可解"的组在 T 栏标 1，但有两个毛病：
 *         a. 它把 expressions 那一栏写丢了（只写到第 5 个逗号就换列）
 *         b. 它对"原来已经是 1"的行不再校验，把 solutions.csv 里那几行
 *            错误的 1 原样保留（例如 1,1,1,5 和 1,1,3,15 其实纯加减
 *            根本算不出 9，因为四个数的带符号和永远是偶数）
 *
 *  本程序做的事（旧 T / marked 全部丢弃，重新算）：
 *    · 保留前 6 列：num1,num2,num3,num4,count,expressions （算式一列不再丢失）
 *    · 逐条算式【真正求值】（用分数精确运算，不用 double），统计：
 *        valid  真正等于 9 的算式个数（正常应该 == count）
 *        addsub 其中只用到 + - 的算式个数
 *        pad    含 ×0 ×1 ÷1 +0 -0 这类"注水"操作的算式个数
 *        frac   中间结果出现分数的算式个数（要通分的那种难解）
 *    · T     = 1 表示这 4 个数只用 + - 就能得到 9（纯加减送分题）
 *              做法：枚举 16 种正负号组合，看是否存在 ±a±b±c±d == 9
 *              （只用加减时，任何括号形式的结果都等价于某个带符号和）
 *    · diff  = 0~10 的难度分（越大越难），公式在下面 DIF 区，权重可自己改
 *    · level = easy / mid / hard / extreme
 *
 *  编译（VSCode 终端 / MinGW gcc）：
 *      gcc reevaluate.c -o reevaluate -lm
 *  运行：
 *      reevaluate                                  -> solutions_filtered.csv => solutions_reeval.csv
 *      reevaluate solutions.csv out.csv            -> 也可以直接重算全量文件
 *
 *  输出 solutions_reeval.csv 的列：
 *      num1,num2,num3,num4,count,expressions,valid,addsub,T,pad,frac,has9,min_num,max_num,sum,diff,level
 * ==========================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define TARGET      9            /* 目标值：拼出 9 */
#define MAX_LINE    65536        /* 一行最长（算式列可能很长） */
#define MAX_EXPR    65536        /* 算式列缓冲 */

/* ---------------- 难度权重（想调整难度口味就改这里） ---------------- */
#define W_SOL       5.0   /* 解越少越难：5.0 * (1 - log(count+1)/log(100))  -> 0 ~ 5 */
#define W_NUM       2.5   /* 数越小越难：(18-均值)/17 * 2.5                -> 0 ~ 2.5 */
#define W_FRAC      2.5   /* 越多的解需要分数中间值越难：frac/count * 2.5  -> 0 ~ 2.5 */
#define PEN_T       2.5   /* 纯加减可解（送分题）扣掉 */
#define PEN_PAD     2.0   /* 所有解都是 ×0/×1/÷1 注水凑出来的，扣掉 */
#define MAX_COUNT  100.0  /* 过滤阈值：count 的上限（用于解数归一化） */
#define LV_EXTREME  7.5
#define LV_HARD     5.5
#define LV_MID      3.0

/* ============================ 分数（有理数） ============================ */
/* 用 long long 存分子/分母，避免 double 的 1e-6 误差把不是 9 的算成 9。      */
/* d == 0 表示"无效"（除零 / 语法错误）。                                    */
typedef struct { long long n, d; } Frac;

static long long ll_abs(long long x) { return x < 0 ? -x : x; }

static long long ll_gcd(long long x, long long y) {
    x = ll_abs(x); y = ll_abs(y);
    while (y) { long long t = x % y; x = y; y = t; }
    return x;
}

static Frac fr_make(long long n, long long d) {
    Frac f;
    if (d == 0) { f.n = 0; f.d = 0; return f; }   /* 无效标记 */
    if (d < 0)  { n = -n; d = -d; }
    long long g = ll_gcd(n, d);
    if (g > 1) { n /= g; d /= g; }
    f.n = n; f.d = d;
    return f;
}

static Frac fr_bad(void) { Frac f; f.n = 0; f.d = 0; return f; }
static int  fr_ok(Frac f)     { return f.d != 0; }
static int  fr_is(Frac f, long long k) { return f.d != 0 && f.n == k * f.d; }
static int  fr_is_int(Frac f) { return f.d != 0 && (f.n % f.d == 0); }

static Frac fr_add(Frac a, Frac b) { return fr_make(a.n * b.d + b.n * a.d, a.d * b.d); }
static Frac fr_sub(Frac a, Frac b) { return fr_make(a.n * b.d - b.n * a.d, a.d * b.d); }
static Frac fr_mul(Frac a, Frac b) { return fr_make(a.n * b.n, a.d * b.d); }
static Frac fr_div(Frac a, Frac b) {
    if (b.n == 0) return fr_bad();
    return fr_make(a.n * b.d, a.d * b.n);
}

/* ========================= 算式解析 + 求值 ========================= */
typedef struct {
    const char *s;
    int pos;
    int ok;          /* 语法/除零是否合法 */
    int has_muldiv;  /* 用到了 * 或 / */
    int pad;         /* 出现 ×0 ×1 ÷1 +0 -0 之类注水操作 */
    int frac;        /* 中间结果出现非整数（需要通分） */
} Parse;

static void skip_ws(Parse *p) {
    while (p->s[p->pos] == ' ' || p->s[p->pos] == '\t') p->pos++;
}

static Frac parse_expr(Parse *p);

static Frac parse_factor(Parse *p) {
    skip_ws(p);
    char c = p->s[p->pos];

    if (c == '+') { p->pos++; return parse_factor(p); }
    if (c == '-') {                          /* 一元负号，正常数据里没有，防身用 */
        p->pos++;
        Frac f = parse_factor(p);
        if (fr_ok(f)) f.n = -f.n;
        return f;
    }
    if (c == '(') {
        p->pos++;
        Frac f = parse_expr(p);
        skip_ws(p);
        if (p->s[p->pos] == ')') p->pos++; else p->ok = 0;
        return f;
    }
    if (c >= '0' && c <= '9') {
        long long v = 0;
        while (p->s[p->pos] >= '0' && p->s[p->pos] <= '9') {
            v = v * 10 + (p->s[p->pos] - '0');
            p->pos++;
        }
        return fr_make(v, 1);
    }

    p->ok = 0;
    return fr_bad();
}

static Frac parse_term(Parse *p) {
    Frac a = parse_factor(p);
    for (;;) {
        skip_ws(p);
        char c = p->s[p->pos];
        if (c != '*' && c != '/') break;
        p->pos++;
        p->has_muldiv = 1;

        Frac b = parse_factor(p);
        if (!p->ok || !fr_ok(a) || !fr_ok(b)) { p->ok = 0; return fr_bad(); }

        if (c == '*') {
            if (fr_is(a, 0) || fr_is(a, 1) || fr_is(b, 0) || fr_is(b, 1)) p->pad = 1;
            a = fr_mul(a, b);
        } else {
            if (b.n == 0) { p->ok = 0; return fr_bad(); }   /* 除零 */
            if (fr_is(b, 1) || fr_is(a, 0)) p->pad = 1;
            a = fr_div(a, b);
        }
        if (!fr_ok(a)) { p->ok = 0; return fr_bad(); }
        if (!fr_is_int(a)) p->frac = 1;
    }
    return a;
}

static Frac parse_expr(Parse *p) {
    Frac a = parse_term(p);
    for (;;) {
        skip_ws(p);
        char c = p->s[p->pos];
        if (c != '+' && c != '-') break;
        p->pos++;

        Frac b = parse_term(p);
        if (!p->ok || !fr_ok(a) || !fr_ok(b)) { p->ok = 0; return fr_bad(); }

        if (fr_is(a, 0) || fr_is(b, 0)) p->pad = 1;
        a = (c == '+') ? fr_add(a, b) : fr_sub(a, b);
        if (!fr_ok(a)) { p->ok = 0; return fr_bad(); }
        if (!fr_is_int(a)) p->frac = 1;
    }
    return a;
}

typedef struct { int is9, has_muldiv, pad, frac; } ExprInfo;

static ExprInfo eval_expr(const char *s) {
    Parse p;
    ExprInfo r;
    p.s = s; p.pos = 0; p.ok = 1;
    p.has_muldiv = 0; p.pad = 0; p.frac = 0;

    Frac v = parse_expr(&p);
    skip_ws(&p);
    if (p.s[p.pos] != '\0') p.ok = 0;     /* 有剩余字符 -> 式子不合法 */

    r.is9        = (p.ok && fr_is(v, TARGET));
    r.has_muldiv = p.has_muldiv;
    r.pad        = p.pad;
    r.frac       = p.frac;
    return r;
}

/* ============ 纯加减能否得到 9（等价于 T 栏的判定） ============ */
static int can_addsub_only(int a, int b, int c, int d) {
    int v[4];
    v[0] = a; v[1] = b; v[2] = c; v[3] = d;
    for (int m = 0; m < 16; m++) {
        int s = 0;
        for (int i = 0; i < 4; i++) s += ((m >> i) & 1) ? -v[i] : v[i];
        if (s == TARGET) return 1;
    }
    return 0;
}

/* ============================== 工具 ============================== */
static void strip_bom(char *s) {
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB &&
        (unsigned char)s[2] == 0xBF)
        memmove(s, s + 3, strlen(s + 3) + 1);
}

/* ============================== main ============================== */
int main(int argc, char **argv) {
    const char *in_name  = (argc > 1) ? argv[1] : "solutions_filtered.csv";
    const char *out_name = (argc > 2) ? argv[2] : "solutions_reeval.csv";

    FILE *fin = fopen(in_name, "r");
    if (!fin) { printf("打不开输入文件：%s\n", in_name); return 1; }
    FILE *fout = fopen(out_name, "w");
    if (!fout) { printf("无法创建输出文件：%s\n", out_name); fclose(fin); return 1; }

    /* 表头：前 6 列和原文件一致（算式保留！），后面全是新算出来的评价列 */
    fprintf(fout, "num1,num2,num3,num4,count,expressions,"
                  "valid,addsub,T,pad,frac,has9,min_num,max_num,sum,diff,level\n");

    static char line[MAX_LINE];
    static char ebuf[MAX_EXPR];

    int first_line = 1;
    int rows = 0;                 /* 处理的数据行数 */
    int bad_lines = 0;            /* 解析失败被跳过的行 */
    int count_mismatch = 0;       /* count 列 != 算式实际条数 */
    int rows_with_invalid = 0;    /* 存在"算出来不等于 9"的算式 */
    int invalid_expr_total = 0;
    int t_rows = 0;               /* T == 1 的行数 */
    int addsub_rows = 0;          /* 至少有一条纯加减算式的行数 */
    int t_inconsistent = 0;       /* T 与算式列互相矛盾的行数 */
    int pad_all_rows = 0;         /* 所有解都是注水解的行数 */
    int lv_cnt[4] = {0, 0, 0, 0};

    while (fgets(line, sizeof(line), fin)) {
        if (first_line) { strip_bom(line); first_line = 0; }

        size_t L = strlen(line);
        while (L > 0 && (line[L - 1] == '\n' || line[L - 1] == '\r')) line[--L] = '\0';
        if (L == 0) continue;
        if (strncmp(line, "num1,", 5) == 0) continue;    /* 跳过表头 */

        int a, b, c, d, cf;
        if (sscanf(line, "%d,%d,%d,%d,%d", &a, &b, &c, &d, &cf) != 5) {
            bad_lines++;
            continue;
        }

        /* 定位第 6 列 expressions：第 5 个逗号之后，第 6 个逗号之前 */
        char *p = line, *c5 = NULL, *c6 = NULL;
        int nc = 0;
        for (; *p; p++) {
            if (*p == ',') {
                nc++;
                if (nc == 5) c5 = p;
                else if (nc == 6) { c6 = p; break; }
            }
        }
        if (!c5) { bad_lines++; continue; }

        char *e_start = c5 + 1;
        char *e_end   = c6 ? c6 : (line + L);
        if (e_end < e_start) e_end = e_start;

        int elen = (int)(e_end - e_start);
        if (elen > MAX_EXPR - 1) { elen = MAX_EXPR - 1; e_end = e_start + elen; }

        memcpy(ebuf, e_start, (size_t)elen);
        ebuf[elen] = '\0';

        /* 按 ';' 拆开，逐条求值 */
        int n_expr = 0, valid = 0, addsub_cnt = 0, pad_cnt = 0, frac_cnt = 0;
        char *tok = ebuf;
        while (*tok) {
            char *semi = strchr(tok, ';');
            if (semi) *semi = '\0';

            char *ts = tok;
            while (*ts == ' ' || *ts == '\t') ts++;
            size_t tl = strlen(ts);
            while (tl > 0 && (ts[tl - 1] == ' ' || ts[tl - 1] == '\t')) ts[--tl] = '\0';

            if (*ts) {
                ExprInfo info = eval_expr(ts);
                n_expr++;
                if (info.is9) {
                    valid++;
                    if (!info.has_muldiv) addsub_cnt++;
                    if (info.pad)  pad_cnt++;
                    if (info.frac) frac_cnt++;
                }
            }
            if (!semi) break;
            tok = semi + 1;
        }

        /* ---- 重新标记 T：只用 + - 能不能得到 9 ---- */
        int T = can_addsub_only(a, b, c, d);
        if (T) t_rows++;
        if (addsub_cnt > 0) addsub_rows++;
        if (T != (addsub_cnt > 0)) t_inconsistent++;

        if (cf != n_expr) count_mismatch++;
        if (valid != n_expr) {
            rows_with_invalid++;
            invalid_expr_total += (n_expr - valid);
        }
        if (valid > 0 && pad_cnt == valid) pad_all_rows++;

        /* ---- 难度分 ---- */
        double cnt  = (double)(valid > 0 ? valid : n_expr);
        double mean = (a + b + c + d) / 4.0;

        double ratio = log(cnt + 1.0) / log(MAX_COUNT);   /* 0=解数1，1=解数达到上限 */
        if (ratio < 0.0) ratio = 0.0;
        if (ratio > 1.0) ratio = 1.0;

        double num_ratio = (18.0 - mean) / 17.0;          /* 1=全是小数，0=全是 18 */
        if (num_ratio < 0.0) num_ratio = 0.0;
        if (num_ratio > 1.0) num_ratio = 1.0;

        double sol_part  = W_SOL  * (1.0 - ratio);
        double num_part  = W_NUM  * num_ratio;
        double frac_part = (cnt > 0.0) ? (W_FRAC * ((double)frac_cnt / cnt)) : 0.0;

        double diff = sol_part + num_part + frac_part;
        if (T) diff -= PEN_T;
        if (valid > 0 && pad_cnt == valid) diff -= PEN_PAD;
        if (diff < 0.0)  diff = 0.0;
        if (diff > 10.0) diff = 10.0;
        diff = floor(diff * 10.0 + 0.5) / 10.0;   /* 保留 1 位小数 */

        int li = (diff >= LV_EXTREME) ? 3 : (diff >= LV_HARD) ? 2
               : (diff >= LV_MID)     ? 1 : 0;
        const char *level = (li == 3) ? "extreme" : (li == 2) ? "hard"
                          : (li == 1) ? "mid"     : "easy";
        lv_cnt[li]++;

        int has9 = (a == 9 || b == 9 || c == 9 || d == 9);

        fprintf(fout, "%d,%d,%d,%d,%d,%.*s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.1f,%s\n",
                a, b, c, d, cf, elen, e_start,
                valid, addsub_cnt, T, pad_cnt, frac_cnt, has9,
                a < b ? (a < c ? (a < d ? a : d) : (c < d ? c : d))
                      : (b < c ? (b < d ? b : d) : (c < d ? c : d)),
                a > b ? (a > c ? (a > d ? a : d) : (c > d ? c : d))
                      : (b > c ? (b > d ? b : d) : (c > d ? c : d)),
                a + b + c + d, diff, level);

        rows++;
    }

    fclose(fin);
    fclose(fout);

    printf("========================= 重新评价完成 =========================\n");
    printf("输入文件        : %s\n", in_name);
    printf("输出文件        : %s\n", out_name);
    printf("处理数据行数    : %d\n", rows);
    printf("无法解析的行    : %d\n", bad_lines);
    printf("---------------------------------------------------------------\n");
    printf("count 列与算式条数不一致的行 : %d\n", count_mismatch);
    printf("含有\"算出来不等于9\"的行       : %d （共 %d 条算式）\n",
           rows_with_invalid, invalid_expr_total);
    printf("T=1（纯加减就能拼9）的行      : %d\n", t_rows);
    printf("算式里确有纯加减解的行        : %d\n", addsub_rows);
    printf("以上两者互相矛盾的行          : %d （正常应为 0；不为 0 说明数据有异常）\n",
           t_inconsistent);
    printf("全部解都是注水解的行          : %d\n", pad_all_rows);
    printf("---------------------------------------------------------------\n");
    printf("难度分布  easy=%d  mid=%d  hard=%d  extreme=%d\n",
           lv_cnt[0], lv_cnt[1], lv_cnt[2], lv_cnt[3]);
    printf("---------------------------------------------------------------\n");
    printf("输出列说明：\n");
    printf("  count    原文件的解数（保留不动）\n");
    printf("  expressions 原文件的全部算式（保留，未删除）\n");
    printf("  valid    逐条精确求值后真正 == 9 的算式条数\n");
    printf("  addsub   其中只用 + - 的算式条数\n");
    printf("  T        1 = 这 4 个数只用 + - 就能得到 9（纯加减送分题）；0 = 不能\n");
    printf("  pad      含 x0 x1 /1 +0 -0 之类注水操作的算式条数\n");
    printf("  frac     中间结果出现分数的算式条数（要通分的难题）\n");
    printf("  has9     4 个数里本来就有 9\n");
    printf("  min_num/max_num/sum  四数的最小值 / 最大值 / 和\n");
    printf("  diff     难度分 0~10（越大越难）：解少、数小、需要分数 -> 大；\n");
    printf("           纯加减可解、解全是注水 -> 小\n");
    printf("  level    easy(<3.0) / mid(3.0~) / hard(5.5~) / extreme(>=7.5)\n");
    printf("===============================================================\n");

    return 0;
}
