/* ============================================================================
 *  pair9.c  ——  两组（共 8 个数）拼两个 9 的【查表分析 + 评分】
 *
 *  它做的事：
 *    给你 8 个数（通常是从 solutions_reeval.csv 里挑的两行 4 数组合），
 *    程序把这 8 个数【重新打乱分组】，枚举所有 "4 + 4" 的拆法（去重后最多 35 种），
 *    每一半都拿去查 solutions.csv 字典，看这一半能不能拼出 9：
 *        · part_total  一共有多少种 4+4 拆法（按数字多重集合去重）
 *        · part_valid  其中两边都有解的拆法数 P
 *        · ways        总解法对数 = Σ (左半解数 × 右半解数)
 *        · ways_clean  同上，但只用"不注水"的解（排除 ×0 ×1 ÷1 +0 -0 那种）
 *    然后按这些量给这道 8 数题打分（diff / level）。
 *
 *  用法：
 *    pair9.exe 1 1 3 14 4 5 6 10            直接给 8 个数
 *    pair9.exe 1,1,3,14 4,5,6,10            两组用逗号写也行
 *    pair9.exe -r 12 40                      从 solutions_reeval.csv 取第 12 行和第 40 行
 *    pair9.exe -r 12 40 12 55 3 7            一次分析多对（两两成对给行号）
 *    pair9.exe -r 12 40 -t other.csv         指定取行的表文件（默认 solutions_reeval.csv）
 *    pair9.exe -f pairs.txt                  批量：文件每行 8 个数（空格/逗号分隔，# 开头为注释）
 *    pair9.exe -d solutions.csv 1 1 8 8 1 1 10 14   指定查表字典（默认 solutions.csv）
 *    -q                                      安静模式：只写 CSV，不打印报告
 *
 *  输出：
 *    pair9_summary.csv   每道 8 数题一行：P、ways、H、diff、level、原配分组是否有解…
 *    pair9_detail.csv    每道题的每个 4+4 分组一行：两边解数、干净解数、示例算式
 *
 *  编译：gcc pair9.c -o pair9 -lm
 *
 *  注意：字典 solutions.csv 是 solutions.c 生成的，里面【没有 13 和 17】，
 *        所以 8 个数里若含 13/17，结果会不准（程序会提示）。
 *        count>=100 的组在 solutions_filtered.csv 里被删了，所以查表请用
 *        完整的 solutions.csv（默认就是它）。
 * ==========================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define TARGET      9
#define MAX_LINE    65536
#define KMAX        130321        /* 19^4：数字 1~18 的排序后 4 元组一一对应一个下标 */
#define MAX_PART    40            /* 8 个数 4+4 拆法最多 35 种 */

/* ---- 4 数组难度权重：必须和 reevaluate.c 保持一致 ---- */
#define W_SOL       5.0
#define W_NUM       2.5
#define W_FRAC      2.5
#define PEN_T       2.5
#define PEN_PAD     2.0
#define MAX_COUNT  100.0
#define LV_EXTREME  7.5
#define LV_HARD     5.5
#define LV_MID      3.0

/* ---- 8 数组（两组）评分权重：想改口味改这里 ---- */
#define P8_HALF     0.45   /* 半组平均难度（0~10）占 45%  -> 最多 4.5 分 */
#define P8_W        3.5    /* 总解法对数越少越难           -> 最多 3.5 分 */
#define P8_P        2.0    /* 有效拆法越少越难（越"唯一"） -> 最多 2.0 分 */
#define W_REF     300.0    /* 总解法对数的归一化上限 */
#define P_REF      24.0    /* 有效拆法数的归一化上限 */

/* ============================ 分数（有理数） ============================ */
typedef struct { long long n, d; } Frac;   /* d==0 表示无效（除零/语法错） */

static long long ll_abs(long long x) { return x < 0 ? -x : x; }

static long long ll_gcd(long long x, long long y) {
    x = ll_abs(x); y = ll_abs(y);
    while (y) { long long t = x % y; x = y; y = t; }
    return x;
}

