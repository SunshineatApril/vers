#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ── Commit 历史全局存储 ──────────────────────────────────────────
#define MAX_COMMITS 8192
#define HASH_LEN    41
#define AUTHOR_LEN  128
#define DATE_LEN    32
#define MSG_LEN     512

typedef struct {
    char hash[HASH_LEN];
    char author[AUTHOR_LEN];
    char date[DATE_LEN];
    char message[MSG_LEN];
} Commit;

Commit commit_log[MAX_COMMITS];
int    commit_count = 0;

// 从 URL 中提取仓库目录名（去掉路径前缀和 .git 后缀）
static void ExtractRepoName(const char *url, char *out, size_t size) {
    const char *start = strrchr(url, '/');
    start = start ? start + 1 : url;
    strncpy(out, start, size - 1);
    out[size - 1] = '\0';
    char *dot = strstr(out, ".git");
    if (dot) *dot = '\0';
}

// 读取指定仓库目录的全量 commit 历史，写入全局 commit_log[]
static int LoadCommitLog(const char *repo_dir) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" log --pretty=format:\"%%H\\t%%an\\t%%ad\\t%%s\" --date=short 2>nul",
        repo_dir);

    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    commit_count = 0;
    char line[HASH_LEN + AUTHOR_LEN + DATE_LEN + MSG_LEN + 8];
    while (fgets(line, sizeof(line), fp) && commit_count < MAX_COMMITS) {
        line[strcspn(line, "\n")] = '\0';

        char *hash   = strtok(line,  "\t");
        char *author = strtok(NULL,  "\t");
        char *date   = strtok(NULL,  "\t");
        char *msg    = strtok(NULL,  "\t");

        if (!hash) continue;

        strncpy(commit_log[commit_count].hash,    hash,                HASH_LEN   - 1);
        strncpy(commit_log[commit_count].author,  author ? author : "", AUTHOR_LEN - 1);
        strncpy(commit_log[commit_count].date,    date   ? date   : "", DATE_LEN   - 1);
        strncpy(commit_log[commit_count].message, msg    ? msg    : "", MSG_LEN    - 1);
        commit_count++;
    }

    pclose(fp);
    return commit_count;
}

// 将全局 commit_log[] 保存到文件（TSV 格式）
static int SaveCommitLog(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return -1; }
    for (int i = 0; i < commit_count; i++) {
        fprintf(f, "%s\t%s\t%s\t%s\n",
            commit_log[i].hash,
            commit_log[i].author,
            commit_log[i].date,
            commit_log[i].message);
    }
    fclose(f);
    return 0;
}

// 从文件加载 commit_log[]（TSV 格式）
static int LoadCommitLogFile(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    commit_count = 0;
    char line[HASH_LEN + AUTHOR_LEN + DATE_LEN + MSG_LEN + 8];
    while (fgets(line, sizeof(line), f) && commit_count < MAX_COMMITS) {
        line[strcspn(line, "\n")] = '\0';

        char *hash   = strtok(line, "\t");
        char *author = strtok(NULL, "\t");
        char *date   = strtok(NULL, "\t");
        char *msg    = strtok(NULL, "\t");

        if (!hash) continue;

        strncpy(commit_log[commit_count].hash,    hash,                HASH_LEN   - 1);
        strncpy(commit_log[commit_count].author,  author ? author : "", AUTHOR_LEN - 1);
        strncpy(commit_log[commit_count].date,    date   ? date   : "", DATE_LEN   - 1);
        strncpy(commit_log[commit_count].message, msg    ? msg    : "", MSG_LEN    - 1);
        commit_count++;
    }

    fclose(f);
    return commit_count;
}

// ── 子命令函数 ───────────────────────────────────────────────────
static int CmdDownload(const char *url) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "git clone %s", url);
    int ret = system(cmd);
    if (ret != 0) return ret;

    // clone 成功后加载 commit 历史到全局变量
    char repo_name[256];
    ExtractRepoName(url, repo_name, sizeof(repo_name));

    int n = LoadCommitLog(repo_name);
    if (n > 0) {
        char log_path[512];
        snprintf(log_path, sizeof(log_path), "commit.log", repo_name);
        SaveCommitLog(log_path);

        printf("Loaded %d commits from '%s':\n", n, repo_name);
        int preview = n < 5 ? n : 5;
        for (int i = 0; i < preview; i++) {
            printf("  [%d] %s  %s  %s  %s\n",
                i + 1,
                commit_log[i].hash,
                commit_log[i].date,
                commit_log[i].author,
                commit_log[i].message);
        }
        if (n > 5) printf("  ... (%d more)\n", n - 5);
    }

    return 0;
}

