#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <direct.h>  // _getcwd (Windows)

// ── Commit 历史全局存储 ──────────────────────────────────────────
#define MAX_COMMITS      8192
#define HASH_LEN           64
#define AUTHOR_LEN         64
#define DATE_LEN           32
#define MSG_LEN           512
#define LOG_LINE_SEP        8   // TSV 行缓冲额外空间（制表符/换��/终止符）

// ── 路径与命令缓冲 ───────────────────────────────────────────────
#define ROOT_DIR_LEN      256   // 根目录路径缓冲
#define REPO_NAME_LEN     128   // 仓库子目录名缓冲
#define PATH_LEN          512   // 完整路径缓冲（root + "/" + name）
#define CMD_LEN           512   // git 命令字符串缓冲

// ── 配置文件解析 ─────────────────────────────────────────────────
#define CFG_KEY_LEN         5   // "root=" / "repo=" 键长度
#define LOG_FILE        "commit.log"
#define LOG_LINE_LEN    (HASH_LEN + AUTHOR_LEN + DATE_LEN + MSG_LEN + LOG_LINE_SEP)

// ── 显示参数 ─────────────────────────────────────────────────────
#define DOWNLOAD_PREVIEW   10   // download 后预览的 commit 条数
#define GOTO_LIST_MAX      10   // goto 无参数时直接显示的最大条数
#define ANSWER_LEN          8   // y/N 交互输入缓冲
#define STATUS_PEEK_LEN     4   // git status --porcelain 输出探测缓冲

typedef struct {
    char hash[HASH_LEN];
    char author[AUTHOR_LEN];
    char date[DATE_LEN];
    char message[MSG_LEN];
} Commit;

Commit commit_log[MAX_COMMITS];
int    commit_count = 0;

// ── 根目录配置 ───────────────────────────────────────────────────
#define CFG_FILE "vers.cfg"

static char g_root_dir[ROOT_DIR_LEN]   = "";  // CMD_Download 时保存的根目录
static char g_repo_name[REPO_NAME_LEN] = "";  // clone 下来的仓库子目录名

// 将根目录和仓库名写入 <root>/vers.cfg
static int INR_SaveVersConfig(const char *root, const char *repo) {
    char path[PATH_LEN];
    snprintf(path, sizeof(path), "%s/%s", root, CFG_FILE);
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return -1; }
    fprintf(f, "root=%s\nrepo=%s\n", root, repo);
    fclose(f);
    return 0;
}

// 搜索并加载 vers.cfg：先查当前目录，再查上一级目录
static int INR_LoadVersConfig(void) {
    FILE *f = fopen(CFG_FILE, "r");
    if (!f) {
        f = fopen("../" CFG_FILE, "r");
    }
    if (!f) {
        fprintf(stderr, "vers.cfg not found. Run 'vers download' first.\n");
        return -1;
    }

    char line[PATH_LEN];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "root=", CFG_KEY_LEN) == 0)
            strncpy(g_root_dir,  line + CFG_KEY_LEN, sizeof(g_root_dir)  - 1);
        else if (strncmp(line, "repo=", CFG_KEY_LEN) == 0)
            strncpy(g_repo_name, line + CFG_KEY_LEN, sizeof(g_repo_name) - 1);
    }
    fclose(f);

    if (!g_root_dir[0] || !g_repo_name[0]) {
        fprintf(stderr, "vers.cfg is incomplete.\n");
        return -1;
    }
    return 0;
}

// 从 URL 中提取仓库目录名（去掉路径前缀和 .git 后缀）
static void INR_ExtractRepoName(const char *url, char *out, size_t size) {
    const char *start = strrchr(url, '/');
    start = start ? start + 1 : url;
    strncpy(out, start, size - 1);
    out[size - 1] = '\0';
    char *dot = strstr(out, ".git");
    if (dot) *dot = '\0';
}