static Frac fr_make(long long n, long long d) {
    Frac f;
    if (d == 0) { f.n = 0; f.d = 0; return f; }
    if (d < 0)  { n = -n; d = -d; }
    long long g = ll_gcd(n, d);
    if (g > 1) { n /= g; d /= g; }
    f.n = n; f.d = d;
    return f;
}

static Frac fr_bad(void) { Frac f; f.n = 0; f.d = 0; return f; }
static int  fr_ok(Frac f) { return f.d != 0; }
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
    int pos, ok, has_muldiv, pad, frac;
} Parse;

static void skip_ws(Parse *p) {
    while (p->s[p->pos] == ' ' || p->s[p->pos] == '\t') p->pos++;
}

static Frac parse_expr(Parse *p);

static Frac parse_factor(Parse *p) {
    skip_ws(p);
    char c = p->s[p->pos];

    if (c == '+') { p->pos++; return parse_factor(p); }
    if (c == '-') {
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
            if (b.n == 0) { p->ok = 0; return fr_bad(); }
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
    if (p.s[p.pos] != '\0') p.ok = 0;

    r.is9        = (p.ok && fr_is(v, TARGET));
    r.has_muldiv = p.has_muldiv;
    r.pad        = p.pad;
    r.frac       = p.frac;
    return r;
}

/* ============ 纯加减能否得到 9（和 reevaluate.c 的 T 判定一致） ============ */
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

/* ===================== 4 数组难度分（与 reevaluate.c 同一公式） ===================== */
static double clamp01(double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }

static double diff4_of(int valid, int n_expr, int T, int pad, int frac, const int n[4]) {
    double cnt  = (double)(valid > 0 ? valid : n_expr);
    double mean = (n[0] + n[1] + n[2] + n[3]) / 4.0;

    double ratio     = clamp01(log(cnt + 1.0) / log(MAX_COUNT));
    double num_ratio = clamp01((18.0 - mean) / 17.0);

    double d = W_SOL * (1.0 - ratio) + W_NUM * num_ratio;
    if (cnt > 0.0) d += W_FRAC * ((double)frac / cnt);
    if (T) d -= PEN_T;
    if (valid > 0 && pad == valid) d -= PEN_PAD;
    if (d < 0.0) d = 0.0;
    if (d > 10.0) d = 10.0;
    return floor(d * 10.0 + 0.5) / 10.0;
}

static const char *level_of(double d) {
    if (d >= LV_EXTREME) return "extreme";
    if (d >= LV_HARD)    return "hard";
    if (d >= LV_MID)     return "mid";
    return "easy";
}

/* ============================== 小工具 ============================== */
static void strip_eol(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = '\0';
}

static void strip_bom(char *s) {
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB &&
        (unsigned char)s[2] == 0xBF)
        memmove(s, s + 3, strlen(s + 3) + 1);
}

static void app(char *dst, int cap, const char *src, int len) {
    int cur = (int)strlen(dst);
    if (cur >= cap - 1) return;
    if (len > cap - 1 - cur) len = cap - 1 - cur;
    if (len <= 0) return;
    memcpy(dst + cur, src, (size_t)len);
    dst[cur + len] = '\0';
}

static void sort4(int *x) {
    for (int i = 1; i < 4; i++) {
        int t = x[i], j = i - 1;
        while (j >= 0 && x[j] > t) { x[j + 1] = x[j]; j--; }
        x[j + 1] = t;
    }
}

