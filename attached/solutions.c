#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define EPS 1e-6
#define MAX_SOL 500

// ================= 运算符定义 =================
typedef double (*OpFunc)(double, double);

double op_add(double a, double b) { return a + b; }
double op_sub(double a, double b) { return a - b; }
double op_mul(double a, double b) { return a * b; }
double op_div(double a, double b) { return b == 0.0 ? NAN : a / b; }

OpFunc ops[] = { op_add, op_sub, op_mul, op_div };
const char *op_sym[] = { "+", "-", "*", "/" };
#define NUM_OPS 4

// ================= 当前组合的解集 =================
char sol_strings[MAX_SOL][128];
int sol_count = 0;

void add_solution(const char *expr) {
    for (int i = 0; i < sol_count; i++) {
        if (strcmp(sol_strings[i], expr) == 0) return;
    }
    if (sol_count < MAX_SOL) {
        strcpy(sol_strings[sol_count], expr);
        sol_count++;
    }
}

// ================= 核心计算：5种括号结构 =================
void evaluate(int p0, int p1, int p2, int p3, int o0, int o1, int o2) {
    OpFunc f0 = ops[o0], f1 = ops[o1], f2 = ops[o2];
    char expr[128];

    // 结构 1: ((a op b) op c) op d
    {
        double r1 = f0(p0, p1);
        if (!isnan(r1)) {
            double r2 = f1(r1, p2);
            if (!isnan(r2)) {
                double r3 = f2(r2, p3);
                if (!isnan(r3) && fabs(r3 - 9.0) < EPS) {
                    snprintf(expr, sizeof(expr), "((%d%s%d)%s%d)%s%d",
                             p0, op_sym[o0], p1, op_sym[o1], p2, op_sym[o2], p3);
                    add_solution(expr);
                }
            }
        }
    }
    // 结构 2: (a op (b op c)) op d
    {
        double r1 = f1(p1, p2);
        if (!isnan(r1)) {
            double r2 = f0(p0, r1);
            if (!isnan(r2)) {
                double r3 = f2(r2, p3);
                if (!isnan(r3) && fabs(r3 - 9.0) < EPS) {
                    snprintf(expr, sizeof(expr), "(%d%s(%d%s%d))%s%d",
                             p0, op_sym[o0], p1, op_sym[o1], p2, op_sym[o2], p3);
                    add_solution(expr);
                }
            }
        }
    }
    // 结构 3: a op ((b op c) op d)
    {
        double r1 = f1(p1, p2);
        if (!isnan(r1)) {
            double r2 = f2(r1, p3);
            if (!isnan(r2)) {
                double r3 = f0(p0, r2);
                if (!isnan(r3) && fabs(r3 - 9.0) < EPS) {
                    snprintf(expr, sizeof(expr), "%d%s((%d%s%d)%s%d)",
                             p0, op_sym[o0], p1, op_sym[o1], p2, op_sym[o2], p3);
                    add_solution(expr);
                }
            }
        }
    }
    // 结构 4: a op (b op (c op d))
    {
        double r1 = f2(p2, p3);
        if (!isnan(r1)) {
            double r2 = f1(p1, r1);
            if (!isnan(r2)) {
                double r3 = f0(p0, r2);
                if (!isnan(r3) && fabs(r3 - 9.0) < EPS) {
                    snprintf(expr, sizeof(expr), "%d%s(%d%s(%d%s%d))",
                             p0, op_sym[o0], p1, op_sym[o1], p2, op_sym[o2], p3);
                    add_solution(expr);
                }
            }
        }
    }
    // 结构 5: (a op b) op (c op d)
    {
        double r1 = f0(p0, p1);
        if (!isnan(r1)) {
            double r2 = f2(p2, p3);
            if (!isnan(r2)) {
                double r3 = f1(r1, r2);
                if (!isnan(r3) && fabs(r3 - 9.0) < EPS) {
                    snprintf(expr, sizeof(expr), "(%d%s%d)%s(%d%s%d)",
                             p0, op_sym[o0], p1, op_sym[o1], p2, op_sym[o2], p3);
                    add_solution(expr);
                }
            }
        }
    }
}

// ================= 全排列 =================
void permute(int *nums, int *used, int *cur, int depth, int *op_indices) {
    if (depth == 4) {
        evaluate(cur[0], cur[1], cur[2], cur[3],
                 op_indices[0], op_indices[1], op_indices[2]);
        return;
    }
    for (int i = 0; i < 4; i++) {
        if (used[i]) continue;
        if (i > 0 && nums[i] == nums[i-1] && !used[i-1]) continue;
        used[i] = 1;
        cur[depth] = nums[i];
        permute(nums, used, cur, depth + 1, op_indices);
        used[i] = 0;
    }
}

// ================= 主函数 =================
int main(void) {
    // 输出文件名改为 solutions.csv
    FILE *fp = fopen("solutions.csv", "w");
    if (!fp) { perror("fopen"); return 1; }

    fprintf(fp, "num1,num2,num3,num4,count,expressions\n");

    int nums[4];
    int cur[4], used[4];
    int op_indices[3];
    int total_count = 0;

    // 范围扩展到 1~18
    for (nums[0] = 1; nums[0] <= 18; nums[0]++) {
        if (nums[0] == 13 || nums[0] == 17) continue; // 排除 13 和 17
        for (nums[1] = nums[0]; nums[1] <= 18; nums[1]++) {
            if (nums[1] == 13 || nums[1] == 17) continue;
            for (nums[2] = nums[1]; nums[2] <= 18; nums[2]++) {
                if (nums[2] == 13 || nums[2] == 17) continue;
                for (nums[3] = nums[2]; nums[3] <= 18; nums[3]++) {
                    if (nums[3] == 13 || nums[3] == 17) continue;

                    sol_count = 0; // 清空解集

                    // 枚举所有运算符组合
                    for (op_indices[0] = 0; op_indices[0] < NUM_OPS; op_indices[0]++)
                    for (op_indices[1] = 0; op_indices[1] < NUM_OPS; op_indices[1]++)
                    for (op_indices[2] = 0; op_indices[2] < NUM_OPS; op_indices[2]++) {
                        memset(used, 0, sizeof(used));
                        permute(nums, used, cur, 0, op_indices);
                    }

                    // 如果有解，写入 solutions.csv (这就是查表字典)
                    if (sol_count > 0) {
                        total_count++;
                        fprintf(fp, "%d,%d,%d,%d,%d,",
                                nums[0], nums[1], nums[2], nums[3], sol_count);
                        for (int i = 0; i < sol_count; i++) {
                            if (i > 0) fprintf(fp, ";");
                            fprintf(fp, "%s", sol_strings[i]);
                        }
                        fprintf(fp, "\n");
                    }
                }
            }
        }
    }

    fclose(fp);
    printf("Done! 结果已保存到 solutions.csv\n");
    printf("共生成 %d 组有解的4数组合（已排除13和17）。\n", total_count);
    return 0;
}