static int CmdUpload(const char *message) {
    int ret = system("git add .");
    if (ret != 0) return ret;

    // 检查相对远程分支是否已有 1 个或以上未推送的 commit
    FILE *fp = popen("git rev-list @{u}..HEAD --count 2>nul", "r");
    int ahead = 0;
    if (fp) {
        fscanf(fp, "%d", &ahead);
        pclose(fp);
    }

    char cmd[1024];
    if (ahead >= 1) {
        snprintf(cmd, sizeof(cmd), "git commit --amend -m \"%s\"", message);
    } else {
        snprintf(cmd, sizeof(cmd), "git commit -m \"%s\"", message);
    }
    ret = system(cmd);
    if (ret != 0) return ret;

    // 提交成功后，读取最新 commit 信息更���全局 commit_log
    FILE *lp = popen("git log -1 --pretty=format:\"%H\\t%an\\t%ad\\t%s\" --date=short 2>nul", "r");
    if (lp) {
        char line[HASH_LEN + AUTHOR_LEN + DATE_LEN + MSG_LEN + 8];
        if (fgets(line, sizeof(line), lp)) {
            line[strcspn(line, "\n")] = '\0';

            char *hash   = strtok(line, "\t");
            char *author = strtok(NULL, "\t");
            char *date   = strtok(NULL, "\t");
            char *msg    = strtok(NULL, "\t");

            if (hash) {
                Commit c;
                strncpy(c.hash,    hash,                HASH_LEN   - 1);
                strncpy(c.author,  author ? author : "", AUTHOR_LEN - 1);
                strncpy(c.date,    date   ? date   : "", DATE_LEN   - 1);
                strncpy(c.message, msg    ? msg    : "", MSG_LEN    - 1);

                if (ahead >= 1) {
                    // amend：替换 commit_log 第 0 条
                    commit_log[0] = c;
                } else {
                    // 新 commit：插入到 commit_log 头部
                    if (commit_count < MAX_COMMITS) commit_count++;
                    memmove(&commit_log[1], &commit_log[0], (commit_count - 1) * sizeof(Commit));
                    commit_log[0] = c;
                }
            }
        }
        pclose(lp);
    }

    // 更新持久化文件
    SaveCommitLog("commit.log");

    return 0;
}

static void PrintCommits(int from, int to) {
    for (int i = from; i < to; i++) {
        printf("  [%d] %s  %s  %s  %s\n",
            i + 1,
            commit_log[i].hash,
            commit_log[i].date,
            commit_log[i].author,
            commit_log[i].message);
    }
}

static int CmdGoto(const char *commit_id) {
    // 从持久化文件加载 commit 历史
    if (LoadCommitLogFile("commit.log") < 0 && commit_count == 0) {
        fprintf(stderr, "No commit history found. Run 'vers download' first.\n");
        return 1;
    }

    // 未传入 commit_id：展示 commit 列表供用户选择
    if (!commit_id) {
        if (commit_count == 0) {
            fprintf(stderr, "No commit history loaded. Run 'vers download' first.\n");
            return 1;
        }

        int show = commit_count < 10 ? commit_count : 10;
        printf("Recent %d commits:\n", show);
        PrintCommits(0, show);

        if (commit_count > 10) {
            printf("Show all %d commits? [y/N]: ", commit_count);
            char answer[8];
            if (fgets(answer, sizeof(answer), stdin) && (answer[0] == 'y' || answer[0] == 'Y')) {
                printf("All %d commits:\n", commit_count);
                PrintCommits(10, commit_count);
            }
        }

        return 0;
    }

    // 检测工作区是否有未提交的修改
    FILE *fp = popen("git status --porcelain 2>nul", "r");
    int has_changes = 0;
    if (fp) {
        char buf[4];
        has_changes = (fgets(buf, sizeof(buf), fp) != NULL);
        pclose(fp);
    }

    if (has_changes) {
        int ret = CmdUpload("auto save");
        if (ret != 0) return ret;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "git reset --hard %s", commit_id);
    return system(cmd);
}

// ── 入口 ────────────────────────────────────────────────────────��
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: vers <command> [args...]\n");
        return 1;
    }

    const char *arg = argc >= 3 ? argv[2] : NULL;

    if (strcmp(argv[1], "goto") == 0) return CmdGoto(arg);

    if (!arg) {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        return 1;
    }

    if (strcmp(argv[1], "download") == 0) return CmdDownload(arg);
    if (strcmp(argv[1], "upload")   == 0) return CmdUpload(arg);

    fprintf(stderr, "Unknown command: %s\n", argv[1]);
    return 1;
}