// 读取指定仓库目录的全量 commit 历史，写入全局 commit_log[]
static int INR_LoadCommitLog(const char *repo_dir) {
    char cmd[CMD_LEN];
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" log --pretty=format:\"%%H%%x09%%an%%x09%%ad%%x09%%s\" --date=short 2>nul",
        repo_dir);

    commit_count = 0;
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    char line[LOG_LINE_LEN];
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
static int INR_SaveCommitLog(const char *path) {
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
static int INR_LoadCommitLogFile(const char *path) {
    commit_count = 0;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[LOG_LINE_LEN];
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

// download：克隆仓库，保存根目录配置，写入 commit.log
static int CMD_Download(const char *url) {
    // 记录当前目录为根目录
    char root[ROOT_DIR_LEN];
    if (!_getcwd(root, sizeof(root))) {
        perror("getcwd");
        return -1;
    }

    char cmd[CMD_LEN];
    snprintf(cmd, sizeof(cmd), "git clone %s", url);
    int ret = system(cmd);
    if (ret != 0) return ret;

    char repo_name[REPO_NAME_LEN];
    INR_ExtractRepoName(url, repo_name, sizeof(repo_name));

    // 保存配置到根目录
    INR_SaveVersConfig(root, repo_name);

    // 从克隆的仓库加载 commit 历史，保存到根目录的 commit.log
    char repo_path[PATH_LEN];
    snprintf(repo_path, sizeof(repo_path), "%s/%s", root, repo_name);

    int n = INR_LoadCommitLog(repo_path);
    if (n > 0) {
        char log_path[PATH_LEN];
        snprintf(log_path, sizeof(log_path), "%s/" LOG_FILE, root);
        INR_SaveCommitLog(log_path);

        printf("Loaded %d commits from '%s':\n", n, repo_name);
        int preview = n < DOWNLOAD_PREVIEW ? n : DOWNLOAD_PREVIEW;
        for (int i = 0; i < preview; i++) {
            printf("  [%d] %s  %s  %s  %s\n",
                i + 1,
                commit_log[i].hash,
                commit_log[i].date,
                commit_log[i].author,
                commit_log[i].message);
        }
        if (n > DOWNLOAD_PREVIEW) printf("  ... (%d more)\n", n - DOWNLOAD_PREVIEW);
    }

    return 0;
}

// 保存修改到本地仓库，更新 commit.log
static int INR_SaveModify(const char *message) {
    if (INR_LoadVersConfig() < 0) return 1;

    char repo_path[PATH_LEN];
    snprintf(repo_path, sizeof(repo_path), "%s/%s", g_root_dir, g_repo_name);

    char commit_log_path[PATH_LEN];
    snprintf(commit_log_path, sizeof(commit_log_path), "%s/" LOG_FILE, g_root_dir);

    char cmd[CMD_LEN];

    // git add
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" add .", repo_path);
    int ret = system(cmd);
    if (ret != 0) return ret;

    // 检查相对远程分支是否已有未推送的 commit
    char rev_cmd[CMD_LEN];
    snprintf(rev_cmd, sizeof(rev_cmd),
        "git -C \"%s\" rev-list @{u}..HEAD --count 2>nul", repo_path);
    FILE *fp = popen(rev_cmd, "r");
    int ahead = 0;
    if (fp) {
        fscanf(fp, "%d", &ahead);
        pclose(fp);
    }

    if (ahead >= 1) {
        snprintf(cmd, sizeof(cmd), "git -C \"%s\" commit --amend -m \"%s\"", repo_path, message);
    } else {
        snprintf(cmd, sizeof(cmd), "git -C \"%s\" commit -m \"%s\"", repo_path, message);
    }
    ret = system(cmd);
    if (ret != 0) return ret;

    // 提交成功后，读取最新 commit 信息更新全局 commit_log
    char log_cmd[CMD_LEN];
    snprintf(log_cmd, sizeof(log_cmd),
        "git -C \"%s\" log -1 --pretty=format:\"%%H%%x09%%an%%x09%%ad%%x09%%s\" --date=short 2>nul",
        repo_path);
    FILE *lp = popen(log_cmd, "r");
    if (lp) {
        char line[LOG_LINE_LEN];
        if (fgets(line, sizeof(line), lp)) {
            line[strcspn(line, "\n")] = '\0';

            char *hash   = strtok(line, "\t");
            char *author = strtok(NULL, "\t");
            char *date   = strtok(NULL, "\t");
            char *msg    = strtok(NULL, "\t");

            if (hash) {
                Commit c = {0};
                strncpy(c.hash,    hash,                HASH_LEN   - 1);
                strncpy(c.author,  author ? author : "", AUTHOR_LEN - 1);
                strncpy(c.date,    date   ? date   : "", DATE_LEN   - 1);
                strncpy(c.message, msg    ? msg    : "", MSG_LEN    - 1);

                // 先从根目录加载现有 commit.log
                INR_LoadCommitLogFile(commit_log_path);

                if (ahead >= 1) {
                    commit_log[0] = c;
                } else {
                    if (commit_count < MAX_COMMITS) commit_count++;
                    memmove(&commit_log[1], &commit_log[0], (commit_count - 1) * sizeof(Commit));
                    commit_log[0] = c;
                }
            }
        }
        pclose(lp);
    }

    // 更新根目录的 commit.log
    INR_SaveCommitLog(commit_log_path);

    return 0;
}

// upload：保存修改并推送到远程仓库
static int CMD_Upload(const char *message) {
    int ret = INR_SaveModify(message);
    if (ret != 0) return ret;

    char repo_path[PATH_LEN];
    snprintf(repo_path, sizeof(repo_path), "%s/%s", g_root_dir, g_repo_name);

    char cmd[CMD_LEN];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" push", repo_path);
    return system(cmd);
}

