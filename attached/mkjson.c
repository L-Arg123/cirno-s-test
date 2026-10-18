/* ============================================================================
 *  mkjson.c —— 把各档 scores CSV 导出成网页用的题库
 *
 *  产物：
 *    puzzles.js    网页用 <script src="puzzles.js"></script> 直接加载（双击 html 也能跑）
 *    puzzles.json  纯 JSON，将来放到服务器上给 fetch 用
 *
 *  用法：
 *    mkjson.exe                     自动扫描当前目录所有 CSV，认出我们自己的那些档
 *    mkjson.exe -k 300              每档最多导出 300 题（默认 200）
 *    mkjson.exe -o data             输出文件名前缀（默认 puzzles -> puzzles.js/.json）
 *    mkjson.exe normal.csv hard.csv 也可以手动指定要导的 CSV
 *
 *  会做的事：
 *    · 只认表头是 id,tier,n,k,unused,nums 的 CSV（solutions.csv 等会自动跳过）
 *    · 跳过没解的题（sols<=0）和没有答案路线的题
 *    · 按"数字多重集合"去重（两对不同的池子可能组合出同一组数字）
 *    · 每档按 score 从高到低排，然后【等间隔取样】到 K 题（保留最难和最易两端，
 *      这样页面上既能出难题也能出简单题），并给出档内百分位 p（1=该档最难）
 *    · 答案一并导出：每组 4 个数 + 一条示例算式
 *
 *  编译：gcc mkjson.c -o mkjson.exe
 * ==========================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#  include <io.h>
#  define FIND_FIRST _findfirst
#  define FIND_NEXT  _findnext
#  define FIND_CLOSE _findclose
#  define FIND_T     struct _finddata_t
#else
#  include <dirent.h>
#endif

#define MAX_LINE 65536
#define MAXF     24
#define MAXG     8            /* 一题最多 6 组，留点余量 */
#define MAXN     32

typedef struct {
    char tier[16];
    int  n, k, unused;
    int  nums[MAXN];
    long long sols, ways;
    double davg, dmax, score;
    char grade;
    int  truncated;
    int  gk;                  /* 答案组数 */
    int  gnums[MAXG][4];
    char gexpr[MAXG][128];
} Rec;

static Rec  *recs = NULL;
static int   nrec = 0, caprec = 0;
static char (*keys)[320] = NULL;

static void strip_eol(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = '\0';
}
static void strip_bom(char *s) {
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB &&
        (unsigned char)s[2] == 0xBF)
        memmove(s, s + 3, strlen(s + 3) + 1);
}
static void trim(char *s) {
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = '\0';
}

static int parse_ints(const char *s, int *out, int maxn) {
    int n = 0;
    while (*s && n < maxn) {
        while (*s == ' ' || *s == '\t' || *s == ',' || *s == '|') s++;
        if (!*s) break;
        char *end;
        long v = strtol(s, &end, 10);
        if (end == s) break;
        out[n++] = (int)v;
        s = end;
    }
    return n;
}

static int split_csv(char *line, char **fields, int maxf) {
    int n = 0;
    char *p = line;
    fields[n++] = p;
    while (*p) {
        if (*p == ',') {
            *p = '\0';
            if (n >= maxf) break;
            fields[n++] = p + 1;
        }
        p++;
    }
    return n;
}

/* route 形如 "1 1 3 14 | 4 5 6 10"；samples 形如 "expr1 | expr2" */
static int split_groups(char *s, int gnums[MAXG][4]) {
    int g = 0;
    char *p = s;
    while (*p && g < MAXG) {
        char *bar = strchr(p, '|');
        if (bar) *bar = '\0';
        trim(p);
        if (*p) {
            int c = parse_ints(p, gnums[g], 4);
            if (c == 4) g++;
        }
        if (!bar) break;
        p = bar + 1;
    }
    return g;
}

static int split_exprs(char *s, char out[MAXG][128]) {
    int g = 0;
    char *p = s;
    while (*p && g < MAXG) {
        char *bar = strchr(p, '|');
        if (bar) *bar = '\0';
        trim(p);
        if (*p) {
            snprintf(out[g], 128, "%s", p);
            g++;
        }
        if (!bar) break;
        p = bar + 1;
    }
    return g;
}

