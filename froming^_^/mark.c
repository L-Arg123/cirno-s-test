#include <stdio.h>
#include <string.h>

// 判断 4 个数字仅用加减能否算出 9
int can_add_sub_only(int a, int b, int c, int d) {
    // 枚举每个数字的符号：0 表示正，1 表示负
    for (int mask = 0; mask < 16; mask++) {
        int sum = 0;
        sum += (mask & 1) ? -a : a;
        sum += (mask & 2) ? -b : b;
        sum += (mask & 4) ? -c : c;
        sum += (mask & 8) ? -d : d;
        if (sum == 9) return 1;
    }
    return 0;
}

int main(void) {
    FILE *fin = fopen("solutions_filtered.csv", "r");
    if (!fin) {
        perror("solutions_filtered.csv");
        return 1;
    }

    FILE *fout = fopen("solutions_marked.csv", "w");
    if (!fout) {
        perror("solutions_marked.csv");
        fclose(fin);
        return 1;
    }

    char line[65536];

    // 表头原样输出
    if (fgets(line, sizeof(line), fin)) {
        fputs(line, fout);
    }

    int total = 0;
    int marked = 0;

    while (fgets(line, sizeof(line), fin)) {
        // 去掉行尾换行
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') continue;

        // 找第 5 个和第 6 个逗号
        // 字段结构：num1,num2,num3,num4,count,expressions,,T
        // 逗号编号：0 后、1 后、2 后、3 后、4 后、5 后、6 后
        char *p = line;
        int comma = 0;
        char *pos5 = NULL; // 第 5 个逗号（expressions 后面的逗号）
        char *pos6 = NULL; // 第 6 个逗号（空字段后面的逗号）

        while (*p) {
            if (*p == ',') {
                comma++;
                if (comma == 5) pos5 = p;
                if (comma == 6) {
                    pos6 = p;
                    break;
                }
            }
            p++;
        }

        // 格式不对就原样输出
        if (comma < 6 || !pos5 || !pos6) {
            fprintf(fout, "%s\n", line);
            continue;
        }

        // 读取前 4 个数字
        int a, b, c, d;
        if (sscanf(line, "%d,%d,%d,%d", &a, &b, &c, &d) != 4) {
            fprintf(fout, "%s\n", line);
            continue;
        }

        // 读取原来的 T 值
        int old_T = 0;
        if (pos6[1] == '1') {
            old_T = 1;
        }

        // 如果原来没标 1，则自动判断
        int new_T = old_T;
        if (!old_T) {
            if (can_add_sub_only(a, b, c, d)) {
                new_T = 1;
            }
        }

        // 输出前 6 个字段（到第 5 个逗号为止）
        fwrite(line, 1, pos5 - line + 1, fout);

        // 补上空字段和 T 列
        fprintf(fout, ",%s\n", new_T ? "1" : "");

        total++;
        if (new_T) marked++;
    }

    fclose(fin);
    fclose(fout);

    printf("处理完成！\n");
    printf("总行数：%d\n", total);
    printf("T=1 的行数：%d\n", marked);
    printf("结果已保存到 solutions_marked.csv\n");

    return 0;
}