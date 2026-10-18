/* ============================================================================
 *  nines.c —— 通用「N 个数拼 k 个 9」分析 / 打分 / 档内评级 / 组合生成
 *
 *  支持全部档位（个数 -> 档名）：
 *      4 个  -> normal      (1 组)
 *      8 个  -> hard        (2 组)
 *     12 个  -> veryhard    (3 组)
 *     16 个  -> hardcore    (4 组)
 *     20 个  -> extreme     (5 组)
 *     24 个  -> insane      (6 组)
 *     26 个  -> torment     (6 组 + 剩 2 个不用)
 *   （其它个数也支持：k = N/4，剩 N%4 个不用，档名 custom）
 *
 *  核心算法：把 N 个数看成位掩码，先用 solutions.csv 字典找出所有
 *  「能拼出 9」的 4 数组合（C(N,4) 个），再用【记忆化 DFS + 最低位强制】
 *  精确统计：
 *      sols   能拼出 k 个 9 的【分组方案数】（每组 4 个数，组内无序）
 *      ways   总解法对数 = Σ 各方案 (各组解数之积)
 *      clean  ways 的"干净版"（排掉 ×0 ×1 ÷1 +0 -0 注水解）
 *      d_avg  最省力路线的 k 个半组平均难度（用 reevaluate.c 的 4 数难度分）
 *      d_max  最省力路线里最难的那一个半组的难度分
 *  26 个数时 DFS 允许丢弃 2 个数，所以"剩两个"自动被支持。
 *
 *  难度分（0~100，只在【同一档内部】互相比较）：
 *      score = 45*(d_max/10) + 30*(1 - ln(sols+1)/ln(上限+1)) + 25*(1 - ln(ways+1)/ln(上限+1))
 *      （默认 上限：方案数 200000，解法数 1e12；-fast / -slow / -cap / -call 可调）
 *  批量处理完后，对【同一档的全部题目】按 score 排名给评级：
 *      前 5% = S，5~20% = A，20~50% = B，50~80% = C，后 20% = D
 *      完全无解的题 grade = X（废题）；被截断的题（sols/ways 只是下界，20/24/26 档
 *      或 -fast 时常见）grade = ?，不参与档内排名；含 13/17 无法判定的 grade = N
 *
 *  用法（★最省事：什么都不带，直接运行 nines.exe）：
 *    nines.exe                      无参数：有 pools.txt 就批量跑它；否则若存在
 *                                   solutions_reeval.csv 就【自动依次跑完 7 个档位】，
 *                                   生成 scores_<档名>.csv + pool_<档名>.txt；
 *                                   都没有才进入键盘输入模式（每行一组数）
 *    nines.exe 1 1 3 14 ...         直接把数写在命令行（个数决定档位）
 *    nines.exe -f pool.txt          批量：文件每行一组数（4/8/12/16/20/24/26 个混着也行）
 *    nines.exe -c A.txt B.txt       组合：A、B 两个池（每行一组数）交叉组合成 N 数题再打分
 *    nines.exe -c A.txt B.txt -top 500    每个池只取分数最高的 500 组再组合
 *  选项：
 *    -o out.csv    输出文件（批量默认 nines_scores.csv，单题默认 nines_result.csv）
 *    -d dict.csv   查表字典（默认 solutions.csv）
 *    -max M        最多输出 M 条（默认 100000）
 *    -q            安静模式，不打印每题报告
 *    -col4         批量模式下每行只取开头 4 个数（可以直接吃 solutions_reeval.csv）
 *    -head K       组合模式下每个池直接取前 K 组（池文件按分数排序，比 -top 快）
 *    -turbo        极限粗算（20/24/26 档快速出结果；sols/ways 只是下界，grade 会给 ?）
 *    -fast/-slow   粗算 / 精算；-cap N 方案数上限，-call N 单题搜索步数上限
 *
 *  阶梯式生成（推荐流程，每一步都会写出 pool_<档名>.txt 给下一步用）：
 *    1) nines.exe -f solutions_reeval.csv -col4 -o normal.csv
 *         -> 2850 个 4 数组按 normal 档打分评级，得到 pool_normal.txt
 *    2) nines.exe -c pool_normal.txt pool_normal.txt -top 500 -fast -o hard.csv        (4+4=8)
 *    3) nines.exe -c pool_hard.txt pool_normal.txt -top 500 -fast -o veryhard.csv      (8+4=12)
 *    4) nines.exe -c pool_hard.txt pool_hard.txt -top 500 -fast -o hardcore.csv        (8+8=16)
 *    5) nines.exe -c pool_veryhard.txt pool_hard.txt -top 500 -fast -o extreme.csv     (12+8=20)
 *    6) nines.exe -c pool_veryhard.txt pool_veryhard.txt -top 500 -fast -o insane.csv  (12+12=24)
 *    7) nines.exe -c pool_insane.txt pairs2.txt -top 200 -fast -o torment.csv          (24+2=26，剩两个)
 *    每份输出 CSV 的 grade 列就是【该档内部】的 S/A/B/C/D 评级。
 *
 *  输出 CSV（每题一行，共 19 列）：
 *    id,tier,n,k,unused,nums,sols,truncated,ways,clean_ways,d_avg,d_max,
 *    min_num,max_num,sum,score,grade,route,route_samples
 *    nums/route/route_samples 用空格和 | 分隔，不含逗号，Excel 直接能开。
 *
 *  编译：gcc nines.c -o nines -lm
 *
 *  注意：字典 solutions.csv 里没有 13 和 17；含这两个数的题会被跳过并提示。
 * ==========================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#ifdef _WIN32
#  include <direct.h>
#  define GETCWD _getcwd
#else
#  include <unistd.h>
#  define GETCWD getcwd
#endif

#define TARGET      9
#define MAX_LINE    65536
#define MAX_N       32          /* 位掩码上限 */
#define MAX_PUZZLE  26          /* 建议的题目规模上限 */
#define KMAX        130321      /* 19^4 */
#define MAX_GROUPS  40000       /* C(32,4) = 35960 */

/* ---- 4 数组难度权重：和 reevaluate.c 完全一致 ---- */
#define W_SOL       5.0
#define W_NUM       2.5
#define W_FRAC      2.5
#define PEN_T       2.5
#define PEN_PAD     2.0
#define MAX_COUNT  100.0
#define LV_EXTREME  7.5
#define LV_HARD     5.5
#define LV_MID      3.0

/* ---- 打分权重（同一档内互相比较） ---- */
#define SC_DMAX     45.0
#define SC_SOLS     30.0
#define SC_WAYS     25.0

/* ============================ 分数（有理数） ============================ */
typedef struct { long long n, d; } Frac;      /* d==0 表示无效 */

static long long ll_abs(long long x) { return x < 0 ? -x : x; }
static long long ll_gcd(long long x, long long y) {
    x = ll_abs(x); y = ll_abs(y);
    while (y) { long long t = x % y; x = y; y = t; }
    return x;
}
static Frac fr_make(long long n, long long d) {
    Frac f;
    if (d == 0) { f.n = 0; f.d = 0; return f; }
    if (d < 0) { n = -n; d = -d; }
    long long g = ll_gcd(n, d);
    if (g > 1) { n /= g; d /= g; }
    f.n = n; f.d = d;
    return f;
}
static Frac fr_bad(void) { Frac f; f.n = 0; f.d = 0; return f; }
static int fr_ok(Frac f) { return f.d != 0; }
static int fr_is(Frac f, long long k) { return f.d != 0 && f.n == k * f.d; }
static int fr_is_int(Frac f) { return f.d != 0 && (f.n % f.d == 0); }
static Frac fr_add(Frac a, Frac b) { return fr_make(a.n * b.d + b.n * a.d, a.d * b.d); }
static Frac fr_sub(Frac a, Frac b) { return fr_make(a.n * b.d - b.n * a.d, a.d * b.d); }
static Frac fr_mul(Frac a, Frac b) { return fr_make(a.n * b.n, a.d * b.d); }
static Frac fr_div(Frac a, Frac b) {
    if (b.n == 0) return fr_bad();
    return fr_make(a.n * b.d, a.d * b.n);
}