static char *dupstr(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static int cmp_int_asc(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

static void make_key(const Rec *r, char *key, int cap) {
    int a[MAXN];
    int n = r->n > MAXN ? MAXN : r->n;
    memcpy(a, r->nums, sizeof(int) * (size_t)n);
    qsort(a, (size_t)n, sizeof(int), cmp_int_asc);
    key[0] = '\0';
    int used = 0;
    for (int i = 0; i < n; i++) {
        char t[16];
        snprintf(t, sizeof(t), "%s%d", i ? "-" : "", a[i]);
        int tl = (int)strlen(t);
        if (used + tl >= cap - 1) break;
        memcpy(key + used, t, (size_t)tl);
        used += tl;
        key[used] = '\0';
    }
}

static void append_rec(const Rec *r) {
    if (nrec == caprec) {
        int nc = caprec ? caprec * 2 : 1024;
        Rec *nr = (Rec *)realloc(recs, (size_t)nc * sizeof(Rec));
        if (!nr) { printf("内存不足\n"); exit(1); }
        recs = nr;
        char (*nk)[320] = (char (*)[320])realloc(keys, (size_t)nc * 320);
        if (!nk) { printf("内存不足\n"); exit(1); }
        keys = nk;
        caprec = nc;
    }
    recs[nrec] = *r;
    make_key(r, keys[nrec], 320);
    nrec++;
}

/* 一行 -> 一条记录；返回 1 成功 */
static int parse_line(char *line) {
    if (strncmp(line, "id,tier,n,k,unused,nums", 23) == 0) return 0;   /* 表头 */
    char copy[MAX_LINE];
    snprintf(copy, sizeof(copy), "%s", line);
    char *fd[MAXF];
    int nf = split_csv(copy, fd, MAXF);
    if (nf < 19) return 0;

    Rec r;
    memset(&r, 0, sizeof(r));
    snprintf(r.tier, sizeof(r.tier), "%s", fd[1]);
    r.n = atoi(fd[2]);
    r.k = atoi(fd[3]);
    r.unused = atoi(fd[4]);
    if (r.n < 4 || r.n > MAXN || r.k < 1 || r.k > MAXG) return 0;

    int c = parse_ints(fd[5], r.nums, MAXN);
    if (c != r.n) return 0;

    r.sols = atoll(fd[6]);
    if (r.sols <= 0) return 0;                 /* 没解的不要 */
    r.truncated = atoi(fd[7]);

    r.ways  = atoll(fd[8]);
    r.davg  = atof(fd[10]);
    r.dmax  = atof(fd[11]);
    r.score = atof(fd[15]);
    r.grade = fd[16][0] ? fd[16][0] : '-';

    r.gk = split_groups(fd[17], r.gnums);
    split_exprs(fd[18], r.gexpr);
    if (r.gk == 0) return 0;                   /* 没答案的不要 */

    append_rec(&r);
    return 1;
}

static int load_file(const char *fname, int quiet) {
    FILE *f = fopen(fname, "r");
    if (!f) return -1;
    static char line[MAX_LINE];
    int first = 1, ok = 0;
    while (fgets(line, sizeof(line), f)) {
        if (first) { strip_bom(line); first = 0; }
        strip_eol(line);
        if (!line[0]) continue;
        if (parse_line(line)) ok++;
    }
    fclose(f);
    if (!quiet) printf("  %-22s 读入 %d 题\n", fname, ok);
    return ok;
}

static int is_ours(const char *fname) {
    FILE *f = fopen(fname, "r");
    if (!f) return 0;
    char head[128] = "";
    if (fgets(head, sizeof(head), f)) {
        strip_bom(head);
        strip_eol(head);
    }
    fclose(f);
    return strncmp(head, "id,tier,n,k,unused,nums", 23) == 0;
}

/* ---------------- 排序 / 取样 ---------------- */
static int cmp_score_desc_idx(const void *x, const void *y) {
    int a = *(const int *)x, b = *(const int *)y;
    double sa = recs[a].score, sb = recs[b].score;
    if (sa < sb) return 1;
    if (sa > sb) return -1;
    return 0;
}
static int cmp_key_idx(const void *x, const void *y) {
    int a = *(const int *)x, b = *(const int *)y;
    return strcmp(keys[a], keys[b]);
}

/* ---------------- 档位与评分（导出文件里的 meta 部分） ---------------- */
typedef struct {
    const char *key;      /* 档位内部名 */
    const char *label;    /* 显示名 */
    int size, groups, unused, base, multi;
} TierDef;

static const TierDef TIERS[] = {
    {"normal",   "普通",     4,  1, 0, 100,  1},
    {"hard",     "困难",     8,  2, 0, 250,  1},
    {"veryhard", "非常困难", 12, 3, 0, 450,  1},
    {"hardcore", "硬核",     16, 4, 0, 700,  0},
    {"extreme",  "极限",     20, 5, 0, 1000, 0},
    {"insane",   "疯狂",     24, 6, 0, 1350, 0},
    {"torment",  "折磨",     26, 6, 2, 1700, 0},
};
#define NT ((int)(sizeof(TIERS) / sizeof(TIERS[0])))

static void emit_meta(FILE *f) {
    fprintf(f, "  \"version\": 1,\n  \"target\": 9,\n  \"tiers\": [\n");
    for (int t = 0; t < NT; t++)
        fprintf(f, "    {\"key\":\"%s\",\"label\":\"%s\",\"size\":%d,\"groups\":%d,"
                   "\"unused\":%d,\"base\":%d,\"multiSolve\":%s}%s\n",
                TIERS[t].key, TIERS[t].label, TIERS[t].size, TIERS[t].groups,
                TIERS[t].unused, TIERS[t].base, TIERS[t].multi ? "true" : "false",
                (t == NT - 1) ? "" : ",");
    fprintf(f,
        "  ],\n"
        "  \"scoring\": {\n"
        "    \"formula\": \"最终分 = round(base * diffMult * timeMult * (1 + 多解加成 + 第二解加成 + 连击加成))\",\n"
        "    \"diffMult\": [0.85, 1.15],\n"
        "    \"time\": {\"parSecondsPerGroup\": 20, \"min\": 0.85, \"max\": 1.15},\n"
        "    \"partial\": {\"perGroupRate\": 0.6},\n"
        "    \"multiSolve\": {\"tiers\": [\"normal\", \"hard\", \"veryhard\"],\n"
        "                    \"normal\": {\"mode\": \"expressions\", \"extraRate\": 0.10, \"maxExtra\": 3},\n"
        "                    \"hard\": {\"mode\": \"partitions\", \"extraRate\": 0.15, \"maxExtra\": 3},\n"
        "                    \"veryhard\": {\"mode\": \"partitions\", \"extraRate\": 0.15, \"maxExtra\": 3}},\n"
        "    \"secondSolution\": {\"tiers\": [\"hardcore\", \"extreme\", \"insane\", \"torment\"], \"rate\": 0.15},\n"
        "    \"combo\": {\"step\": 0.05, \"max\": 0.25}\n"
        "  },\n"
        "  \"puzzles\": [\n");
}

static void emit_rec(FILE *f, const Rec *r, int seq, double p, int first, int multi) {
    if (!first) fprintf(f, ",\n");           /* 逗号写在条目【前面】，跨档也不会漏 */
    fprintf(f, "    {\"id\":\"%s-%03d\",\"tier\":\"%s\",\"size\":%d,\"groups\":%d,\"unused\":%d,\"nums\":[",
            r->tier, seq, r->tier, r->n, r->k, r->unused);
    for (int i = 0; i < r->n; i++) fprintf(f, "%s%d", i ? "," : "", r->nums[i]);
    fprintf(f, "]");
    if (multi) fprintf(f, ",\"sols\":%lld,\"ways\":%lld", r->sols, r->ways);
    if (!r->truncated) fprintf(f, ",\"score\":%.1f", r->score);
    fprintf(f, ",\"solutions\":[[");
    for (int g = 0; g < r->gk; g++) {
        if (g) fprintf(f, ",");
        fprintf(f, "{\"nums\":[");
        for (int i = 0; i < 4; i++) fprintf(f, "%s%d", i ? "," : "", r->gnums[g][i]);
        fprintf(f, "],\"expr\":\"%s\"}", r->gexpr[g][0] ? r->gexpr[g] : "");
    }
    fprintf(f, "]]}");
}

int main(int argc, char **argv) {
    const char *outbase = "puzzles";
    int per_tier = 200;
    const char *files[64];
    int nfiles = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-k") && i + 1 < argc) per_tier = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) outbase = argv[++i];
        else if (argv[i][0] == '-' && argv[i][1]) { printf("未知选项：%s\n", argv[i]); return 1; }
        else if (nfiles < 64) files[nfiles++] = argv[i];
    }
    if (per_tier < 1) per_tier = 1;

    printf("=== 导出网页题库 ===\n");
    if (nfiles == 0) {
#ifdef _WIN32
        FIND_T fd;
        intptr_t h = FIND_FIRST("*.csv", &fd);
        if (h != -1) {
            do {
                if (nfiles < 64 && is_ours(fd.name)) files[nfiles++] = dupstr(fd.name);
            } while (FIND_NEXT(h, &fd) == 0);
            FIND_CLOSE(h);
        }
#else
        DIR *d = opendir(".");
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) && nfiles < 64) {
                size_t l = strlen(e->d_name);
                if (l > 4 && !strcmp(e->d_name + l - 4, ".csv") && is_ours(e->d_name))
                    files[nfiles++] = dupstr(e->d_name);
            }
            closedir(d);
        }