static int cmp4(const int *a, const int *b) {
    for (int i = 0; i < 4; i++)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

static void swap4(int *x, int *y) {
    for (int i = 0; i < 4; i++) { int t = x[i]; x[i] = y[i]; y[i] = t; }
}

static int pop4(int m) { int c = 0; while (m) { c += m & 1; m >>= 1; } return c; }

/* ============================== 字典 ============================== */
typedef struct {
    int n[4];
    int count_file;      /* 文件里的 count */
    char *expr;          /* malloc 的算式串（分号分隔） */
    int done;            /* 指标是否已算过 */
    int n_expr, valid, addsub, pad, frac, T;
    double diff;         /* 4 数组难度分 */
} Entry;

static Entry **ents = NULL;
static int n_ent = 0, cap_ent = 0;
static int dict_idx[KMAX];    /* 0 = 无，否则 ents[i-1] */

static int key4(const int *x) { return ((x[0] * 19 + x[1]) * 19 + x[2]) * 19 + x[3]; }

static Entry *lookup4(const int *x) {
    int i = dict_idx[key4(x)];
    return i ? ents[i - 1] : NULL;
}

static int add_entry(int a, int b, int c, int d, int cf, char *expr) {
    int x[4];
    x[0] = a; x[1] = b; x[2] = c; x[3] = d;
    sort4(x);
    for (int i = 0; i < 4; i++)                 /* key4 只支持 0~18，越界就丢弃 */
        if (x[i] < 0 || x[i] > 18) return 0;
    int k = key4(x);
    if (dict_idx[k]) return 0;                 /* 已有，保留第一条 */

    if (n_ent == cap_ent) {
        int nc = cap_ent ? cap_ent * 2 : 4096;
        Entry **ne = (Entry **)realloc(ents, (size_t)nc * sizeof(Entry *));
        if (!ne) return 0;
        ents = ne; cap_ent = nc;
    }
    Entry *e = (Entry *)malloc(sizeof(Entry));
    if (!e) return 0;

    e->n[0] = x[0]; e->n[1] = x[1]; e->n[2] = x[2]; e->n[3] = x[3];
    e->count_file = cf;
    e->expr = expr;
    e->done = 0;
    e->n_expr = e->valid = e->addsub = e->pad = e->frac = 0;
    e->T = 0; e->diff = 0.0;

    ents[n_ent] = e;
    n_ent++;
    dict_idx[k] = n_ent;                       /* 存 index+1 */
    return 1;
}

static void ensure_metrics(Entry *e) {
    if (e->done) return;
    e->done = 1;

    char *buf = (char *)malloc(strlen(e->expr) + 1);
    if (!buf) return;
    strcpy(buf, e->expr);

    int n_expr = 0, valid = 0, addsub = 0, pad = 0, frac = 0;
    char *tok = buf;
    while (*tok) {
        char *semi = strchr(tok, ';');
        if (semi) *semi = '\0';

        char *ts = tok;
        while (*ts == ' ' || *ts == '\t') ts++;
        size_t tl = strlen(ts);
        while (tl > 0 && (ts[tl - 1] == ' ' || ts[tl - 1] == '\t')) ts[--tl] = '\0';

        if (*ts) {
            ExprInfo in = eval_expr(ts);
            n_expr++;
            if (in.is9) {
                valid++;
                if (!in.has_muldiv) addsub++;
                if (in.pad)  pad++;
                if (in.frac) frac++;
            }
        }
        if (!semi) break;
        tok = semi + 1;
    }
    free(buf);

    e->n_expr = n_expr; e->valid = valid; e->addsub = addsub;
    e->pad = pad; e->frac = frac;
    e->T = can_addsub_only(e->n[0], e->n[1], e->n[2], e->n[3]);
    e->diff = diff4_of(valid, n_expr, e->T, pad, frac, e->n);
}

static int load_dict(const char *fname) {
    FILE *f = fopen(fname, "r");
    if (!f) { printf("打不开字典文件：%s\n", fname); return 0; }

    static char line[MAX_LINE];
    int first = 1, rows = 0, dup = 0;

    while (fgets(line, sizeof(line), f)) {
        if (first) { strip_bom(line); first = 0; }
        strip_eol(line);
        if (!line[0]) continue;
        if (strncmp(line, "num1,", 5) == 0) continue;      /* 表头 */

        int a, b, c, d, cf;
        if (sscanf(line, "%d,%d,%d,%d,%d", &a, &b, &c, &d, &cf) != 5) continue;

        char *p = line, *c5 = NULL, *c6 = NULL;
        int nc = 0;
        for (; *p; p++) {
            if (*p == ',') {
                nc++;
                if (nc == 5) c5 = p;
                else if (nc == 6) { c6 = p; break; }
            }
        }
        if (!c5) continue;

        const char *es = c5 + 1;
        const char *ee = c6 ? c6 : (line + strlen(line));
        int elen = (int)(ee - es);
        if (elen < 0) elen = 0;

        char *buf = (char *)malloc((size_t)elen + 1);
        if (!buf) { fclose(f); return 0; }
        memcpy(buf, es, (size_t)elen);
        buf[elen] = '\0';

        if (add_entry(a, b, c, d, cf, buf)) rows++;
        else { free(buf); dup++; }
    }
    fclose(f);
    printf("字典 %s：载入 %d 组（跳过重复 %d 行）\n", fname, rows, dup);
    return 1;
}

/* ==================== 从算式串里取前 n 条示例 ==================== */
static void collect_samples(const char *expr, int clean_only, int n,
                            char *out, int cap) {
    out[0] = '\0';
    int got = 0;
    const char *p = expr;
    while (*p && got < n) {
        const char *semi = strchr(p, ';');
        int len = semi ? (int)(semi - p) : (int)strlen(p);
        char tmp[512];
        int tl = len < (int)sizeof(tmp) - 1 ? len : (int)sizeof(tmp) - 1;
        memcpy(tmp, p, (size_t)tl);
        tmp[tl] = '\0';

        if (tmp[0]) {
            int take = 0;
            if (!clean_only) take = 1;
            else { ExprInfo in = eval_expr(tmp); if (in.is9 && !in.pad) take = 1; }
            if (take) {
                if (got) app(out, cap, ";", 1);
                app(out, cap, tmp, (int)strlen(tmp));
                got++;
            }
        }
        if (!semi) break;
        p = semi + 1;
    }
}

static void samples_of(Entry *e, int n, char *out, int cap) {
    collect_samples(e->expr, 1, n, out, cap);      /* 先找不注水的漂亮解 */
    if (!out[0]) collect_samples(e->expr, 0, n, out, cap);
    if (!out[0]) app(out, cap, "(无)", 4);
}

/* ====================== 8 个数的 4+4 分组分析 ====================== */
typedef struct {
    int ok;                 /* 两边都有解 */
    int A[4], B[4];
    Entry *ea, *eb;
    int ca, cb;             /* 两边解数 */
    int cla, clb;           /* 两边"干净"解数 */
    long long ways, ways_clean;
} Part;

static int analyze8(const int v[8], Part *res) {
    int np = 0;
    for (int m = 0; m < 256; m++) {
        if (pop4(m) != 4) continue;

        int A[4], B[4];
        int na = 0, nb = 0;
        for (int i = 0; i < 8; i++) {
            if ((m >> i) & 1) A[na++] = v[i];
            else              B[nb++] = v[i];
        }
        sort4(A); sort4(B);
        if (cmp4(A, B) > 0) swap4(A, B);           /* 规范序，避免 (A|B) 与 (B|A) 重复 */

        int dup = 0;
        for (int k = 0; k < np; k++)
            if (cmp4(res[k].A, A) == 0 && cmp4(res[k].B, B) == 0) { dup = 1; break; }
        if (dup) continue;

        Part *p = &res[np];
        for (int i = 0; i < 4; i++) { p->A[i] = A[i]; p->B[i] = B[i]; }

        p->ea = lookup4(A);
        p->eb = lookup4(B);
        if (p->ea) ensure_metrics(p->ea);
        if (p->eb) ensure_metrics(p->eb);

        p->ca  = p->ea ? p->ea->valid : 0;
        p->cb  = p->eb ? p->eb->valid : 0;
        p->cla = p->ea ? (p->ea->valid - p->ea->pad) : 0;
        p->clb = p->eb ? (p->eb->valid - p->eb->pad) : 0;
        if (p->cla < 0) p->cla = 0;
        if (p->clb < 0) p->clb = 0;

        p->ok         = (p->ca > 0 && p->cb > 0);
        p->ways       = p->ok ? (long long)p->ca * (long long)p->cb : 0;
        p->ways_clean = p->ok ? (long long)p->cla * (long long)p->clb : 0;
        np++;
    }
    return np;
}

static int find_part(const Part *res, int np, const int *A, const int *B) {
    for (int i = 0; i < np; i++)
        if ((cmp4(res[i].A, A) == 0 && cmp4(res[i].B, B) == 0) ||
            (cmp4(res[i].A, B) == 0 && cmp4(res[i].B, A) == 0)) return i;
    return -1;
}

/* 排序用：有解优先，其次 ways 大，再按字典序 */
static int part_better(const Part *a, const Part *b) {
    if (a->ok != b->ok) return a->ok > b->ok;
    if (a->ways != b->ways) return a->ways > b->ways;
    return cmp4(a->A, b->A) < 0;
}

/* ====================== 分析一道 8 数题 + 写 CSV ====================== */
static void analyze_one(const int v[8], const char *pid,
                        FILE *fsum, FILE *fdet, int quiet) {
    for (int i = 0; i < 8; i++) {
        if (v[i] < 1 || v[i] > 18) {
            printf("[%s] 数字 %d 超出字典范围(1~18)，跳过\n", pid, v[i]);
            return;
        }
    }
    int has1317 = 0;
    for (int i = 0; i < 8; i++) if (v[i] == 13 || v[i] == 17) has1317 = 1;

    Part parts[MAX_PART];
    int np = analyze8(v, parts);

    int npv = 0;
    long long W = 0, WC = 0;
    double Hsum = 0.0;
    for (int i = 0; i < np; i++) {
        if (!parts[i].ok) continue;
        npv++;
        W  += parts[i].ways;
        WC += parts[i].ways_clean;
        Hsum += ((parts[i].ea->diff + parts[i].eb->diff) / 2.0) * (double)parts[i].ways;
    }
    double H = (W > 0) ? (Hsum / (double)W) : 0.0;

    double diff8;
    const char *lv;
    if (npv == 0) {                    /* 怎么拆都拼不出两个 9：不是"难"，是废题 */
        diff8 = 0.0;
        lv = "none";
    } else {
        double fW = 1.0 - clamp01(log((double)W + 1.0) / log(W_REF));
        double fP = 1.0 - clamp01(log((double)npv + 1.0) / log(P_REF));
        diff8 = P8_HALF * H + P8_W * fW + P8_P * fP;
        if (diff8 < 0.0) diff8 = 0.0;
        if (diff8 > 10.0) diff8 = 10.0;
        diff8 = floor(diff8 * 10.0 + 0.5) / 10.0;
        lv = level_of(diff8);
    }

    /* 原配分组（前 4 个 | 后 4 个）是否有解 */
    int A[4], B[4];
    for (int i = 0; i < 4; i++) { A[i] = v[i]; B[i] = v[4 + i]; }
    sort4(A); sort4(B);
    int oi = find_part(parts, np, A, B);
    int orig_ok = (oi >= 0 && parts[oi].ok) ? 1 : 0;
    long long orig_ways  = orig_ok ? parts[oi].ways : 0;
    long long orig_clean = orig_ok ? parts[oi].ways_clean : 0;

    /* ---------------- 控制台报告 ---------------- */
    if (!quiet) {
        printf("\n================ 8 个数拼两个 9 ================\n");
        printf("题目：%d %d %d %d | %d %d %d %d   (id=%s)\n",
               v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], pid);
        printf("4+4 拆法(去重)：%d 种；两边都有解：%d 种\n", np, npv);
        printf("总解法对数 ways = %lld   干净解法对数 = %lld\n", W, WC);
        printf("原配分组 %d %d %d %d | %d %d %d %d : %s",
               v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7],
               orig_ok ? "有解" : "无解");
        if (orig_ok) printf("（%lld 对解法，干净 %lld 对）", orig_ways, orig_clean);
        printf("\n");
        if (has1317) printf("!! 含 13/17：字典里没有含这两数的组合，结果会偏少\n");
        printf("评分：半组平均难度 H=%.2f  有效拆法 %d  总解法 %lld  =>  diff=%.1f  level=%s\n",
               H, npv, W, diff8, lv);
        printf("---- 分组明细 ----\n");

        int order[MAX_PART];
        for (int i = 0; i < np; i++) order[i] = i;
        for (int i = 1; i < np; i++) {                 /* 插入排序 */
            int cur = order[i], j = i - 1;
            while (j >= 0 && part_better(&parts[cur], &parts[order[j]])) {
                order[j + 1] = order[j]; j--;
            }
            order[j + 1] = cur;
        }

        int shown = 0;
        for (int i = 0; i < np; i++) {
            Part *p = &parts[order[i]];
            if (p->ok) {
                printf("[有解] %d %d %d %d | %d %d %d %d   解数 %d x %d = %lld   干净 %d x %d = %lld\n",
                       p->A[0], p->A[1], p->A[2], p->A[3],
                       p->B[0], p->B[1], p->B[2], p->B[3],
                       p->ca, p->cb, p->ways, p->cla, p->clb, p->ways_clean);
                if (shown < 6) {
                    char sa[256], sb[256];
                    samples_of(p->ea, 2, sa, (int)sizeof(sa));
                    samples_of(p->eb, 2, sb, (int)sizeof(sb));
                    printf("        左：%s\n        右：%s\n", sa, sb);
                    shown++;
                }
            } else {
                printf("[无解] %d %d %d %d | %d %d %d %d   (%s拼不出 9)\n",
                       p->A[0], p->A[1], p->A[2], p->A[3],
                       p->B[0], p->B[1], p->B[2], p->B[3],
                       (p->ca == 0 && p->cb == 0) ? "两边都" :
                       (p->ca == 0 ? "左边" : "右边"));
            }
        }
        if (npv == 0) printf("结论：这 8 个数无论怎么分成 4+4，都拼不出两个 9 —— 不要用。\n");
    }

    /* ---------------- 写 CSV ---------------- */
    fprintf(fsum, "%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%lld,%lld,%.2f,%.1f,%s,%d,%lld,%lld\n",
            pid, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7],
            np, npv, W, WC, H, diff8, lv, orig_ok, orig_ways, orig_clean);

    for (int i = 0; i < np; i++) {                     /* 明细按 A/B 自然顺序写 */
        Part *p = &parts[i];
        char sa[256] = "", sb[256] = "";
        if (p->ok) {
            samples_of(p->ea, 2, sa, (int)sizeof(sa));
            samples_of(p->eb, 2, sb, (int)sizeof(sb));
        }
        fprintf(fdet, "%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%lld,%lld,%s,%s\n",
                pid, p->ok ? 1 : 0,
                p->A[0], p->A[1], p->A[2], p->A[3],
                p->B[0], p->B[1], p->B[2], p->B[3],
                p->ca, p->cla, p->cb, p->clb, p->ways, p->ways_clean, sa, sb);
    }
}

