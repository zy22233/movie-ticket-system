#include <stdio.h>
#include "lvgl/lvgl.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include "list.h"

#include <netdb.h>

#include "./UI/ui.h"

#define  DISP_BUF_SIZE  1024*600*4
//date -s "2026-09-29 21:03:30"
//全局链表头指针
node *account_list = NULL;

/* 帧缓冲和触摸屏初始化函数 */
void lvgl_init_framebuffer_ts(void)
{
    /* LittlevGL init */
    lv_init();

    /* Linux frame buffer device init */
    fbdev_init();

    /* 设置显示缓冲区 */
    static lv_color_t buf[DISP_BUF_SIZE];
    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf, NULL, DISP_BUF_SIZE);

    /* 初始化显示驱动 */
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf   = &disp_buf;
    disp_drv.flush_cb   = fbdev_flush;
    disp_drv.hor_res    = 800;
    disp_drv.ver_res    = 480;
    lv_disp_drv_register(&disp_drv);

    /* 初始化触摸屏输入设备 */
    evdev_init();
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = evdev_read;
    lv_indev_drv_register(&indev_drv);
}

// 按钮事件回调函数
static void event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        LV_LOG_USER("Clicked");
    }
}

 // 加载账号文件到链表
void load_account_files(node *head)
{
    DIR *dir;
    struct dirent *entry;
    char filepath[512];
    
    // 打开 account 目录
    dir = opendir("./account");
    if (dir == NULL) {
        printf("提示：./account 目录不存在，将在注册时自动创建\n");
        return;
    }
    
    // 遍历目录中的所有文件
    while ((entry = readdir(dir)) != NULL) 
    {
        // 跳过 . 和 ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) 
        {
            continue;
        }
        
        // 只处理 .txt 文件
        if (strstr(entry->d_name, ".txt") != NULL) 
        {
            // 构建完整文件路径
            snprintf(filepath, sizeof(filepath), "./account/%s", entry->d_name);
            
            // 创建新节点并插入链表
            node *new_node = create_newnode(filepath);
            insert(head, new_node);
            
            printf("已加载账号文件: %s\n", filepath);
        }
    }
    
    closedir(dir);
}


int main(lv_event_t * e)
{
    //链表初始化
    account_list = initList();
    // 加载账号文件到链表
    load_account_files(account_list);
    // 从服务器拉取其他板子备份过的账号，实现多板共享账号
    fetch_server_accounts();
    // 打印链表内容（调试用）
    listForEach(account_list);
    //帧缓冲和屏幕初始化
    printf("666\n");
    lvgl_init_framebuffer_ts();
    printf("666\n");
   
    tcpfd = bemfa_Client("bemfa.com");
    if(tcpfd == -1)
    {
        printf("连接巴法云失败，请检查网络！\n");
    }
    else
    {
        // 2. 订阅主题
        char recvled[] = "cmd=1&uid=da19aabc274f49a29dedc5bbddc2f2a6&topic=deng\r\n";
        send(tcpfd, recvled, strlen(recvled), 0);
    }
    
    //UI界面初始化
    ui_init();

    pthread_t tid,thread1, thread2, thread3;
    pthread_create(&tid, NULL, weather_thread, NULL);   // 后台取天气
    pthread_create(&thread1, NULL, recv_msg,  (void *)&tcpfd);     //接受巴法云发回的信息
    pthread_create(&thread2, NULL, ping_msg,  (void *)&tcpfd);     //心跳包，避免断联
    pthread_create(&thread3, NULL, led_flow,  (void *)&tcpfd);     //控制led

    printf("666\n");
    while(1){
        lv_timer_handler();
        weather_update(); 
        usleep(5000);
    }
    return 0;
}


/*Set in lv_conf.h as `LV_TICK_CUSTOM_SYS_TIME_EXPR`*/
uint32_t custom_tick_get(void)
{
    static uint64_t start_ms = 0;
    if(start_ms == 0) {
        struct timeval tv_start;
        gettimeofday(&tv_start, NULL);
        start_ms = (tv_start.tv_sec * 1000000 + tv_start.tv_usec) / 1000;
    }

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    uint64_t now_ms;
    now_ms = (tv_now.tv_sec * 1000000 + tv_now.tv_usec) / 1000;

    uint32_t time_ms = now_ms - start_ms;
    return time_ms;
}