#endif
        if (nfiles == 0) {
            printf("当前目录没有找到本程序的 CSV（表头应为 id,tier,n,k,unused,nums）。\n");
            printf("请把 normal.csv / hard.csv / ... / torment.csv 放在同一目录，或手动指定文件名。\n");
            return 1;
        }
        printf("自动认出 %d 个题库 CSV：\n", nfiles);
    }

    for (int i = 0; i < nfiles; i++) {
        int got = load_file(files[i], 0);
        if (got < 0) printf("  %-22s 打不开，跳过\n", files[i]);
    }
    if (nrec == 0) { printf("没有读到任何题目。\n"); return 1; }

    /* ---- 去重：同一组数字只留分数最高的那条 ---- */
    int *ord = (int *)malloc((size_t)nrec * sizeof(int));
    for (int i = 0; i < nrec; i++) ord[i] = i;
    qsort(ord, (size_t)nrec, sizeof(int), cmp_key_idx);
    char *dead = (char *)calloc((size_t)nrec, 1);
    int ndup = 0;
    for (int i = 0; i < nrec; ) {
        int j = i + 1;
        while (j < nrec && strcmp(keys[ord[i]], keys[ord[j]]) == 0) j++;
        if (j - i > 1) {
            int best = ord[i];
            for (int t = i + 1; t < j; t++)
                if (recs[ord[t]].score > recs[best].score) best = ord[t];
            for (int t = i; t < j; t++)
                if (ord[t] != best) { dead[ord[t]] = 1; ndup++; }
        }
        i = j;
    }
    printf("共读入 %d 题，去掉重复数字组 %d 题，剩 %d 题\n", nrec, ndup, nrec - ndup);

    /* ---- 按档输出 ---- */
    char jsname[300], jsonname[300];
    snprintf(jsname, sizeof(jsname), "%s.js", outbase);
    snprintf(jsonname, sizeof(jsonname), "%s.json", outbase);

    FILE *fj = fopen(jsname, "w");
    FILE *fo = fopen(jsonname, "w");
    if (!fj || !fo) { printf("无法创建输出文件\n"); return 1; }

    fprintf(fj, "/* 自动生成：由 mkjson.exe 从各档 scores CSV 导出，勿手改。\n");
    fprintf(fj, "   用 script 标签加载本文件后，页面里就能用 window.PUZZLE_DATA。\n");
    fprintf(fj, "   把第一行和最后一行的包装去掉，就是纯 JSON。*/\n");
    fprintf(fj, "window.PUZZLE_DATA =\n{\n");
    fprintf(fo, "{\n");
    emit_meta(fj);
    emit_meta(fo);

    int total_out = 0;
    int *sel = (int *)malloc((size_t)(nrec + 1) * sizeof(int));

    for (int t = 0; t < NT; t++) {
        /* 收集这一档 */
        int m = 0;
        for (int i = 0; i < nrec; i++)
            if (!dead[i] && strcmp(recs[i].tier, TIERS[t].key) == 0) sel[m++] = i;
        if (m == 0) continue;

        qsort(sel, (size_t)m, sizeof(int), cmp_score_desc_idx);   /* 分数从高到低 */

        /* 等间隔取样到 per_tier 个（保留两端） */
        int take = m < per_tier ? m : per_tier;
        printf("%-9s: 候选 %5d -> 导出 %d 题\n", TIERS[t].key, m, take);

        for (int j = 0; j < take; j++) {
            int idx;
            if (take == 1) idx = sel[0];
            else {
                double f = (double)j * (double)(m - 1) / (double)(take - 1);
                idx = sel[(int)(f + 0.5)];
            }
            int first = (total_out == 0);
            emit_rec(fj, &recs[idx], j + 1, 0.0, first, TIERS[t].multi);
            emit_rec(fo, &recs[idx], j + 1, 0.0, first, TIERS[t].multi);
            total_out++;
        }
    }

    fprintf(fj, "  ]\n};\n");
    fprintf(fo, "  ]\n}\n");
    fclose(fj);
    fclose(fo);

    printf("\n导出完成：%s / %s，共 %d 题\n", jsname, jsonname, total_out);
    printf("网页里这样用：\n");
    printf("  <script src=\"%s\"></script>\n", jsname);
    printf("  PUZZLES[i].nums       题目数字（打乱后显示给玩家）\n");
    printf("  PUZZLES[i].answer     参考答案：每项 {nums:[4个数], expr:\"算式\"}\n");
    printf("  PUZZLES[i].tier/p     难度档 / 档内百分位(1=最难)\n");

    free(ord); free(dead); free(sel); free(recs); free(keys);
    return 0;
}