/* ==================== 从表里取指定行的 4 个数 ==================== */
static int fetch_rows(const char *fname, const int *want, int nwant, int got[][4]) {
    FILE *f = fopen(fname, "r");
    if (!f) return -1;

    static char line[MAX_LINE];
    int first = 1, idx = 0;
    for (int i = 0; i < nwant; i++) got[i][0] = 0;

    while (fgets(line, sizeof(line), f)) {
        if (first) { strip_bom(line); first = 0; }
        strip_eol(line);
        if (!line[0]) continue;
        if (strncmp(line, "num1,", 5) == 0) continue;

        idx++;
        for (int i = 0; i < nwant; i++) {
            if (want[i] != idx) continue;
            int a, b, c, d;
            if (sscanf(line, "%d,%d,%d,%d", &a, &b, &c, &d) == 4) {
                got[i][0] = a; got[i][1] = b; got[i][2] = c; got[i][3] = d;
            }
        }
    }
    fclose(f);
    return idx;
}

static int parse_nums(const char *s, int *out, int maxn) {
    int n = 0;
    while (*s && n < maxn) {
        while (*s == ' ' || *s == '\t' || *s == ',' || *s == ';' || *s == '|') s++;
        if (!*s) break;
        char *end;
        long v = strtol(s, &end, 10);
        if (end == s) break;
        out[n++] = (int)v;
        s = end;
    }
    return n;
}