static void INR_PrintCommits(int from, int to) {
    for (int i = from; i < to; i++) {
        printf("  [%d] %s  %s  %s  %s\n",
            i + 1,
            commit_log[i].hash,
            commit_log[i].date,
            commit_log[i].author,
            commit_log[i].message);
    }
}

// goto：从根目录加载 commit.log，在仓库子目录中执行 git 操作
static int CMD_Goto(const char *commit_id) {
    if (INR_LoadVersConfig() < 0) return 1;

    char repo_path[PATH_LEN];
    snprintf(repo_path, sizeof(repo_path), "%s/%s", g_root_dir, g_repo_name);

    char commit_log_path[PATH_LEN];
    snprintf(commit_log_path, sizeof(commit_log_path), "%s/" LOG_FILE, g_root_dir);

    // 从根目录加载 commit 历史
    INR_LoadCommitLogFile(commit_log_path);
    if (commit_count == 0) {
        fprintf(stderr, "No commit history found in '%s'.\n", commit_log_path);
        return 1;
    }

    // 未传入 commit_id：展示 commit 列表供用户选择
    if (!commit_id) {
        int show = commit_count < GOTO_LIST_MAX ? commit_count : GOTO_LIST_MAX;
        printf("Recent %d commits:\n", show);
        INR_PrintCommits(0, show);

        if (commit_count > GOTO_LIST_MAX) {
            printf("Show all %d commits? [y/N]: ", commit_count);
            char answer[ANSWER_LEN];
            if (fgets(answer, sizeof(answer), stdin) && (answer[0] == 'y' || answer[0] == 'Y')) {
                printf("All %d commits:\n", commit_count);
                INR_PrintCommits(GOTO_LIST_MAX, commit_count);
            }
        }

        return 0;
    }

    // 检测仓库工作区是否有未提交的修改
    char stat_cmd[CMD_LEN];
    snprintf(stat_cmd, sizeof(stat_cmd),
        "git -C \"%s\" status --porcelain 2>nul", repo_path);
    FILE *fp = popen(stat_cmd, "r");
    int has_changes = 0;
    if (fp) {
        char buf[STATUS_PEEK_LEN];
        has_changes = (fgets(buf, sizeof(buf), fp) != NULL);
        pclose(fp);
    }

    if (has_changes) {
        int ret = INR_SaveModify("auto save");
        if (ret != 0) return ret;
    }

    char cmd[CMD_LEN];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" reset --hard %s", repo_path, commit_id);
    return system(cmd);
}

// ── 入口 ─────────────────────────────────────────────────────────
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: vers <command> [args...]\n");
        return 1;
    }

    const char *arg = argc >= 3 ? argv[2] : NULL;

    if (strcmp(argv[1], "goto")     == 0) return CMD_Goto(arg);

    if (strcmp(argv[1], "download") == 0) {
        if (!arg) { fprintf(stderr, "Usage: vers download <url>\n"); return 1; }
        return CMD_Download(arg);
    }
    if (strcmp(argv[1], "upload")   == 0) {
        if (!arg) { fprintf(stderr, "Usage: vers upload <message>\n"); return 1; }
        return CMD_Upload(arg);
    }

    fprintf(stderr, "Unknown command: %s\n", argv[1]);
    return 1;
}
