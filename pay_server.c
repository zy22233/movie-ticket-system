/*
 * 电影票自助售票机 —— 云端支付/充值服务器
 *
 * 配合客户端 UI/ui_event_tmp.c 里的 charge1cloud() 使用。
 *
 * 协议（每条命令以 '\n' 结尾，服务器逐条应答）：
 *   客户端发送                          服务器回复
 *   -------------------------------   ----------------------------------
 *   PAY:用户名:金额\n                  扣款成功回 "OK"，余额不足回 "NO"
 *   RECHARGE:用户名:金额\n             充值成功回 "OK"
 *   BALANCE:用户名\n                   回复 "BAL:当前余额"
 *   SYNC:用户名:密码\n                 账号备份成功回 "OK"（各板子注册的账号都存一份）
 *   ACCS\n                             逐行回 "ACC:用户名:密码"，最后回 "ACC_END"
 *   其他任何内容                        回复 "ERR"
 *
 * 余额表保存在服务器同目录的 balance.txt（每行 "用户名 余额"），
 * 每次扣款/充值后立即写盘，服务器重启不丢数据。
 * 新用户第一次出现时自动开户，默认余额 1000 元（改 DEFAULT_BALANCE）。
 *
 * 各售票机注册的账号会通过 SYNC 备份到 server_accounts.txt（每行 "用户名 密码"），
 * 其他板子用 ACCS 拉取后即可在本机登录已注册过的账号，实现多机共享账号。
 *
 * 编译运行（在云服务器上）：
 *   gcc pay_server.c -o pay_server
 *   ./pay_server            # 默认监听 8888 端口
 *   ./pay_server 9000       # 或指定端口
 *
 * 客户端那边把 ui_event_tmp.c 顶部的 PAY_SERVER_IP 改成这台服务器的 IP、
 * PAY_SERVER_PORT 改成对应端口即可。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define DEFAULT_PORT    8888
#define DEFAULT_BALANCE 1000        //新用户默认余额（元）
#define MAX_USERS       256         //最多保存的用户数
#define BALANCE_FILE    "balance.txt"
#define ACCOUNTS_FILE   "server_accounts.txt"   //各板子注册账号的备份文件

//余额表
struct account {
    char name[64];
    int  balance;
};
static struct account g_accounts[MAX_USERS];
static int g_account_count = 0;

//账号备份表（用户名 密码）
struct acc_info {
    char name[64];
    char pass[64];
};
static struct acc_info g_backup_accs[MAX_USERS];
static int g_backup_count = 0;

//启动时从 balance.txt 加载余额表
static void load_balances(void)
{
    FILE *fp = fopen(BALANCE_FILE, "r");
    if (fp == NULL) {
        printf("提示：%s 不存在，全部用户按新户处理（默认 %d 元）\n",
               BALANCE_FILE, DEFAULT_BALANCE);
        return;
    }

    char line[128];
    while (fgets(line, sizeof(line), fp) != NULL && g_account_count < MAX_USERS) 
    {
        line[strcspn(line, "\n")] = 0;
        char name[64] = {0};
        int balance = 0;
        if (sscanf(line, "%63s %d", name, &balance) == 2) 
        {
            snprintf(g_accounts[g_account_count].name,
                     sizeof(g_accounts[g_account_count].name), "%s", name);
            g_accounts[g_account_count].balance = balance;
            g_account_count++;
        }
    }
    fclose(fp);
    printf("已从 %s 加载 %d 个用户的余额\n", BALANCE_FILE, g_account_count);
}

//把余额表写回 balance.txt
static void save_balances(void)
{
    FILE *fp = fopen(BALANCE_FILE, "w");
    if (fp == NULL) {
        printf("错误：无法写 %s\n", BALANCE_FILE);
        return;
    }
    for (int i = 0; i < g_account_count; i++) {
        fprintf(fp, "%s %d\n", g_accounts[i].name, g_accounts[i].balance);
    }
    fclose(fp);
}

//查找用户，没有则自动开户（默认余额）
static struct account *find_or_create(const char *name)
{
    for (int i = 0; i < g_account_count; i++) {
        if (strcmp(g_accounts[i].name, name) == 0) {
            return &g_accounts[i];
        }
    }
    if (g_account_count >= MAX_USERS) {
        return NULL;
    }
    snprintf(g_accounts[g_account_count].name,
             sizeof(g_accounts[g_account_count].name), "%s", name);
    g_accounts[g_account_count].balance = DEFAULT_BALANCE;
    printf("新用户开户：%s，默认余额 %d 元\n", name, DEFAULT_BALANCE);
    return &g_accounts[g_account_count++];
}

//启动时从 server_accounts.txt 加载账号备份表
static void load_backup_accounts(void)
{
    FILE *fp = fopen(ACCOUNTS_FILE, "r");
    if (fp == NULL) {
        printf("提示：%s 不存在，还没有任何备份账号\n", ACCOUNTS_FILE);
        return;
    }

    char line[160];
    while (fgets(line, sizeof(line), fp) != NULL && g_backup_count < MAX_USERS)
    {
        line[strcspn(line, "\n")] = 0;
        char name[64] = {0}, pass[64] = {0};
        if (sscanf(line, "%63s %63s", name, pass) == 2)
        {
            snprintf(g_backup_accs[g_backup_count].name,
                     sizeof(g_backup_accs[g_backup_count].name), "%s", name);
            snprintf(g_backup_accs[g_backup_count].pass,
                     sizeof(g_backup_accs[g_backup_count].pass), "%s", pass);
            g_backup_count++;
        }
    }
    fclose(fp);
    printf("已从 %s 加载 %d 个备份账号\n", ACCOUNTS_FILE, g_backup_count);
}

//把账号备份表写回 server_accounts.txt
static void save_backup_accounts(void)
{
    FILE *fp = fopen(ACCOUNTS_FILE, "w");
    if (fp == NULL) {
        printf("错误：无法写 %s\n", ACCOUNTS_FILE);
        return;
    }
    for (int i = 0; i < g_backup_count; i++) {
        fprintf(fp, "%s %s\n", g_backup_accs[i].name, g_backup_accs[i].pass);
    }
    fclose(fp);
}

//备份/更新一个账号（已存在则刷新密码）
static void upsert_backup_account(const char *name, const char *pass)
{
    for (int i = 0; i < g_backup_count; i++) {
        if (strcmp(g_backup_accs[i].name, name) == 0) {
            snprintf(g_backup_accs[i].pass, sizeof(g_backup_accs[i].pass), "%s", pass);
            return;
        }
    }
    if (g_backup_count >= MAX_USERS) {
        printf("错误：备份账号数已达上限 %d\n", MAX_USERS);
        return;
    }
    snprintf(g_backup_accs[g_backup_count].name,
             sizeof(g_backup_accs[g_backup_count].name), "%s", name);
    snprintf(g_backup_accs[g_backup_count].pass,
             sizeof(g_backup_accs[g_backup_count].pass), "%s", pass);
    g_backup_count++;
}

//处理一条命令，把应答写进 reply
static void handle_command(const char *cmd, char *reply, int reply_size)
{
    char op[16] = {0}, user[64] = {0};
    int amount = 0;

    //ACCS：板子拉取全部备份账号，逐行回 ACC:用户名:密码，最后回 ACC_END
    if (strcmp(cmd, "ACCS") == 0) {
        int off = 0;
        for (int i = 0; i < g_backup_count; i++) {
            off += snprintf(reply + off, reply_size - off,
                            "ACC:%s:%s\n", g_backup_accs[i].name, g_backup_accs[i].pass);
            if (off >= reply_size - 16) break;
        }
        snprintf(reply + off, reply_size - off, "ACC_END\n");
        printf("[账号同步] 下发 %d 个备份账号\n", g_backup_count);
        return;
    }

    //SYNC:用户名:密码：注册成功后把账号备份到服务器
    char op2[16] = {0}, user2[64] = {0}, pass2[64] = {0};
    if (sscanf(cmd, "%15[^:]:%63[^:]:%63s", op2, user2, pass2) == 3 &&
        strcmp(op2, "SYNC") == 0 && strlen(user2) > 0) {
        upsert_backup_account(user2, pass2);
        save_backup_accounts();
        printf("[账号备份] %s 已保存到 %s\n", user2, ACCOUNTS_FILE);
        snprintf(reply, reply_size, "OK");
        return;
    }

    if (sscanf(cmd, "%15[^:]:%63[^:]:%d", op, user, &amount) == 3 &&
        strlen(user) > 0) {

        if (strcmp(op, "PAY") == 0) {
            if (amount <= 0) {
                snprintf(reply, reply_size, "ERR");
                return;
            }
            struct account *acc = find_or_create(user);
            if (acc == NULL) {
                snprintf(reply, reply_size, "ERR");
                return;
            }
            if (acc->balance < amount) {
                printf("[支付失败] %s 余额 %d，需扣 %d\n",
                       user, acc->balance, amount);
                snprintf(reply, reply_size, "NO");
                return;
            }
            acc->balance -= amount;
            save_balances();
            printf("[支付成功] %s 扣款 %d，余额 %d\n",
                   user, amount, acc->balance);
            snprintf(reply, reply_size, "OK");

        } else if (strcmp(op, "RECHARGE") == 0) {
            if (amount <= 0) {
                snprintf(reply, reply_size, "ERR");
                return;
            }
            struct account *acc = find_or_create(user);
            if (acc == NULL) {
                snprintf(reply, reply_size, "ERR");
                return;
            }
            acc->balance += amount;
            save_balances();
            printf("[充值成功] %s 充值 %d，余额 %d\n",
                   user, amount, acc->balance);
            snprintf(reply, reply_size, "OK");

        } else {
            snprintf(reply, reply_size, "ERR");
        }
        return;
    }

    //BALANCE:用户名（只有两个字段）
    if (sscanf(cmd, "%15[^:]:%63s", op, user) == 2 &&
        strcmp(op, "BALANCE") == 0) {
        struct account *acc = find_or_create(user);
        if (acc == NULL) {
            snprintf(reply, reply_size, "ERR");
        } else {
            snprintf(reply, reply_size, "BAL:%d", acc->balance);
        }
        return;
    }

    snprintf(reply, reply_size, "ERR");
}

int main(int argc, char *argv[])
{
    int port = (argc > 1) ? atoi(argv[1]) : DEFAULT_PORT;

    load_balances();
    load_backup_accounts();

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        return 1;
    }

    //服务器重启后可以立刻再次绑定同一端口
    int on = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = htonl(INADDR_ANY);
    serv.sin_port = htons(port);

    if (bind(listenfd, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(listenfd, 5) < 0) {
        perror("listen");
        return 1;
    }

    printf("支付服务器已启动，监听端口 %d ...\n", port);

    while (1) {
        struct sockaddr_in cli;
        socklen_t clilen = sizeof(cli);
        int connfd = accept(listenfd, (struct sockaddr *)&cli, &clilen);
        if (connfd < 0) {
            perror("accept");
            continue;
        }
        printf("客户端连接：%s:%d\n", inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));

        //收发缓冲，攒够一行（以 \n 结尾）就处理一条命令
        char buf[1024] = {0};
        int len = 0;
        while (1) {
            char tmp[256];
            int n = recv(connfd, tmp, sizeof(tmp), 0);
            if (n <= 0) {
                break;                       //客户端断开
            }
            if (len + n >= (int)sizeof(buf)) {
                break;                       //命令过长，直接断开
            }
            memcpy(buf + len, tmp, n);
            len += n;
            buf[len] = 0;

            //按 \n 拆分处理
            char *start = buf;
            char *nl;
            while ((nl = strchr(start, '\n')) != NULL) {
                *nl = 0;
                if (strlen(start) > 0) {
                    printf("收到命令：%s\n", start);
                    char reply[4096];       //ACCS 要一次回很多账号，缓冲区加大
                    handle_command(start, reply, sizeof(reply));
                    strcat(reply, "\n");
                    send(connfd, reply, strlen(reply), 0);
                }
                start = nl + 1;
            }
            //把剩余的半条命令挪到缓冲区头部
            if (start != buf) {
                len = strlen(start);
                memmove(buf, start, len + 1);
            }
        }
        close(connfd);
        printf("客户端断开\n");
    }

    close(listenfd);
    return 0;
}