static void usage(const char *exe) {
    printf("用法：\n");
    printf("  %s 1 1 3 14 4 5 6 10          直接给 8 个数（也可写 1,1,3,14 4,5,6,10）\n", exe);
    printf("  %s -r 12 40                  从 solutions_reeval.csv 取第 12 行和第 40 行\n", exe);
    printf("  %s -r 12 40 12 55            一次分析多对（行号两两成对）\n", exe);
    printf("  %s -f pairs.txt              批量：每行 8 个数\n", exe);
    printf("选项：\n");
    printf("  -d <字典.csv>   查表字典（默认 solutions.csv）\n");
    printf("  -t <表.csv>     -r 模式取行的表（默认 solutions_reeval.csv）\n");
    printf("  -q              安静模式，只写 CSV\n");
    printf("输出：pair9_summary.csv / pair9_detail.csv\n");
}

/* ============================== main ============================== */
int main(int argc, char **argv) {
    const char *dict_name  = "solutions.csv";
    const char *table_name = "solutions_reeval.csv";
    const char *batch_name = NULL;
    int quiet = 0, mode_r = 0;

    int rows[256];
    int n_rows = 0;
    int nums[8];
    int have = 0;

    if (argc < 2) {
        printf("没有命令行参数：下面是对照用法，程序随后会进入键盘输入模式。\n");
        usage(argv[0]);
    }

    for (int k = 1; k < argc; k++) {
        const char *a = argv[k];
        if (!strcmp(a, "-d") && k + 1 < argc)      dict_name  = argv[++k];
        else if (!strcmp(a, "-t") && k + 1 < argc) table_name = argv[++k];
        else if (!strcmp(a, "-f") && k + 1 < argc) batch_name = argv[++k];
        else if (!strcmp(a, "-q"))                 quiet = 1;
        else if (!strcmp(a, "-r"))                 mode_r = 1;
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(argv[0]); return 0; }
        else if (a[0] == '-' && a[1] >= 'a' && a[1] <= 'z') {
            printf("未知选项：%s\n", a);
            usage(argv[0]);
            return 1;
        } else if (mode_r) {
            if (n_rows < 256) rows[n_rows++] = atoi(a);
        } else {
            have += parse_nums(a, nums + have, 8 - have);
        }
    }

    /* 输出文件先建好：哪怕后面字典读不到，文件也已经存在（里面是表头） */
    FILE *fsum = fopen("pair9_summary.csv", "w");
    FILE *fdet = fopen("pair9_detail.csv", "w");
    if (!fsum || !fdet) {
        printf("无法创建输出文件 pair9_summary.csv / pair9_detail.csv\n");
        return 1;
    }
    fprintf(fsum, "puzzle_id,n1,n2,n3,n4,n5,n6,n7,n8,"
                  "part_total,part_valid,ways,ways_clean,avg_half_diff,diff,level,"
                  "orig_ok,orig_ways,orig_clean_ways\n");
    fprintf(fdet, "puzzle_id,ok,A1,A2,A3,A4,B1,B2,B3,B4,"
                  "countA,cleanA,countB,cleanB,ways,ways_clean,sampleA,sampleB\n");
    fflush(fsum);
    fflush(fdet);

    if (!load_dict(dict_name)) {
        fclose(fsum);
        fclose(fdet);
        printf("字典没读到，两个 CSV 里只有表头。检查 solutions.csv 是否和 exe 在同一目录。\n");
        return 1;
    }

    if (mode_r) {
        if (n_rows < 2 || (n_rows % 2) != 0) {
            printf("-r 需要成对的行号（至少 2 个，偶数个）\n");
        } else {
            int got[256][4];
            int total = fetch_rows(table_name, rows, n_rows, got);
            if (total < 0) {
                printf("打不开表文件：%s\n", table_name);
            } else {
                printf("表 %s：共 %d 个数据行（数据行 1 = 文件第 2 行，表头占第 1 行）\n",
                       table_name, total);
                for (int i = 0; i + 1 < n_rows; i += 2) {
                    if (got[i][0] == 0 || got[i + 1][0] == 0) {
                        printf("行号 %d 或 %d 超出范围，跳过\n", rows[i], rows[i + 1]);
                        continue;
                    }
                    int v[8];
                    memcpy(v,     got[i],     4 * sizeof(int));
                    memcpy(v + 4, got[i + 1], 4 * sizeof(int));
                    char pid[64];
                    snprintf(pid, sizeof(pid), "r%d-r%d", rows[i], rows[i + 1]);
                    analyze_one(v, pid, fsum, fdet, quiet);
                }
            }
        }
    } else if (batch_name) {
        FILE *fb = fopen(batch_name, "r");
        if (!fb) {
            printf("打不开批量文件：%s\n", batch_name);
        } else {
            static char bl[MAX_LINE];
            int ln = 0, okcnt = 0, skip = 0;
            while (fgets(bl, sizeof(bl), fb)) {
                ln++;
                strip_eol(bl);
                if (!bl[0] || bl[0] == '#') continue;
                int v[8];
                int got = parse_nums(bl, v, 8);
                if (got != 8) {
                    printf("第 %d 行不是 8 个数（读到 %d 个），跳过\n", ln, got);
                    skip++;
                    continue;
                }
                char pid[32];
                snprintf(pid, sizeof(pid), "L%d", ln);
                analyze_one(v, pid, fsum, fdet, quiet);
                okcnt++;
            }
            fclose(fb);
            printf("批量分析完成：成功 %d 行，跳过 %d 行\n", okcnt, skip);
        }
    } else if (have == 8) {
        analyze_one(nums, "one", fsum, fdet, quiet);
    } else {
        /* 没有参数 / 没凑够 8 个数：从键盘按行读，每行 8 个数 */
        static char ib[MAX_LINE];
        int ln = 0;
        if (have > 0) printf("只读到 %d 个数（需要 8 个），改为键盘输入模式\n", have);
        printf("键盘输入模式：每行 8 个数（空格或逗号分隔），空行结束\n");
        printf("例：1 1 3 14 1 1 4 9\n> ");
        fflush(stdout);
        while (fgets(ib, sizeof(ib), stdin)) {
            int v[8];
            strip_eol(ib);
            if (!ib[0]) break;
            if (parse_nums(ib, v, 8) != 8) {
                printf("这行不是 8 个数，重来\n> ");
                fflush(stdout);
                continue;
            }
            ln++;
            char pid[32];
            snprintf(pid, sizeof(pid), "K%d", ln);
            analyze_one(v, pid, fsum, fdet, quiet);
            printf("> ");
            fflush(stdout);
        }
        if (ln == 0) printf("（没有输入，两个 CSV 只写了表头）\n");
    }

    fclose(fsum);
    fclose(fdet);
    printf("\n已写出 pair9_summary.csv（每题一行的评分）和 pair9_detail.csv（每个 4+4 分组一行）\n");
    return 0;
}