/* ========================= 算式解析 + 求值 ========================= */
typedef struct { const char *s; int pos, ok, has_muldiv, pad, frac; } Parse;

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
    r.is9 = (p.ok && fr_is(v, TARGET));
    r.has_muldiv = p.has_muldiv;
    r.pad = p.pad;
    r.frac = p.frac;
    return r;
}

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
    if (cur >= cap - 1 || len <= 0) return;
    if (len > cap - 1 - cur) len = cap - 1 - cur;
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
static int key4(const int *x) { return ((x[0] * 19 + x[1]) * 19 + x[2]) * 19 + x[3]; }

/* ============================== 字典 ============================== */
typedef struct {
    int n[4];
    int count_file, done, n_expr, valid, addsub, pad, frac, T;
    char *expr;
    double diff;
} Entry;

static Entry **ents = NULL;
static int n_ent = 0, cap_ent = 0;
static int dict_idx[KMAX];

static Entry *lookup4(int a, int b, int c, int d) {
    int x[4];
    x[0] = a; x[1] = b; x[2] = c; x[3] = d;
    sort4(x);
    for (int t = 0; t < 4; t++)                 /* key4/dict_idx 只支持 0~18 */
        if (x[t] < 0 || x[t] > 18) return NULL;
    int i = dict_idx[key4(x)];
    return i ? ents[i - 1] : NULL;
}

