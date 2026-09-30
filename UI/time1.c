#include "ui.h"
#include "lvgl/lvgl.h"

#include <time.h>

#define TEST_MAGIC 'x'
#define LED_ON   0     /* 或类似定义 */
#define LED_OFF  1


#define LED1 _IO(TEST_MAGIC, 0)
#define LED2 _IO(TEST_MAGIC, 1)    
#define LED3 _IO(TEST_MAGIC, 2)    
#define LED4 _IO(TEST_MAGIC, 3)    

//=============================================时间获取============================

typedef struct {
    lv_obj_t  *label;//指向需要操作的标签
    const char *fmt;//显示的时间格式
} time_label_t;

static time_label_t g_labels[256];//最多可以存放256个时间标签
static int g_count = 0;           //记录已经创建的标签数量
static lv_timer_t *g_timer = NULL;//lvgl定时器初始化置空

static const char *week_names[] = 
{
    "星期日","星期一","星期二","星期三","星期四","星期五","星期六"
};

//把对应的label绑定到对应刷新的时间格式
static void time_timer_cb(lv_timer_t *t)
{
    static int last_sec = -1;//定义全局变量，只初始化一次，程序结束时销毁
    
    time_t now = time(NULL);
    struct tm *ti = localtime(&now);//组合使用获取本地时间
    
    if (ti->tm_sec == last_sec) return; //如果当前时间等于记录的上一秒时间 退出
    last_sec = ti->tm_sec;//更新为当前时间

    char buf[32];
    //遍历所有标签
    for (int i = 0; i < g_count; i++)
    {
        //删除屏幕时标签也会随之删除，只有充值面板是删除，其他的都是切换
        if (g_labels[i].label == NULL) continue;

        //开头为%号说明是日期或时间          strftime根据打印的格式解析 ti    写进buf
        if (g_labels[i].fmt[0] == '%') strftime(buf, sizeof(buf), g_labels[i].fmt, ti);
                                                    //0-6，从周日开始，周日=0    写进buf
        else  snprintf(buf, sizeof(buf), "%s", week_names[ti->tm_wday]);

        lv_label_set_text(g_labels[i].label, buf);//把显示的文本值设置成buf
    }
}

static void time_bind(lv_obj_t *label, const char *fmt)
{
    //不是标签，是否指空（野）或者超过256的上限
    if (!label || g_count >= 256) return;

    //把这个标签的名字和显示的时间格式存进数组
    g_labels[g_count].label = label; 
    g_labels[g_count].fmt   = fmt;
    g_count++;

    //定时器没有被创建
    if (g_timer == NULL)
    {
        //创建一个并设置定时器0.2秒更新一次
        g_timer = lv_timer_create(time_timer_cb, 200, NULL);
    }

}
void date1(lv_obj_t *label)
{ 
    //strftime解析     年 月 日
    time_bind(label, "%Y-%m-%d"); 
}

void clock1(lv_obj_t *label) 
{   //strftime解析     时 分 秒
    time_bind(label, "%H:%M:%S"); 
}

void week1(lv_obj_t *label)  
{ 
    time_bind(label, "WEEK"); 
}

//============================================== 天气部分 ===============================
//先将label对应到数组中的成员    weather1(lv_obj_t *label)
//调用线程先http连接接口获取天气信息解析出温度，并更新到g_temp_str   *weather_thread(void *arg)
//然后把g_temp_str的值设置到存储的天气数组text  weather_update(void)
//先存进数组在调线程解析在更新

typedef struct sockaddr_in IPV4;
typedef struct sockaddr ADDR;
socklen_t socklen = sizeof(IPV4);
int led_fd;

static lv_obj_t *g_weather_labels[64];//各屏的天气label存储数组
static int  g_weather_count = 0;
static char g_temp_str[16] = "--度";//全局内存，存放线程解析出的温度字符串， 后面给主线程读出去

//
void weather1(lv_obj_t *label)
{
    //不是标签，是否指空（野）或者超过256的上限
    if (!label || g_weather_count >= 64) return;

    //把标签的名字存进天气标签存储数组
    g_weather_labels[g_weather_count++] = label;
}

//更新天气text到天气存储表
void weather_update(void)
{
    //遍历所有存储的天气标签
    for (int i = 0; i < g_weather_count; i++)
    {
        if (g_weather_labels[i])
        {
            //把解析出来的温度设置成第几个成员label的文本显示
            lv_label_set_text(g_weather_labels[i], g_temp_str);
        }
    }
}

