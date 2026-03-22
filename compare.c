#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINES 10000
#define MAX_LINE_LEN 4096

/* 读取文件所有行，返回行数 */
static int read_lines(FILE *fp, char lines[][MAX_LINE_LEN], int max_lines) {
    int count = 0;
    while (count < max_lines && fgets(lines[count], MAX_LINE_LEN, fp)) {
        count++;
    }
    return count;
}

/* 使用 LCS 动态规划计算最长公共子序列长度表 */
static int **build_lcs(char lines1[][MAX_LINE_LEN], int n1,
                        char lines2[][MAX_LINE_LEN], int n2) {
    int **dp = (int **)malloc((n1 + 1) * sizeof(int *));
    for (int i = 0; i <= n1; i++) {
        dp[i] = (int *)calloc(n2 + 1, sizeof(int));
    }
    for (int i = 1; i <= n1; i++) {
        for (int j = 1; j <= n2; j++) {
            if (strcmp(lines1[i - 1], lines2[j - 1]) == 0) {
                dp[i][j] = dp[i - 1][j - 1] + 1;
            } else {
                dp[i][j] = dp[i - 1][j] > dp[i][j - 1] ? dp[i - 1][j] : dp[i][j - 1];
            }
        }
    }
    return dp;
}

/* 递归回溯 LCS，输出差异到 file3 */
static void print_diff(FILE *file3, int **dp,
                        char lines1[][MAX_LINE_LEN], int i,
                        char lines2[][MAX_LINE_LEN], int j) {
    if (i == 0 && j == 0) return;

    if (i == 0) {
        print_diff(file3, dp, lines1, 0, lines2, j - 1);
        fprintf(file3, "+ %s", lines2[j - 1]);
    } else if (j == 0) {
        print_diff(file3, dp, lines1, i - 1, lines2, 0);
        fprintf(file3, "- %s", lines1[i - 1]);
    } else if (strcmp(lines1[i - 1], lines2[j - 1]) == 0) {
        print_diff(file3, dp, lines1, i - 1, lines2, j - 1);
        fprintf(file3, "  %s", lines1[i - 1]);
    } else if (dp[i - 1][j] >= dp[i][j - 1]) {
        print_diff(file3, dp, lines1, i - 1, lines2, j);
        fprintf(file3, "- %s", lines1[i - 1]);
    } else {
        print_diff(file3, dp, lines1, i, lines2, j - 1);
        fprintf(file3, "+ %s", lines2[j - 1]);
    }
}

/*
 * CompareFile - 比较 file1 与 file2 的文本差异，将变化写入 file3
 *   '  ' 开头：两者相同的行
 *   '- ' 开头：file1 中存在但 file2 中删除的行
 *   '+ ' 开头：file2 中新增的行
 */
void CompareFile(FILE *file1, FILE *file2, FILE *file3) {
    char (*lines1)[MAX_LINE_LEN] = malloc(MAX_LINES * MAX_LINE_LEN);
    char (*lines2)[MAX_LINE_LEN] = malloc(MAX_LINES * MAX_LINE_LEN);

    if (!lines1 || !lines2) {
        fprintf(stderr, "CompareFile: out of memory\n");
        free(lines1);
        free(lines2);
        return;
    }

    int n1 = read_lines(file1, lines1, MAX_LINES);
    int n2 = read_lines(file2, lines2, MAX_LINES);

    int **dp = build_lcs(lines1, n1, lines2, n2);

    print_diff(file3, dp, lines1, n1, lines2, n2);

    for (int i = 0; i <= n1; i++) free(dp[i]);
    free(dp);
    free(lines1);
    free(lines2);
}

/*
 * ApplyDiff - 将 diffFile 中的变化应用到 srcFile，输出到 destFile
 *
 * diffFile 格式（由 CompareFile 生成）：
 *   '  ' 开头：上下文行，需与 srcFile 当前行一致，原样写入 destFile
 *   '- ' 开头：srcFile 中被删除的行，需与 srcFile 当前行一致，不写入 destFile
 *   '+ ' 开头：新增行，直接写入 destFile，不消耗 srcFile
 *
 * 返回值：0 成功，非 0 失败
 *   -1  内存不足
 *   -2  diffFile 行格式非法
 *   -3  srcFile 内容与 diff 上下文 / 删除行不匹配
 *   -4  diff 处理完毕后 srcFile 仍有剩余行
 */
int ApplyDiff(FILE *destFile, FILE *srcFile, FILE *diffFile) {
    char diff_line[MAX_LINE_LEN];
    char src_line[MAX_LINE_LEN];

    while (fgets(diff_line, MAX_LINE_LEN, diffFile)) {
        char op = diff_line[0];

        if (op != ' ' && op != '-' && op != '+') {
            fprintf(stderr, "ApplyDiff: invalid diff line: %s", diff_line);
            return -2;
        }

        /* 实际内容在前缀 "X " 之后 */
        const char *content = diff_line + 2;

        if (op == '+') {
            /* 新增行：直接写入目标，不消耗源文件 */
            fputs(content, destFile);
        } else {
            /* 上下文行或删除行：从 srcFile 读取一行并校验 */
            if (!fgets(src_line, MAX_LINE_LEN, srcFile)) {
                fprintf(stderr, "ApplyDiff: srcFile ended unexpectedly\n");
                return -3;
            }
            if (strcmp(src_line, content) != 0) {
                fprintf(stderr, "ApplyDiff: srcFile mismatch\n"
                                "  expected: %s  got:      %s", content, src_line);
                return -3;
            }
            if (op == ' ') {
                /* 上下文行：原样保留 */
                fputs(src_line, destFile);
            }
            /* op == '-'：删除行，丢弃，不写入 destFile */
        }
    }

    /* diff 消耗完毕后，srcFile 不应有剩余行 */
    if (fgets(src_line, MAX_LINE_LEN, srcFile)) {
        fprintf(stderr, "ApplyDiff: srcFile has extra lines after diff\n");
        return -4;
    }

    return 0;
}

/* 简单测试入口 */
int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <file1> <file2> <output>\n", argv[0]);
        return 1;
    }
    FILE *f1 = fopen(argv[1], "r");
    FILE *f2 = fopen(argv[2], "r");
    FILE *f3 = fopen(argv[3], "w");
    if (!f1 || !f2 || !f3) {
        perror("fopen");
        return 1;
    }
    CompareFile(f1, f2, f3);
    fclose(f1); fclose(f2); fclose(f3);
    return 0;
}