static int add_entry(int a, int b, int c, int d, int cf, char *expr) {
    int x[4];
    x[0] = a; x[1] = b; x[2] = c; x[3] = d;
    sort4(x);
    for (int i = 0; i < 4; i++)
        if (x[i] < 0 || x[i] > 18) return 0;
    int k = key4(x);
    if (dict_idx[k]) return 0;

    if (n_ent == cap_ent) {
        int nc = cap_ent ? cap_ent * 2 : 4096;
        Entry **ne = (Entry **)realloc(ents, (size_t)nc * sizeof(Entry *));
        if (!ne) return 0;
        ents = ne; cap_ent = nc;
    }
    Entry *e = (Entry *)malloc(sizeof(Entry));
    if (!e) return 0;
    e->n[0] = x[0]; e->n[1] = x[1]; e->n[2] = x[2]; e->n[3] = x[3];
    e->count_file = cf; e->expr = expr; e->done = 0;
    e->n_expr = e->valid = e->addsub = e->pad = e->frac = 0;
    e->T = 0; e->diff = 0.0;
    ents[n_ent] = e;
    n_ent++;
    dict_idx[k] = n_ent;
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
                if (in.pad) pad++;
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
        if (strncmp(line, "num1,", 5) == 0) continue;
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

/* 取一条"干净"示例算式（不注水的优先） */
static void one_sample(Entry *e, char *out, int cap) {
    out[0] = '\0';
    const char *p = e->expr;
    while (*p) {
        const char *semi = strchr(p, ';');
        int len = semi ? (int)(semi - p) : (int)strlen(p);
        char tmp[256];
        int tl = len < (int)sizeof(tmp) - 1 ? len : (int)sizeof(tmp) - 1;
        memcpy(tmp, p, (size_t)tl);
        tmp[tl] = '\0';
        if (tmp[0]) {
            ExprInfo in = eval_expr(tmp);
            if (in.is9 && !in.pad) { app(out, cap, tmp, (int)strlen(tmp)); return; }
        }
        if (!semi) break;
        p = semi + 1;
    }
    p = e->expr;
    while (*p) {
        const char *semi = strchr(p, ';');
        int len = semi ? (int)(semi - p) : (int)strlen(p);
        char tmp[256];
        int tl = len < (int)sizeof(tmp) - 1 ? len : (int)sizeof(tmp) - 1;
        memcpy(tmp, p, (size_t)tl);
        tmp[tl] = '\0';
        if (tmp[0]) { app(out, cap, tmp, (int)strlen(tmp)); return; }
        if (!semi) break;
        p = semi + 1;
    }
}

/* ========================= 关键：批量建组 ========================= */
static unsigned gmask[MAX_GROUPS];
static int      gsol[MAX_GROUPS], gclean[MAX_GROUPS];
static double   gdiff[MAX_GROUPS];
static int      n_groups = 0;

static int *glist[MAX_N];
static int  gcnt[MAX_N], gcap[MAX_N];

/* 保证元素 i 的组列表还能再放一个（失败返回 0） */
static int ensure_cap(int i) {
    if (gcnt[i] < gcap[i]) return 1;
    int nc = gcap[i] ? gcap[i] * 2 : 64;
    int *np = (int *)realloc(glist[i], (size_t)nc * sizeof(int));
    if (!np) return 0;
    glist[i] = np; gcap[i] = nc;
    return 1;
}

static void push_group(int i, unsigned m, Entry *e) {
    if (n_groups >= MAX_GROUPS || gcnt[i] >= gcap[i]) return;   /* 容量已由 add_group4 备好 */
    int gi = n_groups++;
    gmask[gi] = m;
    gsol[gi] = e->valid;
    gclean[gi] = e->valid - e->pad;
    if (gclean[gi] < 0) gclean[gi] = 0;
    gdiff[gi] = e->diff;
    glist[i][gcnt[i]++] = gi;
}

static void add_group4(const int *v, int i, int j, int k, int l) {
    Entry *e = lookup4(v[i], v[j], v[k], v[l]);
    if (!e) return;
    ensure_metrics(e);
    if (e->valid <= 0) return;
    if (n_groups >= MAX_GROUPS) return;
    /* 四个数的容量先都备好；任何一个失败就整组不加，免得留下"半个死组" */
    if (!ensure_cap(i) || !ensure_cap(j) || !ensure_cap(k) || !ensure_cap(l)) return;
    unsigned m = (1u << i) | (1u << j) | (1u << k) | (1u << l);
    push_group(i, m, e);
    push_group(j, m, e);
    push_group(k, m, e);
    push_group(l, m, e);
}

/* ========================= 记忆化 DFS =========================
 * 关键：mask 里的位是【当前这道题】里数字的下标，所以每分析一道新题
 * 都必须作废旧记忆 —— 用 cur_gen 代次戳实现（不整表清零，只在写满时清一次）。*/
typedef struct {
    unsigned mask;
    unsigned gen;                   /* 属于哪一代（哪道题） */
    unsigned char budget, used;
    int choice;                     /* -2 未算, -1 丢掉最低位那个数, >=0 组下标 */
    unsigned long long sols, ways, clean;
    double minsum;
} Memo;

#define MEMO_BITS 19
#define MEMO_SIZE (1u << MEMO_BITS)
static Memo *memo_tab = NULL;
static unsigned cur_gen = 0;            /* 每分析一道题 +1 */
static long long memo_live = 0;         /* 本代写入了多少条 */
static long long memo_physical = 0;     /* 自上次清表以来写过多少槽 */
static long long dfs_calls = 0;
static long long CALL_LIMIT = 2000000LL;   /* 单题 DFS 调用上限，超了就标记"截断" */
static int dfs_truncated = 0;

static unsigned long long CAP_SOLS = 200000ULL;               /* 方案数上限（够了就够了） */
static unsigned long long CAP_WAYS = 1000000000000ULL;        /* 解法上限 1e12 */

static unsigned long long sat_add(unsigned long long a, unsigned long long b,
                                  unsigned long long cap) {
    if (a >= cap || b >= cap) return cap;
    unsigned long long s = a + b;
    return (s < a || s > cap) ? cap : s;
}
static unsigned long long sat_mul(unsigned long long a, unsigned long long b,
                                  unsigned long long cap) {
    if (a == 0 || b == 0) return 0;
    if (a > cap / b) return cap;
    unsigned long long p = a * b;
    return p > cap ? cap : p;
}

static unsigned memo_hash(unsigned mask, int budget) {
    unsigned h = mask * 2654435761u + (unsigned)budget * 2246822519u;
    h ^= h >> 15;
    return h & (MEMO_SIZE - 1);
}

static Memo *memo_find(unsigned mask, int budget) {
    unsigned idx = memo_hash(mask, budget);
    for (int t = 0; t < 24; t++) {
        Memo *m = &memo_tab[idx];
        if (m->gen != cur_gen) return NULL;   /* 空槽或旧代：本代的 key 不可能在更后面 */
        if (m->mask == mask && m->budget == (unsigned char)budget) return m;
        idx = (idx + 1) & (MEMO_SIZE - 1);
    }
    return NULL;
}

static Memo *memo_put(unsigned mask, int budget) {
    if (memo_live >= 400000) return NULL;     /* 单题记忆量封顶，超了就不记（只影响速度） */
    unsigned idx = memo_hash(mask, budget);
    for (int t = 0; t < 24; t++) {
        Memo *m = &memo_tab[idx];
        if (m->gen == cur_gen) {
            if (m->mask == mask && m->budget == (unsigned char)budget) return m;
        } else {
            memo_physical++;                    /* 槽变脏（含覆盖旧代槽） */
            m->mask = mask;
            m->gen = cur_gen;
            m->budget = (unsigned char)budget;
            m->used = 1;
            m->choice = -2;
            m->sols = m->ways = m->clean = 0;
            m->minsum = 1e18;
            memo_live++;
            return m;
        }
        idx = (idx + 1) & (MEMO_SIZE - 1);
    }
    return NULL;
}

typedef struct { unsigned long long sols, ways, clean; double minsum; } DfsRes;

static int dfs_abort = 0;      /* 方案数触顶后提前收工 */

static unsigned low_bit(unsigned m) { return m & (~m + 1u); }
static int bit_index(unsigned b) { int i = 0; while (!(b & 1u)) { b >>= 1; i++; } return i; }

static DfsRes dfs(unsigned mask, int budget) {
    DfsRes r;
    r.sols = 0; r.ways = 0; r.clean = 0; r.minsum = 1e18;
    if (dfs_abort) return r;
    if (mask == 0) { r.sols = 1; r.ways = 1; r.clean = 1; r.minsum = 0.0; return r; }

    Memo *m = memo_find(mask, budget);
    if (m && m->choice != -2) {
        r.sols = m->sols; r.ways = m->ways; r.clean = m->clean; r.minsum = m->minsum;
        return r;
    }
    if (++dfs_calls > CALL_LIMIT) { dfs_truncated = 1; return r; }

    int i = bit_index(low_bit(mask));
    int choice = -2;

    if (budget > 0) {
        DfsRes s = dfs(mask & ~(1u << i), budget - 1);
        if (s.sols || s.ways) {
            r.sols = sat_add(r.sols, s.sols, CAP_SOLS);
            r.ways = sat_add(r.ways, s.ways, CAP_WAYS);
            r.clean = sat_add(r.clean, s.clean, CAP_WAYS);
            if (s.minsum < r.minsum) { r.minsum = s.minsum; choice = -1; }
        }
    }
    for (int t = 0; t < gcnt[i]; t++) {
        if (r.sols >= CAP_SOLS) { dfs_abort = 1; break; }
        int gi = glist[i][t];
        unsigned g = gmask[gi];
        if ((g & mask) != g) continue;
        DfsRes s = dfs(mask & ~g, budget);
        if (s.sols == 0 && s.ways == 0) continue;
        r.sols  = sat_add(r.sols, s.sols, CAP_SOLS);
        r.ways  = sat_add(r.ways, sat_mul((unsigned long long)gsol[gi], s.ways, CAP_WAYS), CAP_WAYS);
        r.clean = sat_add(r.clean, sat_mul((unsigned long long)gclean[gi], s.clean, CAP_WAYS), CAP_WAYS);
        double d = gdiff[gi] + s.minsum;
        if (d < r.minsum) { r.minsum = d; choice = gi; }
    }

    Memo *slot = memo_put(mask, budget);
    if (slot) {
        slot->sols = r.sols; slot->ways = r.ways; slot->clean = r.clean;
        slot->minsum = r.minsum; slot->choice = choice;
    }
    return r;
}

/* ============================== 档位表 ============================== */
typedef struct { int n, k, unused; const char *name; } TierInfo;

static const TierInfo TIERS[] = {
    {  4, 1, 0, "normal"   },
    {  8, 2, 0, "hard"     },
    { 12, 3, 0, "veryhard" },
    { 16, 4, 0, "hardcore" },
    { 20, 5, 0, "extreme"  },
    { 24, 6, 0, "insane"   },
    { 26, 6, 2, "torment"  },
};
#define N_TIERS ((int)(sizeof(TIERS) / sizeof(TIERS[0])))

static const TierInfo *tier_of(int n) {
    for (int i = 0; i < N_TIERS; i++)
        if (TIERS[i].n == n) return &TIERS[i];
    return NULL;
}

/* ============================== 结果 ============================== */
typedef struct {
    int n;
    int v[MAX_PUZZLE];
    int k, unused;
    const char *tier;
    int has_sol;                       /* 1 有解, 0 无解, -1 未知（被截断）, -2 含13/17 */
    int truncated;                     /* sols/ways 只是下界 */
    int capped_sols;                   /* 是"方案数撞上限"造成的截断（=解非常多=简单） */
    unsigned long long sols, ways, clean;
    double d_avg, d_max, score;
    char grade;
    int route_k;
    int route[8][4];
    int route_gi[8];
} Result;

static double compute_score(const Result *r) {
    double d = clamp01(r->d_max / 10.0);
    double sc;
    if (r->capped_sols) {
        /* 撞的是"方案数上限"：说明这题解多到数不清 → 按最简单的算，只留最难的半组的难度 */
        sc = SC_DMAX * d;
    } else {
        double s = 1.0 - clamp01(log((double)r->sols + 1.0) / log((double)CAP_SOLS + 1.0));
        double w = 1.0 - clamp01(log((double)r->ways + 1.0) / log((double)CAP_WAYS + 1.0));
        sc = SC_DMAX * d + SC_SOLS * s + SC_WAYS * w;
    }
    if (sc < 0.0) sc = 0.0;
    if (sc > 100.0) sc = 100.0;
    return floor(sc * 10.0 + 0.5) / 10.0;
}

/* 分析一道题：返回 1 成功，0 失败（越界等） */
static int analyze_puzzle(const int *v, int n, Result *out) {
    for (int i = 0; i < n; i++)
        if (v[i] < 1 || v[i] > 18) return 0;

    memset(out, 0, sizeof(*out));
    out->n = n;
    for (int i = 0; i < n; i++) out->v[i] = v[i];
    out->k = n / 4;
    out->unused = n % 4;
    const TierInfo *ti = tier_of(n);
    out->tier = ti ? ti->name : "custom";
    out->grade = '-';
    if (out->k <= 0) return 0;

    /* 字典里根本没有含 13/17 的组，碰到就无法判定，标成 N（不参与排名） */
    for (int i = 0; i < n; i++) {
        if (v[i] == 13 || v[i] == 17) { out->has_sol = -2; out->grade = 'N'; return 1; }
    }

    /* 新的一道题 = 新一代记忆表（mask 是本题下标，旧结果必须作废） */
    if (memo_physical * 4 >= (long long)MEMO_SIZE * 3) {
        memset(memo_tab, 0, (size_t)MEMO_SIZE * sizeof(Memo));
        memo_physical = 0;
    }
    cur_gen++;
    memo_live = 0;

    /* 建组 */
    n_groups = 0;
    for (int i = 0; i < n; i++) gcnt[i] = 0;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            for (int a = j + 1; a < n; a++)
                for (int b = a + 1; b < n; b++)
                    add_group4(v, i, j, a, b);

    /* 一个数如果不在任何"能拼 9"的 4 数组里，它就只能被剩下不用；
       所以这种数的个数要【超过 unused】才真的无解（26 数档 unused=2） */
    int dead = 0;
    for (int i = 0; i < n; i++) if (gcnt[i] == 0) dead++;

    unsigned full = (n >= 32) ? 0xFFFFFFFFu : ((1u << n) - 1u);

    if (dead > out->unused) {
        out->has_sol = 0;
        out->score = 0.0;
        return 1;
    }

    dfs_calls = 0;
    dfs_truncated = 0;
    dfs_abort = 0;
    DfsRes r = dfs(full, out->unused);
    out->sols = r.sols;
    out->ways = r.ways;
    out->clean = r.clean;
    out->truncated = (dfs_truncated || dfs_abort) ? 1 : 0;
    out->capped_sols = dfs_abort ? 1 : 0;
    /* 方案数触顶时，已知 sols >= CAP，就按 CAP 算分（否则会把"解太多"的简单题算难） */
    if (dfs_abort && out->sols < CAP_SOLS) out->sols = CAP_SOLS;

    if (r.sols > 0) out->has_sol = 1;
    else out->has_sol = dfs_truncated ? -1 : 0;

    /* 复原"最省力路线"（沿着 memo 里记下的选择走） */
    out->route_k = 0;
    if (out->has_sol == 1) {
        unsigned m = full;
        int bud = out->unused;
        int ok = 1;
        while (m && out->route_k < out->k) {
            Memo *mm = memo_find(m, bud);
            if (!mm || mm->choice == -2) { ok = 0; break; }
            if (mm->choice == -1) {
                m &= ~(1u << bit_index(low_bit(m)));
                bud--;
                continue;
            }
            int gi = mm->choice;
            int idx = 0;
            for (int t = 0; t < n; t++)
                if ((gmask[gi] >> t) & 1u) out->route[out->route_k][idx++] = v[t];
            out->route_gi[out->route_k] = gi;
            out->route_k++;
            m &= ~gmask[gi];
        }
        if (!ok) out->route_k = 0;
    }

    /* 难度分量 */
    if (out->route_k == out->k && out->route_k > 0) {
        double sum = 0.0, mx = 0.0;
        for (int t = 0; t < out->route_k; t++) {
            double d = gdiff[out->route_gi[t]];
            sum += d;
            if (d > mx) mx = d;
        }
        out->d_avg = sum / (double)out->route_k;
        out->d_max = mx;
    } else if (r.minsum < 1e17) {
        out->d_avg = r.minsum / (double)out->k;
        out->d_max = out->d_avg;
    }

    out->score = (out->has_sol == 1) ? compute_score(out) : 0.0;
    return 1;
}

/* ============================== 输入解析 ============================== */
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

typedef struct { int n; int v[MAX_PUZZLE]; } NumSet;

/* 把一行 CSV 组装进 buf；*grade_off = 评级字符在这一行里的字节偏移（用于事后回填） */
static int format_row(char *buf, int cap, const Result *r, const char *id,
                      char grade, int *grade_off) {
    char nums[256] = "", route[256] = "", samples[512] = "";
    for (int i = 0; i < r->n; i++) {
        char t[16];
        snprintf(t, sizeof(t), "%s%d", i ? " " : "", r->v[i]);
        app(nums, (int)sizeof(nums), t, (int)strlen(t));
    }
    int mn = r->v[0], mx = r->v[0], sum = 0;
    for (int i = 0; i < r->n; i++) {
        if (r->v[i] < mn) mn = r->v[i];
        if (r->v[i] > mx) mx = r->v[i];
        sum += r->v[i];
    }
    for (int t = 0; t < r->route_k; t++) {
        char seg[64] = "";
        for (int i = 0; i < 4; i++) {
            char tmp[16];
            snprintf(tmp, sizeof(tmp), "%s%d", i ? " " : "", r->route[t][i]);
            app(seg, (int)sizeof(seg), tmp, (int)strlen(tmp));
        }
        if (t) app(route, (int)sizeof(route), " | ", 3);
        app(route, (int)sizeof(route), seg, (int)strlen(seg));

        char s1[128] = "";
        /* 用路线里这一组的 4 个数回查字典取示例（不能再用全局组表，它已被下一题覆盖） */
        Entry *e = lookup4(r->route[t][0], r->route[t][1], r->route[t][2], r->route[t][3]);
        if (e) one_sample(e, s1, (int)sizeof(s1));
        if (t) app(samples, (int)sizeof(samples), " | ", 3);
        app(samples, (int)sizeof(samples), s1, (int)strlen(s1));
    }

    int n = snprintf(buf, (size_t)cap,
                     "%s,%s,%d,%d,%d,%s,%llu,%d,%llu,%llu,%.2f,%.2f,%d,%d,%d,%.1f,",
                     id, r->tier, r->n, r->k, r->unused, nums,
                     r->sols, r->truncated, r->ways, r->clean,
                     r->d_avg, r->d_max, mn, mx, sum, r->score);
    if (n < 0) n = 0;
    if (n > cap - 3) n = cap - 3;
    *grade_off = n;
    buf[n++] = (grade == '\0') ? '-' : grade;
    int m = snprintf(buf + n, (size_t)(cap - n), ",%s,%s\n", route, samples);
    if (m < 0) m = 0;
    n += m;
    if (n > cap - 1) n = cap - 1;
    buf[n] = '\0';
    return n;
}

static void write_row(FILE *f, const Result *r, const char *id) {
    char buf[4096];
    int off = 0;
    format_row(buf, (int)sizeof(buf), r, id, r->grade ? r->grade : '-', &off);
    fputs(buf, f);
}

static void print_report(const Result *r) {
    printf("\n================ %d 个数拼 %d 个 9  [%s] ================\n",
           r->n, r->k, r->tier);
    printf("数字：");
    for (int i = 0; i < r->n; i++) printf("%d ", r->v[i]);
    printf("\n");
    if (r->has_sol == -2) {
        printf("结论：含 13 或 17，字典里没有这两个数的组合，无法判定。\n");
        return;
    }
    if (r->has_sol == 0) {
        printf("结论：拼不出 %d 个 9（无解）。\n", r->k);
        return;
    }
    if (r->has_sol == -1) {
        printf("结论：搜索被截断（状态太多），未能确定。\n");
        return;
    }
    printf("能拼出：是\n");
    printf("分组方案数 sols = %llu%s   总解法对数 ways = %llu%s   干净解 %llu\n",
           r->sols, r->truncated ? "(可能偏小，已截断)" : "",
           r->ways, r->truncated ? "(可能偏小)" : "", r->clean);
    printf("最省力路线：平均半组难度 %.2f，最难半组难度 %.2f\n", r->d_avg, r->d_max);
    for (int t = 0; t < r->route_k; t++) {
        Entry *e = lookup4(r->route[t][0], r->route[t][1], r->route[t][2], r->route[t][3]);
        char s1[128] = "";
        if (e) { ensure_metrics(e); one_sample(e, s1, (int)sizeof(s1)); }
        printf("  第 %d 组: %d %d %d %d   (难度 %.1f)  例: %s\n",
               t + 1, r->route[t][0], r->route[t][1], r->route[t][2], r->route[t][3],
               e ? e->diff : 0.0, s1);
    }
    printf("评分 score = %.1f / 100   （单题模式不做档内排名，批量模式才给 S/A/B/C/D）\n",
           r->score);
}

/* ============================== 评级 ============================== */
static int cmp_score_asc(const void *a, const void *b) {
    const Result *x = *(const Result * const *)a;
    const Result *y = *(const Result * const *)b;
    if (x->score < y->score) return -1;
    if (x->score > y->score) return 1;
    return 0;
}

/* 同一档内部按 score 排名给评级：前 5% S，5~20% A，20~50% B，50~80% C，后 20% D
 * 每档只处理一次：取 grade 还是 '-' 的第一个题，把【同档且未评级】的全部收集起来排一次。*/
static void assign_grades(Result *res, int cnt) {
    for (int i = 0; i < cnt; i++) {
        if (res[i].has_sol == 0) res[i].grade = 'X';
        else if (res[i].has_sol == -2) res[i].grade = 'N';
        else if (res[i].has_sol < 0) res[i].grade = '?';   /* 截断且没找到解：无法判定 */
        else res[i].grade = '-';                           /* 有解就参与档内排名（含截断的） */
    }
    Result **idx = (Result **)malloc((size_t)(cnt > 0 ? cnt : 1) * sizeof(Result *));
    if (!idx) return;

    for (int i = 0; i < cnt; i++) {
        if (res[i].grade != '-') continue;
        const char *tier = res[i].tier;
        int m = 0;
        for (int j = 0; j < cnt; j++)
            if (res[j].grade == '-' && res[j].n == res[i].n && strcmp(res[j].tier, tier) == 0)
                idx[m++] = &res[j];
        qsort(idx, (size_t)m, sizeof(Result *), cmp_score_asc);
        for (int k = 0; k < m; k++) {
            double p = (m <= 1) ? 1.0 : (double)k / (double)(m - 1);
            idx[k]->grade = (p >= 0.95) ? 'S' : (p >= 0.80) ? 'A' : (p >= 0.50) ? 'B'
                          : (p >= 0.20) ? 'C' : 'D';
        }
    }
    free(idx);
}

/* ============================== main ============================== */
/* 把有解的题按档写成纯数字池文件 pool_<tier>.txt，供 -c 组合生成下一档用。
 * 每档内部按 score 从高到低写，所以 -head K 直接取前 K 行就是最好的 K 个。 */
static void write_pools(Result *res, int cnt) {
    Result **idx = NULL;
    if (cnt > 0) idx = (Result **)malloc((size_t)cnt * sizeof(Result *));

    for (int t = 0; t < N_TIERS; t++) {
        int m = 0, wrote = 0;
        if (!idx) break;
        for (int i = 0; i < cnt; i++)
            if (res[i].has_sol == 1 && strcmp(res[i].tier, TIERS[t].name) == 0) idx[m++] = &res[i];
        if (m == 0) continue;

        qsort(idx, (size_t)m, sizeof(Result *), cmp_score_asc);   /* 升序 */

        char fn[64];
        snprintf(fn, sizeof(fn), "pool_%s.txt", TIERS[t].name);
        FILE *pf = fopen(fn, "w");
        if (!pf) continue;
        for (int k = m - 1; k >= 0; k--) {                        /* 倒着写 = 最高分在前 */
            Result *r = idx[k];
            for (int q = 0; q < r->n; q++)
                fprintf(pf, "%s%d", q ? " " : "", r->v[q]);
            fprintf(pf, "\n");
            wrote++;
        }
        fclose(pf);
        printf("  已写 %s（%d 组，按分数从高到低；下一档可用 -head K 直接取前 K 组）\n",
               fn, wrote);
    }
    free(idx);
}

/* 生成 pairs2.txt：所有两数组合（1~18，去掉 13/17），做 26 数题时当"剩两个"的池子用 */
static void write_pairs2(void) {
    FILE *f = fopen("pairs2.txt", "w");
    if (!f) return;
    int c = 0;
    for (int a = 1; a <= 18; a++) {
        if (a == 13 || a == 17) continue;
        for (int b = a; b <= 18; b++) {
            if (b == 13 || b == 17) continue;
            fprintf(f, "%d %d\n", a, b);
            c++;
        }
    }
    fclose(f);
    printf("已写 pairs2.txt（%d 组两数池）；26 数题 = -c pool_insane.txt pairs2.txt\n", c);
}

/* ============ 全阶梯自动生成（无参数、没有 pools.txt 时走这里） ============
 * normal(4) -> hard(8) -> veryhard(12) -> hardcore(16) -> extreme(20)
 *           -> insane(24) -> torment(26)
 * 每一档都写出 scores_<档名>.csv 和 pool_<档名>.txt。 */
static Result *analyze_all(const NumSet *A, int na, int maxout, int *outcnt) {
    int cap = (na < maxout) ? na : maxout;
    if (cap < 1) cap = 1;
    Result *res = (Result *)malloc((size_t)cap * sizeof(Result));
    int cnt = 0;
    if (!res) { *outcnt = 0; return NULL; }
    for (int i = 0; i < na && cnt < cap; i++) {
        Result r;
        if (!analyze_puzzle(A[i].v, A[i].n, &r)) continue;
        res[cnt++] = r;
    }
    *outcnt = cnt;
    return res;
}

static Result *compose_pools(const NumSet *A, int na, const NumSet *B, int nb,
                             int maxout, int *outcnt) {
    long long tot = (long long)na * (long long)nb;
    int cap = (tot < (long long)maxout) ? (int)tot : maxout;
    if (cap < 1) cap = 1;
    Result *res = (Result *)malloc((size_t)cap * sizeof(Result));
    int cnt = 0;
    if (!res) { *outcnt = 0; return NULL; }
    for (int i = 0; i < na && cnt < cap; i++) {
        for (int j = 0; j < nb && cnt < cap; j++) {
            int v[MAX_PUZZLE];
            int n = A[i].n + B[j].n;
            if (n > MAX_PUZZLE || n < 4) continue;
            memcpy(v, A[i].v, (size_t)A[i].n * sizeof(int));
            memcpy(v + A[i].n, B[j].v, (size_t)B[j].n * sizeof(int));
            Result r;
            if (!analyze_puzzle(v, n, &r)) continue;
            res[cnt++] = r;
        }
    }
    *outcnt = cnt;
    return res;
}

/* 取分数最高的前 k 个（有解、未被截断）当下一档的组合池 */
static NumSet *pick_top(Result *res, int cnt, int k, int *outn) {
    *outn = 0;
    Result **idx = (Result **)malloc((size_t)(cnt > 0 ? cnt : 1) * sizeof(Result *));
    if (!idx) return NULL;
    int m = 0;
    for (int i = 0; i < cnt; i++)
        if (res[i].has_sol == 1 && res[i].grade != '?') idx[m++] = &res[i];
    qsort(idx, (size_t)m, sizeof(Result *), cmp_score_asc);   /* 升序：末尾最高分 */

    int take = (m < k) ? m : k;
    NumSet *v = (NumSet *)malloc((size_t)(take > 0 ? take : 1) * sizeof(NumSet));
    if (!v) { free(idx); return NULL; }
    for (int i = 0; i < take; i++) {
        Result *r = idx[m - 1 - i];
        v[i].n = r->n;
        for (int t = 0; t < r->n && t < MAX_PUZZLE; t++) v[i].v[t] = r->v[t];
    }
    free(idx);
    *outn = take;
    return v;
}

/* -top 过滤：n>=4 的按分数从高到低取前 topk；
 * n<4 的"小池子"（例如 pairs2.txt 里每组只有 2 个数）没法当题目打分，
 * 必须整池原样保留，否则会被筛光导致 0 道候选。 */
static int filter_top(NumSet *A, int na, Result *tmp, int topk) {
    int m = 0;
    for (int i = 0; i < na; i++) {
        if (A[i].n < 4) continue;
        Result r;
        if (analyze_puzzle(A[i].v, A[i].n, &r) && r.has_sol == 1) tmp[m++] = r;
    }
    for (int i = 1; i < m; i++) {                 /* 按 score 降序插入排序 */
        Result cur = tmp[i];
        int j = i - 1;
        while (j >= 0 && tmp[j].score < cur.score) { tmp[j + 1] = tmp[j]; j--; }
        tmp[j + 1] = cur;
    }
    if (m > topk) m = topk;

    int w = 0;
    for (int i = 0; i < na; i++) {                /* 小池子原样放前面 */
        if (A[i].n >= 4) continue;
        A[w].n = A[i].n;
        memcpy(A[w].v, A[i].v, sizeof(A[w].v));
        w++;
    }
    for (int i = 0; i < m; i++) {                 /* 再放打分后的前 m 个 */
        A[w].n = tmp[i].n;
        memcpy(A[w].v, tmp[i].v, sizeof(A[w].v));
        w++;
    }
    return w;
}

/* 一档的收尾：评级 + 写 CSV + 写 pool 文件 */
static void write_stage(Result *res, int cnt, const char *csvname) {
    assign_grades(res, cnt);
    FILE *f = fopen(csvname, "w");
    if (f) {
        fprintf(f, "id,tier,n,k,unused,nums,sols,truncated,ways,clean_ways,"
                   "d_avg,d_max,min_num,max_num,sum,score,grade,route,route_samples\n");
        for (int i = 0; i < cnt; i++) {
            char id[32];
            snprintf(id, sizeof(id), "P%d", i + 1);
            write_row(f, &res[i], id);
        }
        fclose(f);
    }
    printf("  -> %s（%d 题）\n", csvname, cnt);
    write_pools(res, cnt);
}

/* 从 solutions_reeval.csv 读每行开头 4 个数 */
static int read_csv_first4(const char *fname, NumSet **out) {
    *out = NULL;
    FILE *f = fopen(fname, "r");
    if (!f) return -1;
    int n = 0, cap = 0;
    NumSet *v = NULL;
    static char line[MAX_LINE];
    int first = 1;
    while (fgets(line, sizeof(line), f)) {
        if (first) { strip_bom(line); first = 0; }
        strip_eol(line);
        if (!line[0] || line[0] == '#') continue;
        if (n == cap) {
            int nc = cap ? cap * 2 : 512;
            NumSet *t = (NumSet *)realloc(v, (size_t)nc * sizeof(NumSet));
            if (!t) break;
            v = t; cap = nc;
        }
        v[n].n = parse_nums(line, v[n].v, 4);
        if (v[n].n == 4) n++;
    }
    fclose(f);
    *out = v;
    return n;
}

/* 内存里造出 136 组两数（给 26 档当"剩两个"） */
static int make_pairs2_mem(NumSet **out) {
    NumSet *v = (NumSet *)malloc(256 * sizeof(NumSet));
    if (!v) { *out = NULL; return 0; }
    int n = 0;
    for (int a = 1; a <= 18; a++) {
        if (a == 13 || a == 17) continue;
        for (int b = a; b <= 18; b++) {
            if (b == 13 || b == 17) continue;
            v[n].n = 2; v[n].v[0] = a; v[n].v[1] = b; n++;
        }
    }
    *out = v;
    return n;
}

static void auto_ladder(int topk, int maxout, int user_caps) {
    clock_t t0 = clock();
    if (!user_caps) { CALL_LIMIT = 200000LL; CAP_SOLS = 20000ULL; }   /* 自动模式默认用粗算 */

    NumSet *p0 = NULL;
    int n0 = read_csv_first4("solutions_reeval.csv", &p0);
    if (n0 <= 0) { printf("读不到 solutions_reeval.csv 的前 4 列，无法自动生成。\n"); free(p0); return; }
    printf("输入：solutions_reeval.csv（%d 组 4 数）；每档取分数最高的 %d 组做池子\n", n0, topk);

    printf("\n[1/7] normal    4 个数\n");
    int c0 = 0; Result *r0 = analyze_all(p0, n0, maxout, &c0);
    write_stage(r0, c0, "scores_normal.csv");

    int m0 = 0; NumSet *s0 = pick_top(r0, c0, topk, &m0);

    printf("\n[2/7] hard      8 个数 = 4+4（%d x %d）\n", m0, m0);
    int c1 = 0; Result *r1 = compose_pools(s0, m0, s0, m0, maxout, &c1);
    write_stage(r1, c1, "scores_hard.csv");
    int m1 = 0; NumSet *s1 = pick_top(r1, c1, topk, &m1);

    printf("\n[3/7] veryhard  12 个数 = 8+4（%d x %d）\n", m1, m0);
    int c2 = 0; Result *r2 = compose_pools(s1, m1, s0, m0, maxout, &c2);
    write_stage(r2, c2, "scores_veryhard.csv");

    printf("\n[4/7] hardcore  16 个数 = 8+8（%d x %d）\n", m1, m1);
    int c3 = 0; Result *r3 = compose_pools(s1, m1, s1, m1, maxout, &c3);
    write_stage(r3, c3, "scores_hardcore.csv");
    free(r3);

    int m2 = 0; NumSet *s2 = pick_top(r2, c2, topk, &m2);

    printf("\n[5/7] extreme   20 个数 = 12+8（%d x %d）\n", m2, m1);
    int c4 = 0; Result *r4 = compose_pools(s2, m2, s1, m1, maxout, &c4);
    write_stage(r4, c4, "scores_extreme.csv");
    free(r4);

    printf("\n[6/7] insane    24 个数 = 12+12（%d x %d）\n", m2, m2);
    int c5 = 0; Result *r5 = compose_pools(s2, m2, s2, m2, maxout, &c5);
    write_stage(r5, c5, "scores_insane.csv");
    int m5 = 0; NumSet *s5 = pick_top(r5, c5, topk, &m5);

    NumSet *p2 = NULL;
    int n2 = make_pairs2_mem(&p2);
    printf("\n[7/7] torment   26 个数 = 24+2（%d x %d，剩两个不用）\n", m5, n2);
    int c6 = 0; Result *r6 = compose_pools(s5, m5, p2, n2, maxout, &c6);
    write_stage(r6, c6, "scores_torment.csv");

    free(r6); free(p2); free(s5); free(r5); free(s2); free(r2);
    free(s1); free(r1); free(s0); free(r0); free(p0);

    printf("\n全部完成，用时 %.1f 秒。\n", (double)(clock() - t0) / CLOCKS_PER_SEC);
    printf("每个 scores_<档名>.csv 里的 grade 列就是该档内部的 S/A/B/C/D 评级。\n");
}

static void usage(const char *exe) {
    printf("用法：\n");
    printf("  %s                          ★无参数：自动跑完 7 个档位（见下）\n", exe);
    printf("  %s 1 1 3 14 ...             直接给数（个数决定档位）\n", exe);
    printf("  %s -f pool.txt              批量：文件每行一组数\n", exe);
    printf("  %s -c A.txt B.txt           组合：两个池交叉组合成新题再打分\n", exe);
    printf("无参数时的行为：有 pools.txt 就批量跑它；否则若有 solutions_reeval.csv 就\n");
    printf("  自动依次生成 normal -> hard -> veryhard -> hardcore -> extreme -> insane -> torment，\n");
    printf("  每档写出 scores_<档名>.csv 和 pool_<档名>.txt；都没有则进入键盘输入模式。\n");
    printf("选项：\n");
    printf("  -o out.csv   输出文件   -d dict.csv 字典   -max M 每档最多 M 题（默认 100000）\n");
    printf("  -top K       每个池只取 score 最高的 K 组（自动/组合模式，默认自动模式 60）\n");
    printf("  -head K      每个池直接取前 K 组（池文件已按分数排好，比 -top 快很多）\n");
    printf("  -turbo       极限粗算：20/24/26 档想很快出结果时用（计数只是下界）\n");
    printf("  -fast        粗算（大档位推荐）   -slow 精算\n");
    printf("  -cap N       方案数上限（默认 200000）；-call N 单题搜索步数上限（默认 2000000）\n");
    printf("  -col4        批量模式下每行只取开头 4 个数（用来直接吃 solutions_reeval.csv）\n");
    printf("  -q           安静模式\n");
    printf("档位：4=normal 8=hard 12=veryhard 16=hardcore 20=extreme 24=insane 26=torment\n");
}

int main(int argc, char **argv) {
    const char *dict_name = "solutions.csv";
    const char *out_name = NULL;
    const char *file_name = NULL;
    const char *ca = NULL, *cb = NULL;
    int quiet = 0, topk = 0, col4 = 0, user_caps = 0, headk = 0;
    int maxout = 100000;
    int nums[MAX_PUZZLE];
    int have = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-d") && i + 1 < argc) dict_name = argv[++i];
        else if (!strcmp(a, "-o") && i + 1 < argc) out_name = argv[++i];
        else if (!strcmp(a, "-f") && i + 1 < argc) file_name = argv[++i];
        else if (!strcmp(a, "-c") && i + 2 < argc) { ca = argv[++i]; cb = argv[++i]; }
        else if (!strcmp(a, "-max") && i + 1 < argc) maxout = atoi(argv[++i]);
        else if (!strcmp(a, "-top") && i + 1 < argc) topk = atoi(argv[++i]);
        else if (!strcmp(a, "-head") && i + 1 < argc) headk = atoi(argv[++i]);
        else if (!strcmp(a, "-turbo")) { CALL_LIMIT = 30000LL; CAP_SOLS = 1000ULL; user_caps = 1; }
        else if (!strcmp(a, "-cap") && i + 1 < argc) {
            long long v = atoll(argv[++i]);
            if (v > 0) { CAP_SOLS = (unsigned long long)v; user_caps = 1; }
        }
        else if (!strcmp(a, "-call") && i + 1 < argc) {
            long long v = atoll(argv[++i]);
            if (v > 0) { CALL_LIMIT = v; user_caps = 1; }
        }
        else if (!strcmp(a, "-fast")) { CALL_LIMIT = 200000LL; CAP_SOLS = 20000ULL; user_caps = 1; }
        else if (!strcmp(a, "-slow")) { CALL_LIMIT = 20000000LL; CAP_SOLS = 2000000ULL; user_caps = 1; }
        else if (!strcmp(a, "-col4")) col4 = 1;
        else if (!strcmp(a, "-q")) quiet = 1;
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(argv[0]); return 0; }
        else if (a[0] == '-' && a[1] >= 'a' && a[1] <= 'z') {
            printf("未知选项：%s\n", a);
            usage(argv[0]);
            return 1;
        } else {
            have += parse_nums(a, nums + have, MAX_PUZZLE - have);
        }
    }

    /* 先把输出文件建好——这样哪怕后面出错，文件也已经存在 */
    char outbuf[512];
    if (!out_name) out_name = (have > 0) ? "nines_result.csv" : "nines_scores.csv";
    snprintf(outbuf, sizeof(outbuf), "%s", out_name);
    FILE *fout = fopen(outbuf, "w");
    if (!fout) { printf("无法创建输出文件：%s\n", outbuf); return 1; }
    fprintf(fout, "id,tier,n,k,unused,nums,sols,truncated,ways,clean_ways,"
                  "d_avg,d_max,min_num,max_num,sum,score,grade,route,route_samples\n");
    fflush(fout);

    {
        char cwd[512] = "";
        if (GETCWD(cwd, (int)sizeof(cwd))) printf("工作目录：%s\n", cwd);
        printf("输出文件：%s（已创建）\n", outbuf);
    }

    if (!load_dict(dict_name)) {
        fclose(fout);
        printf("字典没读到，输出文件里只有表头。检查 solutions.csv 是否和 exe 在同一目录。\n");
        return 1;
    }

    memo_tab = (Memo *)calloc(MEMO_SIZE, sizeof(Memo));
    if (!memo_tab) { printf("内存不足（记忆表）。\n"); fclose(fout); return 1; }

    /* ---------------- 单题模式 ---------------- */
    if (have > 0 && !file_name && !ca) {
        Result r;
        if (!analyze_puzzle(nums, have, &r)) {
            printf("这组数不合法（要么个数 <4，要么有数字不在 1~18）。\n");
        } else {
            if (!quiet) print_report(&r);
            write_row(fout, &r, "one");
        }
        fclose(fout);
        printf("\n结果已写入 %s\n", outbuf);
        return 0;
    }

    /* ---------------- 需要批量处理：先把题目收集起来 ---------------- */
    if (maxout < 1) maxout = 1;
    write_pairs2();

    Result *res = (Result *)malloc((size_t)maxout * sizeof(Result));
    if (!res) { printf("内存不足。\n"); fclose(fout); return 1; }
    int cnt = 0, skipped = 0;
    long long *row_off = NULL;      /* 组合模式：每行 grade 字符在文件里的偏移 */

    if (ca && cb) {
        /* 组合模式：读两个池（纯数字文本，每行一组） */
        FILE *fa = fopen(ca, "r"), *fb = fopen(cb, "r");
        if (!fa || !fb) {
            printf("打不开组合池文件：%s / %s\n", ca, cb);
            fclose(fout);
            return 1;
        }
        NumSet *A = NULL, *B = NULL;
        int na = 0, nb = 0, capa = 0, capb = 0;
        static char bl[MAX_LINE];
        int first = 1;
        while (fgets(bl, sizeof(bl), fa)) {
            if (first) { strip_bom(bl); first = 0; }
            strip_eol(bl);
            if (!bl[0] || bl[0] == '#') continue;
            if (na == capa) {
                int nc = capa ? capa * 2 : 256;
                NumSet *t = (NumSet *)realloc(A, (size_t)nc * sizeof(NumSet));
                if (!t) {
                    printf("内存不足（读组合池 A）\n");
                    fclose(fa); fclose(fb); fclose(fout); free(A); free(B);
                    return 1;
                }
                A = t; capa = nc;
            }
            A[na].n = parse_nums(bl, A[na].v, MAX_PUZZLE);
            if (A[na].n > 0) na++;
        }
        fclose(fa);
        first = 1;
        while (fgets(bl, sizeof(bl), fb)) {
            if (first) { strip_bom(bl); first = 0; }
            strip_eol(bl);
            if (!bl[0] || bl[0] == '#') continue;
            if (nb == capb) {
                int nc = capb ? capb * 2 : 256;
                NumSet *t = (NumSet *)realloc(B, (size_t)nc * sizeof(NumSet));
                if (!t) {
                    printf("内存不足（读组合池 B）\n");
                    fclose(fb); fclose(fout); free(A); free(B);
                    return 1;
                }
                B = t; capb = nc;
            }
            B[nb].n = parse_nums(bl, B[nb].v, MAX_PUZZLE);
            if (B[nb].n > 0) nb++;
        }
        fclose(fb);
        printf("组合池：A 有 %d 组，B 有 %d 组\n", na, nb);

        /* -head K：池子文件已按分数从高到低写好，直接取前 K 组，省掉重新打分 */
        if (headk > 0) {
            if (na > headk) na = headk;
            if (nb > headk) nb = headk;
            printf("-head %d：每池直接取前 %d 组（不重新打分）\n", headk, headk);
        }
        /* -top：给 n>=4 的池子打分取前 topk；n<4 的小池子（pairs2.txt）整池保留 */
        else if (topk > 0) {
            Result *tmp = (Result *)malloc((size_t)(na > nb ? na : nb) * sizeof(Result));
            if (tmp) {
                na = filter_top(A, na, tmp, topk);
                nb = filter_top(B, nb, tmp, topk);
                free(tmp);
                printf("-top %d 之后：A 有 %d 组，B 有 %d 组（不足 4 个数的小池子整池保留）\n",
                       topk, na, nb);
            }
        }

        {
            long long tot = (long long)na * (long long)nb;
            long long done = 0;
            row_off = (long long *)malloc((size_t)maxout * sizeof(long long));
            printf("开始逐题分析：最多 %lld 道候选\n", tot);
            if (row_off)
                printf("（边跑边写 %s：中途关窗口也能保住已经分析好的行，只是 grade 可能还没回填）\n",
                       out_name ? out_name : "输出文件");
            else
                printf("（注意：这一档要全部跑完才会写 CSV，中途 %s 里只有表头）\n",
                       out_name ? out_name : "输出文件");
            fflush(stdout);

            for (int i = 0; i < na && cnt < maxout; i++) {
                for (int j = 0; j < nb && cnt < maxout; j++) {
                    done++;
                    int v[MAX_PUZZLE];
                    int n = A[i].n + B[j].n;
                    if (n > MAX_PUZZLE || n < 4) continue;
                    memcpy(v, A[i].v, (size_t)A[i].n * sizeof(int));
                    memcpy(v + A[i].n, B[j].v, (size_t)B[j].n * sizeof(int));
                    Result r;
                    if (!analyze_puzzle(v, n, &r)) { skipped++; continue; }
                    res[cnt] = r;
                    if (row_off) {                 /* 边跑边写：中途被打断也保住已分析的行 */
                        char idbuf[32], linebuf[4096];
                        int goff = 0;
                        snprintf(idbuf, sizeof(idbuf), "P%d", cnt + 1);
                        format_row(linebuf, (int)sizeof(linebuf), &res[cnt], idbuf, '-', &goff);
                        fputs(linebuf, fout);
                        row_off[cnt] = goff;       /* grade 字符在这一行里的位置 */
                    }
                    cnt++;
                    if ((done % 200) == 0) {
                        printf("  已分析 %lld / %lld 题，已收集 %d 题…\n", done, tot, cnt);
                        fflush(stdout);
                    }
                }
            }
            printf("候选全部过完（%lld 道），收集 %d 题\n", done, cnt);
        }
        printf("组合完成，得到 %d 道题（跳过 %d）\n", cnt, skipped);
        free(A); free(B);
    } else {
        /* 批量文件 / pools.txt / 键盘输入 */
        FILE *f = NULL;
        int interactive = 0;
        if (file_name) {
            f = fopen(file_name, "r");
            if (!f) { printf("打不开批量文件：%s\n", file_name); fclose(fout); return 1; }
        } else {
            f = fopen("pools.txt", "r");
            if (f) {
                printf("没有参数，自动处理 pools.txt\n");
            } else {
                FILE *chk = fopen("solutions_reeval.csv", "r");
                if (chk) {
                    fclose(chk);
                    printf("没有参数、也没有 pools.txt：进入【全阶梯自动生成】模式\n");
                    printf("每档取分数最高的 %d 组做池子，每档最多 %d 题（可用 -top / -max 调整）\n",
                           topk > 0 ? topk : 60, maxout);
                    fclose(fout);
                    free(res);
                    auto_ladder(topk > 0 ? topk : 60, maxout, user_caps);
                    return 0;
                }
                f = stdin;
                interactive = 1;
            }
        }

        if (interactive) {
            printf("\n键盘输入模式：每行一组数（4/8/12/16/20/24/26 个，空格或逗号分隔），直接回车结束\n");
            printf("例：1 1 3 14 4 5 6 10 1 1 8 8 2 2 5 7\n> ");
            fflush(stdout);
        }

        static char bl[MAX_LINE];
        int first = 1, ln = 0;
        while (fgets(bl, sizeof(bl), f)) {
            if (first) { strip_bom(bl); first = 0; }
            strip_eol(bl);
            if (!bl[0]) { if (interactive) break; continue; }
            if (bl[0] == '#') continue;
            ln++;
            int v[MAX_PUZZLE];
            int n = parse_nums(bl, v, col4 ? 4 : MAX_PUZZLE);   /* -col4：只取每行开头 4 个数 */
            if (n < 4) { if (!interactive) printf("第 %d 行只有 %d 个数，跳过\n", ln, n); continue; }
            if (n > 4 && col4) n = 4;
            if (cnt >= maxout) { printf("已达 -max %d 上限，停止。\n", maxout); break; }
            Result r;
            if (!analyze_puzzle(v, n, &r)) {
                printf("第 %d 行个数不足 4 或有数字不在 1~18，跳过\n", ln);
                skipped++;
                continue;
            }
            if (!quiet) print_report(&r);
            res[cnt++] = r;
            if (interactive) { printf("> "); fflush(stdout); }
        }
        if (f != stdin) fclose(f);
        printf("共分析 %d 道题（跳过 %d）\n", cnt, skipped);
    }

    /* ---------------- 档内评级 + 写 CSV ---------------- */
    assign_grades(res, cnt);
    if (row_off) {
        /* 组合模式：行已边跑边写好（grade 是占位 '-'），这里整档重写一遍把 grade 补上 */
        fflush(fout);
        fclose(fout);
        fout = NULL;

        char tmpname[600];
        snprintf(tmpname, sizeof(tmpname), "%s.tmp", outbuf);
        FILE *fin2 = fopen(outbuf, "r");
        FILE *fout2 = fopen(tmpname, "w");
        if (fin2 && fout2) {
            static char lbuf[8192];
            int li = 0, patched = 0;
            while (fgets(lbuf, sizeof(lbuf), fin2)) {
                size_t L = strlen(lbuf);
                while (L > 0 && (lbuf[L - 1] == '\n' || lbuf[L - 1] == '\r')) lbuf[--L] = '\0';
                if (li >= 1 && li - 1 < cnt) {            /* li == 0 是表头 */
                    int off = (int)row_off[li - 1];
                    if (off >= 0 && off < (int)L) {
                        lbuf[off] = res[li - 1].grade ? res[li - 1].grade : '-';
                        patched++;
                    }
                }
                fprintf(fout2, "%s\n", lbuf);
                li++;
            }
            printf("已回填 %d 行的档内评级\n", patched);
        }
        if (fin2) fclose(fin2);
        if (fout2) fclose(fout2);
        if (fin2 && fout2) {
            remove(outbuf);
            if (rename(tmpname, outbuf) != 0)
                printf("（提示：%s 可能正被 Excel 打开，评级结果在 %s，关掉 Excel 后把它改名即可）\n",
                       outbuf, tmpname);
        } else {
            printf("（重写失败，原文件保留；已分析的行 grade 还是占位 '-'）\n");
        }
        free(row_off);
    } else {
        for (int i = 0; i < cnt; i++) {
            char id[32];
            snprintf(id, sizeof(id), "P%d", i + 1);
            write_row(fout, &res[i], id);
        }
    }
    if (fout) fclose(fout);

    printf("\n各档纯数字池：\n");
    write_pools(res, cnt);

    /* 分档统计 */
    printf("\n---------------- 分档统计 ----------------\n");
    for (int t = 0; t < N_TIERS; t++) {
        int total = 0, solved = 0, unknown = 0, g[5] = {0, 0, 0, 0, 0}, x = 0, q = 0;
        for (int i = 0; i < cnt; i++) {
            if (strcmp(res[i].tier, TIERS[t].name) != 0) continue;
            total++;
            if (res[i].has_sol == 1) {
                solved++;
                switch (res[i].grade) {
                    case 'S': g[0]++; break;
                    case 'A': g[1]++; break;
                    case 'B': g[2]++; break;
                    case 'C': g[3]++; break;
                    case 'D': g[4]++; break;
                    default:  q++; break;          /* '?' / 'N' 之类不参与排名的 */
                }
            } else if (res[i].has_sol < 0) unknown++;
            else x++;
        }
        if (total)
            printf("%-9s (%2d 个数): 共 %d 题，有解 %d，无解 %d，无法判定 %d   "
                   "S=%d A=%d B=%d C=%d D=%d 未排名=%d\n",
                   TIERS[t].name, TIERS[t].n, total, solved, x, unknown,
                   g[0], g[1], g[2], g[3], g[4], q);
    }
    printf("------------------------------------------\n");
    printf("结果已写入 %s\n", outbuf);
    free(res);
    return 0;
}