//子线程执行函数，解析获取到的温度并写进缓冲区
void *weather_thread(void *arg)
{
    while (1)
    {
        //连接接口盒子服务器获取天气
        struct hostent *p = gethostbyname("cn.apihz.cn");
        if (p == NULL) 
        { 
            sleep(60); 
            continue; 
        }
                         //ipv4     流式套接字，tcp协议
        int tcpfd = socket(AF_INET, SOCK_STREAM, 0);
        if (tcpfd == -1) 
        { 
            sleep(60); 
            continue; 
        }

        //配置ip和端口
        struct sockaddr_in saddr;
        saddr.sin_family      = AF_INET;
        saddr.sin_port        = htons(80);//HTTP固定访问端口
        saddr.sin_addr.s_addr = inet_addr(inet_ntoa(*(struct in_addr *)(p->h_addr_list[0])));

        //连接服务器，失败60秒后重试
        if (connect(tcpfd, (struct sockaddr *)&saddr, sizeof(saddr)) == -1)
        { 
            close(tcpfd); 
            sleep(60); 
            continue; 
        }

        //发送请求报文
        char *Request =
            "GET /api/tianqi/tqyb.php?"
            "id=10020873&"
            "key=8734b0e660d57a9cf53978b3312d0a4c&"
            "sheng=%E6%B9%96%E5%8C%97&"
            "place=%E6%AD%A6%E6%B1%89&"
            "day=1 "
            "HTTP/1.1\r\n"
            "Host: cn.apihz.cn\r\n"
            "Connection: close\r\n"
            "\r\n";
            
        send(tcpfd, Request, strlen(Request), 0);

        //接受
        char buf[4096] = {0};
        size_t size, total = 0;
        
        //                  套接字   起始位置      需要读的字节数
        while ((size = recv(tcpfd, buf + total, sizeof(buf) - total - 1, 0)) > 0)//把缓冲区读完
        {
            total += size;
        }
        //手动添加
        buf[total] = '\0';
        close(tcpfd);

        printf("=====天气报文======\n%s",buf);  

        //解析出需要的温度
        char *q = strstr(buf, "temp");//在buf中查找temp出现的位置并用q记录位置，从t开始
        if (q)
        {
                        //跳过非数字字符           有负号就停下 
            while (*q && (*q < '0' || *q > '9') && *q != '-') 
            {
                q++;
            }
            char temp[64] = {0};
            int i = 0;
            //         数字部分                  零下        小数部分
            while ((*q >= '0' && *q <= '9') || *q == '-' || *q == '.')
            {   
            temp[i++] = *q++;
            }
            //把解析出的温度存入天气标签
            snprintf(g_temp_str, sizeof(g_temp_str), "%s度", temp);
        }

        sleep(1800); // 30分钟更新一次
    }
    return NULL;
}

// 返回的报文
// "lon":"114.050","lat":"30.600","uptime":"2026-09-16 00:00:28","nowinfo":{"precipitation":0.6,"temperature":19,

//==========================================led云端控制======================
//每秒向巴法云发送1-5,1-4对应四个led灯，发送后打开对应的led，第五秒全部关闭
//创建三个线程函数，分别发送，接收，心跳包防止断联
//连接巴法云函数

int tcpfd;//全局定义

//向巴法云发送控制led的消息
void * led_flow(void *arg)
{
    //设置成分离进程，结束时自动回收资源
    pthread_detach(pthread_self());
    char msg[128];
    int i = 1;
    while(1)
    {
        memset(msg, 0, 128);
        sprintf(msg, "cmd=2&uid=da19aabc274f49a29dedc5bbddc2f2a6&topic=deng&msg=%d\r\n", i);
        send(tcpfd, msg, strlen(msg), 0);    //向巴法云发送消息，由巴法云发回订阅的设备
        usleep(500000); 
        i++;
        if(i > 5) i = 1;                     
    }
}

//接受巴法云传回的消息
void * recv_msg(void *arg)
{
    //线程分离，结束后自动回收资源
    pthread_detach(pthread_self());          

    led_fd = open("/dev/Led", O_RDWR);       //打开LED驱动
    if(led_fd == -1)
    {
        perror("open /dev/Led failed");
        return NULL;
    }

    char buffer[1024];

    while(1)
    {
        memset(buffer, 0, sizeof(buffer));

        int ret = recv(tcpfd, buffer, sizeof(buffer), 0);
        if(ret <= 0)                         //连接断开或出错
        {
            printf("与巴法云的连接已断开\n");
            break;
        }

        printf("巴法云发送过来的:%s\n", buffer);

        //从收到的报文里找 msg=，取后面的字符
        char *p = strstr(buffer, "msg=");
        if(p != NULL)
        {
            //跳过msg=  获取后面的数字
            char sta = *(p + 4);

            if(sta == '1')       ioctl(led_fd, LED1, LED_ON);
            else if(sta == '2')  ioctl(led_fd, LED2, LED_ON);
            else if(sta == '3')  ioctl(led_fd, LED3, LED_ON);
            else if(sta == '4')  ioctl(led_fd, LED4, LED_ON);
            else if(sta == '5' || sta == '0')            //全关
            {
                ioctl(led_fd, LED1, LED_OFF);
                ioctl(led_fd, LED2, LED_OFF);
                ioctl(led_fd, LED3, LED_OFF);
                ioctl(led_fd, LED4, LED_OFF);
            }
        }
    }

    close(led_fd);
    return NULL;
}

//心跳包防止断联
void * ping_msg(void *arg)
{
    pthread_detach(pthread_self());          //线程分离

    while(1)
    {
        send(tcpfd, "ping\r\n", strlen("ping\r\n"), 0);
        sleep(60);
    }
}

//连接巴法云
int bemfa_Client(const char * com)
{
    //解析获取的域名
    struct hostent *p = gethostbyname(com);
    const char *IP = inet_ntoa(*(struct in_addr *)(p->h_addr_list[0]));
    printf("域名地址:%s\n",IP);

    //1.创建TCP通信套接字
    tcpfd = socket(AF_INET,SOCK_STREAM,0);
    if(tcpfd == -1)
    {
        perror("tcpfd socket failed");
        return -1;
    }
    //2.配置服务器本身的地址和端口号
    IPV4 saddr;
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(8344);
    saddr.sin_addr.s_addr = inet_addr(IP);

    //3.通过函数建立客户端连接服务器对象
    int ret = connect(tcpfd,(ADDR *)&saddr,socklen);
    if(ret == -1)
    {
        perror("connect failed");
        return -1;
    }
    printf("客户端连接成功\n");

    return tcpfd;
}
