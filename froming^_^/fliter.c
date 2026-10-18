#include <stdio.h>
#include <string.h>

int main() {
    // 1. 打开输入文件
    FILE *fin = fopen("solutions.csv", "r");
    if (!fin) {
        printf("错误: 无法打开 solutions.csv，请检查文件是否存在！\n");
        return 1;
    }

    // 2. 创建输出文件
    FILE *fout = fopen("solutions_filtered.csv", "w");
    if (!fout) {
        printf("错误: 无法创建 solutions_filtered.csv！\n");
        fclose(fin);
        return 1;
    }

    // 准备一个足够大的缓冲区来存储每一行
    // 表达式可能很长，4096 字节绝对够用了
    char line[4096];
    
    // 读取并写入表头
    if (fgets(line, sizeof(line), fin)) {
        fputs(line, fout);
    }

    // 用于解析数据的变量
    int num1, num2, num3, num4, count;
    int kept_count = 0;    // 保留的行数
    int deleted_count = 0; // 删除的行数

    // 3. 逐行读取并过滤
    while (fgets(line, sizeof(line), fin)) {
        // 解析前5个字段（我们不需要解析后面的算式内容，直接整行保留或丢弃）
        // sscanf 遇到逗号会按格式解析，解析完5个数字后就会停止
        if (sscanf(line, "%d,%d,%d,%d,%d", &num1, &num2, &num3, &num4, &count) >= 5) {
            
            // 核心过滤逻辑：种类大于等于 100 的组删除
            if (count >= 100) {
                deleted_count++;
                // 不写入 fout，直接跳过（丢弃）
            } else {
                // 满足条件，将完整的原始行写入新文件
                fputs(line, fout);
                kept_count++;
            }
        }
    }

    // 4. 关闭文件并打印统计信息
    fclose(fin);
    fclose(fout);

    printf("筛选完成！\n");
    printf("保留的组合数 (count < 100): %d\n", kept_count);
    printf("删除的组合数 (count >= 100): %d\n", deleted_count);
    printf("结果已保存到 solutions_filtered.csv\n");

    return 0;
}