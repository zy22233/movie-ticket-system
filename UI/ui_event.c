#include "ui.h"
#include "list.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <time.h>
                
//ip和端口
#define PAY_SERVER_IP   "192.168.126.94"
#define PAY_SERVER_PORT 8888

//创建一个账户链表
extern node *account_list;
// 存储登录的用户名
char g_current_user[64] = {0};
// 存储当前选择的电影名
char g_movie_name[64] = {0};
// 存储当前选择的影院票价
int g_ticket_price = 40;
// 已确认的票信息和小吃
int g_ticket_count  = 0;               //已选座位数
int g_ticket_amount = 0;               //票款总额 = 座位数 × 票价
static char g_seat_desc[128] = {0};    //座位位置描述，如 "1排2座 3排4座"
// 购票人数
static int g_buy_number = 1;

static const char *food_names[6] = {"冰红茶", "劲凉", "雷碧", "美年达", "薯条", "肯德基"};
static const int   food_prices[6] = {4, 5, 4, 5, 20, 50};
static int food_counts[6] = {0};
//结账总金额
static int checkout_amount(void);
 
//==============================功能函数，功能按钮===================================
// 刷新食品页底部数据
static void food_refresh_labels(void)
{
    int total = checkout_amount();
    int cnt = 0;
    for (int i = 0; i < 6; i++) 
	{
        cnt += food_counts[i];
    }
    lv_label_set_text_fmt(ui_number2, "已选：%d 个", cnt);//商品数量
    lv_label_set_text_fmt(ui_total2, "总计： %d元", total);//购物车总金额
}

//退出按钮
void back(lv_event_t * e)
{
	printf("退出程序\n");
	exit(0);
}

void break1(lv_event_t * e)
{
	printf("退出程序\n");
	exit(0);//直接结束进程，main 里的 while(1) 循环随之终止
}


//===============================================选择电影 影院  座位==============================================
//点击任意影片演出按钮时记录点击的电影名
void ui_select_movie(const char *name)
{
	//把选中的 电影名存进g_movie_name中
	snprintf(g_movie_name, sizeof(g_movie_name), "%s", name);
	printf("已选择电影：%s\n", g_movie_name);
}

// 统计选座页 5 排座位中被选中的座位个数
static int count_checked_seats(void)
{
	//5行座位
    lv_obj_t *rows[5] = {ui_hang1, ui_hang2, ui_hang3, ui_hang4, ui_hang5};
    int n = 0;
    for (int r = 0; r < 5; r++) 
	{
		//不存在或没创建就跳过
        if (rows[r] == NULL) continue;
		//数这行有几个孩子，返回出来即这一排座位的个数
        uint32_t cnt = lv_obj_get_child_cnt(rows[r]);
		//遍历所有的座位
        for (uint32_t k = 0; k < cnt; k++) 
		{
			//把第k个座位的状态赋给seat
            lv_obj_t *seat = lv_obj_get_child(rows[r], k);
            if (lv_obj_has_state(seat, LV_STATE_CHECKED))//如果这个座位被选中 
			{
                n++;
            }
        }
    }
    return n;
}

//选择购票人数
void choosenumber(lv_event_t * e)
{
	//下拉框选项：0=标题 1~5=人数
	uint16_t sel = lv_dropdown_get_selected(ui_buynumber);
	if (sel >= 1 && sel <= 5) 
	{
		g_buy_number = (int)sel;
	}
	printf("选择购票人数：%d 人\n", g_buy_number);

	//统计当前已勾选的座位数并刷新标签
	int seats = count_checked_seats();
	lv_label_set_text_fmt(ui_number, "已选：%d 张", seats);
	lv_label_set_text_fmt(ui_total, "合计： %d元", seats * g_ticket_price);

	//提示选座数量和人数是否匹配
	if (seats > g_buy_number) 
	{
		printf("提示：已选 %d 个座位，超过购票人数 %d 人\n", seats, g_buy_number);
	} 
	else if (seats < g_buy_number) 
	{
		printf("提示：还需选择 %d 个座位\n", g_buy_number - seats);
	}
}

//记录电影名和票价 显示到选座顶部
void ui_select_cinema(int price)
{
	g_ticket_price = price;//根据传回来的参数定为票价,票价定义在ui.c
	printf("已选择影院，票价 %d 元/张\n", g_ticket_price);

	if (ui_moviename != NULL) 
	{
		lv_label_set_text_fmt(ui_moviename, "电影名：%s", g_movie_name);//把记录的电影名显示到选座页顶部
	}

	//刷新选座页底部"已选/合计"（进新影院时座位清零显示）
	if (ui_number != NULL && ui_total != NULL)
	{
		//调用座位检查函数，返回选中的座位数
		int seats = count_checked_seats();
		//更新信息到页面最下面的文本中
		lv_label_set_text_fmt(ui_number, "已选：%d 张", seats);//选中座位数即票数
		lv_label_set_text_fmt(ui_total, "合计： %d元", seats * g_ticket_price);//金额 = 个数 * 票价
	}

	//统一处理已售座位（置灰 + 不可选中）
	ui_apply_sold_seats();
}

// 记录选中的票的位置信息，根据横坐标的大小来排序，从左到右，算出最后需要结账的金额
void ui_confirm_tickets(void)
{
    lv_obj_t *rows[5] = {ui_hang1, ui_hang2, ui_hang3, ui_hang4, ui_hang5};
    //票数，座位描述
	g_ticket_count = 0;
    g_seat_desc[0] = 0;

    int len = 0;
    for (int r = 0; r < 5; r++) 
	{
        if (rows[r] == NULL) continue;
        uint32_t cnt = lv_obj_get_child_cnt(rows[r]);
        for (uint32_t k = 0; k < cnt; k++) 
		{
            lv_obj_t *seat = lv_obj_get_child(rows[r], k);
            if (!lv_obj_has_state(seat, LV_STATE_CHECKED)) 
			{
                continue;
            }

			//选中的k座位的横坐标
            lv_coord_t seat_x = lv_obj_get_x(seat);
            int col = 1;
			
            for (uint32_t m = 0; m < cnt; m++) 
			{
                if (m == k) continue;
				//获取这个座位的横坐标		r行第m个座位          与k比大小小的话位置++ 
                if (lv_obj_get_x(lv_obj_get_child(rows[r], m)) < seat_x) 
				{
                    col++;
                }
            }
			//每有一个选中的座位，票数++
            g_ticket_count++;
            len += snprintf(g_seat_desc + len, sizeof(g_seat_desc) - len, "%s%d排%d座",
														(len == 0) ? "" : " ", //第一个不空，后面的空
														r + 1, 
														col);
            if (len >= (int)sizeof(g_seat_desc) - 1)
				break;
        }
    }

	//总金额 = 票数 * 单价
    g_ticket_amount = g_ticket_count * g_ticket_price;
    
	printf("影厅座位%s,共 %d 张，票款 %d 元\n",
						   (g_ticket_count > 0) ? g_seat_desc : "无",  //具体座位信息
							g_ticket_count, 						   //票数
							g_ticket_amount);						   //总金额
    //标签 刷新
    food_refresh_labels();
}
// 支付成功后清座位取消勾选，票款归零，选座页标签复位
static void clear_ticket_selection(void)
{
    lv_obj_t *rows[5] = {ui_hang1, ui_hang2, ui_hang3, ui_hang4, ui_hang5};
    
	for (int r = 0; r < 5; r++) 
	{
        if (rows[r] == NULL) continue;

        uint32_t cnt = lv_obj_get_child_cnt(rows[r]);
        for (uint32_t k = 0; k < cnt; k++) 
		{
			//将已经选中的按钮恢复成没有被选中
            lv_obj_clear_state(lv_obj_get_child(rows[r], k), LV_STATE_CHECKED);
		}
    }
    //票数，金额，座位信息全部清0
	g_ticket_count = 0;
    g_ticket_amount = 0;
    g_seat_desc[0] = 0;
    
	if (ui_number != NULL && ui_total != NULL) 
	{
        lv_label_set_text(ui_number, "已选: 0张");
        lv_label_set_text(ui_total, "合计： 0元");
    }
}


//===============================================已售座位=================================================
//已售座位表：g_sold_seats[排-1][列-1] 为 true 表示该座位已售出
//实现方式是集中处理：不给每个座位按钮单独写代码，只在进选座页时统一遍历 5 排的所有孩子，
//把已售的座位取消选中、去掉 CLICKABLE 标志（点不动自然选不上）并置灰
#define SEAT_ROWS 5
#define SEAT_COLS 10
#define SOLD_SEAT_FILE "./sold_seats.txt"      //每行格式：电影名 排 列
static bool g_sold_seats[SEAT_ROWS][SEAT_COLS] = {false};

//算出座位在排内的列号（按横坐标从左到右），和 ui_confirm_tickets 的算法一致
static int seat_col_of(lv_obj_t *row, lv_obj_t *seat)
{
    uint32_t cnt = lv_obj_get_child_cnt(row);
    lv_coord_t seat_x = lv_obj_get_x(seat);
    int col = 1;
    for (uint32_t m = 0; m < cnt; m++) {
        lv_obj_t *other = lv_obj_get_child(row, m);
        if (other == seat) continue;
        if (lv_obj_get_x(other) < seat_x) col++;
    }
    return col;
}

//进选座页时调用：按当前电影从 sold_seats.txt 重新加载已售记录，
//然后统一处理 5 排的所有座位按钮，已售的置灰并禁止选中
void ui_apply_sold_seats(void)
{
    //先清空再按当前电影重新读盘
    memset(g_sold_seats, 0, sizeof(g_sold_seats));
    FILE *fp = fopen(SOLD_SEAT_FILE, "r");
    if (fp != NULL) {
        char line[256];
        while (fgets(line, sizeof(line), fp) != NULL) {
            char movie[64] = {0};
            int r = 0, c = 0;
            if (sscanf(line, "%63s %d %d", movie, &r, &c) == 3 &&
                strcmp(movie, g_movie_name) == 0 &&
                r >= 1 && r <= SEAT_ROWS && c >= 1 && c <= SEAT_COLS) {
                g_sold_seats[r - 1][c - 1] = true;
            }
        }
        fclose(fp);
    }

    lv_obj_t *rows[SEAT_ROWS] = {ui_hang1, ui_hang2, ui_hang3, ui_hang4, ui_hang5};
    for (int r = 0; r < SEAT_ROWS; r++) {
        if (rows[r] == NULL || !lv_obj_is_valid(rows[r])) continue;
        uint32_t cnt = lv_obj_get_child_cnt(rows[r]);
        for (uint32_t k = 0; k < cnt; k++) {
            lv_obj_t *seat = lv_obj_get_child(rows[r], k);
            int col = seat_col_of(rows[r], seat);
            if (col < 1 || col > SEAT_COLS) continue;
            if (!g_sold_seats[r][col - 1]) continue;

            lv_obj_clear_state(seat, LV_STATE_CHECKED);           //取消选中
            lv_obj_clear_flag(seat, LV_OBJ_FLAG_CLICKABLE);       //禁止点击，选不上
            //置灰显示已售
            lv_obj_set_style_bg_color(seat, lv_color_hex(0x4A4A4A), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(seat, lv_color_hex(0x4A4A4A), LV_PART_MAIN | LV_STATE_CHECKED);
        }
    }
}

//支付成功后调用：把本次买的座位（g_seat_desc，如 "1排2座 3排4座"）记录为已售并写盘
void ui_mark_seat_desc_sold(const char *seat_desc)
{
    if (seat_desc == NULL || seat_desc[0] == 0) return;

    FILE *fp = fopen(SOLD_SEAT_FILE, "a");
    if (fp == NULL) {
        printf("错误：无法写入已售座位文件 %s\n", SOLD_SEAT_FILE);
        return;
    }

    const char *p = seat_desc;
    while (*p) {
        int r = 0, c = 0;
        if (sscanf(p, "%d排%d座", &r, &c) == 2 &&
            r >= 1 && r <= SEAT_ROWS && c >= 1 && c <= SEAT_COLS) {
            g_sold_seats[r - 1][c - 1] = true;
            fprintf(fp, "%s %d %d\n", (g_movie_name[0] != 0) ? g_movie_name : "未知电影", r, c);
        }
        p = strchr(p, ' ');
        if (p == NULL) break;
        p++;
    }
    fclose(fp);
    printf("已售座位已记录到 %s：%s\n", SOLD_SEAT_FILE, seat_desc);
    ui_apply_sold_seats();//立刻刷新选座页，已售的马上变灰
}

//===============================================购买小吃，总账单==============================================

// is_add返回假则是减号按钮
static int food_index_from_event(lv_event_t * e, bool is_add)
{
    lv_obj_t * target = lv_event_get_target(e);

    if (is_add) //加号按钮
	{
        if (target == ui_add1) return 0;
        if (target == ui_add2) return 1;
        if (target == ui_add3) return 2;
        if (target == ui_add4) return 3;
        if (target == ui_add5) return 4;
        if (target == ui_add6) return 5;
    } 
	else 
	{
        if (target == ui_jian1) return 0;
        if (target == ui_jian2) return 1;
        if (target == ui_jian3) return 2;
        if (target == ui_jian4) return 3;
        if (target == ui_jian5) return 4;
        if (target == ui_jian6) return 5;
    }
    return -1;
}
//商品减1
void jianwoods(lv_event_t * e)
{
	int idx = food_index_from_event(e, false);//根据被点的减号按钮判断是哪种商品
	if (idx < 0) 
	{
		return;
	}
	if (food_counts[idx] > 0) //商品数量大于0
	{
		food_counts[idx]--;
		food_refresh_labels();//刷新
	} 
	else 
	{
		printf("提示：%s 数量已经为 0\n", food_names[idx]);
	}
}

//商品加1
void addwoods(lv_event_t * e)
{
	int idx = food_index_from_event(e, true);
	if (idx < 0) 
	{
		return;
	}
	if (food_counts[idx] < 5)//同一商品最多5个
	{
		food_counts[idx]++;
		food_refresh_labels();//刷新
	} 
	else 
	{
		printf("%s 每人最多购买 5 份\n", food_names[idx]);
	}
}

// 购物车总金额
static int food_total(void)
{
    int total = 0;
    for (int i = 0; i < 6; i++) 
	{
        total += food_counts[i] * food_prices[i];//电影票+小吃
    }
    return total;//返回出金额
}

// 把当前购物车拼成一段描述文字，如 "冰红茶*1 薯条*2"
static void food_build_desc(char *buf, int bufsize)
{
    buf[0] = 0;
    int len = 0;

	//遍历6个商品
    for (int i = 0; i < 6; i++) 
	{
        if (food_counts[i] > 0) 
		{
            len += snprintf(buf + len, bufsize - len, "%s%s*%d",
														(len == 0) ? "" : "  ",//不分隔和分隔 
														food_names[i], 
														food_counts[i]);
            if (len >= bufsize - 1) break;
        }
    }
    if (len == 0) 
	{
        snprintf(buf, bufsize, "无");
    }
}

//拼成对应用户名的订单文件路径
static void get_order_path(char *path, int size)
{
    if (g_current_user[0] != 0) 
	{
        snprintf(path, size, "./account/%s_order.txt", g_current_user);//拼成用户名加_order.txt的格式
    } 
	else 
	{
        snprintf(path, size, "./account/order.txt");
    }
}

// 以追加写法保存订单记录到account，格式：小吃描述|总价|下单时间
static void save_order_record(const char *desc, int price)
{
    // 确保 account 目录存在
    struct stat st = {0};
    if (stat("./account", &st) == -1) {
        mkdir("./account", 0755);
    }

	//存放文件路径
    char path[256];
    get_order_path(path, sizeof(path));

	//打开路劲中的文件，追加写入
    FILE *fp = fopen(path, "a");
    if (fp == NULL) {
        printf("error: cannot open order file %s\n", path);
        return;
    }

	//时间函数获取本地时间
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);

    char tstr[32] = {0};
    strftime(tstr, sizeof(tstr), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(fp, "%s|%d|%s\n", desc, price, tstr);// 小吃描述，价格，下单时间
    fclose(fp);
    printf("订单已保存到 %s\n", path);
}

// 清空购物车并刷新标签
static void food_clear(void)
{
    memset(food_counts, 0, sizeof(food_counts));
    food_refresh_labels();
}

//==========================================合并，创建，追加，查看订单=============================
//订单合并
static void build_order_desc(char *buf, int bufsize)
{
    int len = 0;
    buf[0] = 0;

    if (g_ticket_count > 0) //票数不为0
	{                                          //电影票 数量 座位
        len += snprintf(buf + len, bufsize - len, "%s %d张(%s)", (g_movie_name[0] != 0) ? g_movie_name : "电影票",
                        g_ticket_count,
						g_seat_desc);
        if (len >= bufsize - 1) return;
    }
    if (food_total() > 0)//小吃不为0
	{
        char snacks[256];
        food_build_desc(snacks, sizeof(snacks));
        snprintf(buf + len, bufsize - len, "%s%s", (len == 0) ? "" : " + ", //第一个没有，后面就是xx + xx + xx
													snacks);
    }
    if (len == 0 && food_total() == 0)//都为零
	{
        snprintf(buf, bufsize, "无");
    }
}

//算账
static int checkout_amount(void)
{
    return food_total() + g_ticket_amount;// 结账总金额 = 票款 + 小吃
}

//把选中的电影票,小吃 合并成一条订单记录，存到 ./account/用户名_order.txt
void buyfood(lv_event_t * e)
{
	int total = checkout_amount();//票款 + 小吃
	if (total <= 0) 
	{
		printf("提示：还没有选择电影票或小吃\n");
		return;
	}

	char desc[256];
	//调用函数 生成一个订单信息卡片，合并小吃和电影票
	build_order_desc(desc, sizeof(desc));
	//保存或追加一个订单信息卡片
	save_order_record(desc, total);

	printf("购买成功：%s，合计 %d 元\n", desc, total);

	//本次买的座位标记为已售（要在清空座位信息之前记录）
	ui_mark_seat_desc_sold(g_seat_desc);

	// 清空购物车和电影票，跳转到成功界面
	food_clear();
	clear_ticket_selection();
	_ui_screen_change(&ui_success, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_success_screen_init);
}

//读取用户文件的所有订单
void searchdingdan(lv_event_t * e)
{
	static bool building = false;//防止 500ms 切屏动画期间重复点击重入
	if (building) 
	{
		return;
	}
	building = true;

	printf("[dingdan] 1: enter\n");

	// 清空订单页列表旧内容，下面按订单记录重新生成
	if (ui_orderlist1 != NULL) 
	{
		lv_obj_clean(ui_orderlist1);
	}
	
	printf("[dingdan] 2: clean done\n");
    //动态插入
	lv_scr_load_anim(ui_dingdan, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, false);


	char path[256];
	//把用户订单文件的路径存入path缓冲区
	get_order_path(path, sizeof(path));

	//只读打开用户订单文件
	FILE *fp = fopen(path, "r");
	
	if (fp == NULL) //还没有任何订单，跳转失败界面
	{
		//在订单列表里创建一个空标签
		lv_obj_t *empty = lv_label_create(ui_orderlist1);
		lv_label_set_text(empty, "NO订单");
		lv_obj_set_style_text_font(empty, &ui_font_zti, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_style_text_color(empty, lv_color_hex(0x140202), LV_PART_MAIN | LV_STATE_DEFAULT);
		printf("[dingdan] 3: no order file %s, switch screen\n", path);
		_ui_screen_change(&ui_dingdan, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_dingdan_screen_init);
		building = false;
		return;
	}

	printf("[dingdan] 3: file %s opened\n", path);

	char line[512];
	int count = 0;
	while (fgets(line, sizeof(line), fp) != NULL && count < 20) //最多20个订单
	{
		line[strcspn(line, "\n")] = 0;//去掉换行符

		char desc[256] = {0};//小吃
		int price = 0;//总价
		char tstr[64] = {0};//下单时间                        save_order小吃描述|总价|下单时间写入account
		//从line中读取 分别存入desc price tstr         255[^|]读到的文本上限设置为255 读到 | 停止存入desc
		if (sscanf(line, "%255[^|]|%d|%63[^\n]", desc, &price, tstr) != 3) 
		{
			continue;//格式不对的行跳过
		}
		//                                           订单数    小吃   金额   下单时间
		printf("[dingdan] 4: order %d: %s %d %s\n", count + 1, desc, price, tstr);

		//订单卡片
		//分隔线
		lv_obj_t *card = lv_obj_create(ui_orderlist1);
		lv_obj_remove_style_all(card);
		
		lv_obj_set_width(card, lv_pct(100));
		lv_obj_set_height(card, 100);

		lv_obj_set_style_radius(card, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_style_bg_color(card, lv_color_hex(0xBE9090), LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_style_bg_opa(card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

		//build_order_desc往文件中存入的订单信息格式为：电影票: + 小吃:
		char ticket[192] = {0};
		char snack[128] = {0};
		//指向中间的 + 号
		char *sep = strstr(desc, " + ");
		if (sep != NULL) 
		{  
			//把desc中的前sep-desc个字节存进ticket      转整形（'+'后的 - 全部） 
			snprintf(ticket, sizeof(ticket), "%.*s", (int)(sep - desc), desc);
			//跳过 小吃： 把后面的存进snack
			snprintf(snack, sizeof(snack), "%s", sep + 3);
		} 
		else if (strstr(desc, "张(") != NULL) //只有电影票的话座位的括号就会跟在张后面
		{
			snprintf(ticket, sizeof(ticket), "%s", desc);
		} 
		else //小吃
		{
			snprintf(snack, sizeof(snack), "%s", desc);  
		}

		//电影票部分再拆：'(' 前面是 "电影名 N张"，括号里是座位串
		char name_cnt[128] = {0};
		char seats[128] = {0};
		//找'('第一次出现的位置           GGBOND 2张(1排2座)
		char *lp = strchr(ticket, '(');
		//找')'第一次出现的位置
		char *rp = strchr(ticket, ')');
		if (lp != NULL && rp != NULL && lp < rp) 
		{
			//把lp前面的存进name_cnt   地址从ticket开始  作为票名
			snprintf(name_cnt, sizeof(name_cnt), "%.*s", (int)(lp - ticket), ticket);
			//把括号里面的存进seats 地址从lp+1开始 座位信息
			snprintf(seats, sizeof(seats), "%.*s", (int)(rp - lp - 1), lp + 1);
		} 
		else//出现错误时
		{
			snprintf(name_cnt, sizeof(name_cnt), "%s", ticket);
		}

		//第一行：电影名 + 张数（22px 字体）
		lv_obj_t *lmovie = lv_label_create(card);
		if (strlen(name_cnt) > 0) 
		{
			lv_label_set_text_fmt(lmovie, "电影名：%s", name_cnt);
		} 
		else 
		{
			lv_label_set_text(lmovie, "电影名：--");
		}
		lv_obj_set_style_text_font(lmovie, &ui_font_zzzz30, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_style_text_color(lmovie, lv_color_hex(0x140202), LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_pos(lmovie, 10, 8);

		//价格：右上角（￥和数字 zt3 字体里都有）
		lv_obj_t *lprice = lv_label_create(card);
		lv_label_set_text_fmt(lprice, "￥%d.00", price);
		lv_obj_set_style_text_font(lprice, &ui_font_zzzz30, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_style_text_color(lprice, lv_color_hex(0xFFD100), LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_align(lprice, LV_ALIGN_TOP_RIGHT, -10, 12);

		//第二行：座位位置（纯小吃订单这里显示下单时间）
		lv_obj_t *lsession = lv_label_create(card);
		if (strlen(seats) > 0) 
		{
			lv_label_set_text_fmt(lsession, "座位：%s", seats);
		} 
		else 
		{
			lv_label_set_text_fmt(lsession, "下单时间：%s", tstr);
		}
		lv_obj_set_style_text_font(lsession, &ui_font_zzzz30, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_style_text_color(lsession, lv_color_hex(0x472A2A), LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_pos(lsession, 10, 36);

		//分隔线
		lv_obj_t *divider = lv_obj_create(card);
		lv_obj_remove_style_all(divider);
		lv_obj_set_width(divider, lv_pct(100));
		lv_obj_set_height(divider, 2);
		lv_obj_set_y(divider, 56);
		lv_obj_set_style_bg_color(divider, lv_color_hex(0x0F0707), LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_style_bg_opa(divider, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

		//分隔线下方：小吃（左）
		lv_obj_t *lsnack = lv_label_create(card);
		if (strlen(snack) > 0) 
		{
			lv_label_set_text_fmt(lsnack, "小吃：%s", snack);
		} 
		else 
		{
			lv_label_set_text(lsnack, "小吃：--");
		}
		lv_obj_set_style_text_font(lsnack, &ui_font_zzzz30, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_style_text_color(lsnack, lv_color_hex(0x140202), LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_pos(lsnack, 10, 68);

		//下单时间：右下角
		lv_obj_t *ltime = lv_label_create(card);
		lv_label_set_text(ltime, tstr);
		lv_obj_set_style_text_font(ltime, &ui_font_zzzz30, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_style_text_color(ltime, lv_color_hex(0x472A2A), LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_align(ltime, LV_ALIGN_BOTTOM_RIGHT, -10, -6);

		count++;
	}
	fclose(fp);

	printf("[dingdan] 5: %d orders loaded, switch screen\n", count);
	_ui_screen_change(&ui_dingdan, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_dingdan_screen_init);
	building = false;
}

//=================================================充值查询支付========================================================
// 带超时的 TCP 连接防止服务器不可达时程序卡死
static int connect_with_timeout(const char *ip, int port, int timeout_sec)
{
	//创建套接字
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) 
	{
        return -1;
    }
	
	//配置ip地址
    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);
	
	//将ip地址转换成二进制网络形式存进serv.sin_addr并返回
    if (inet_pton(AF_INET, ip, &serv.sin_addr) != 1) 
	{
        close(fd);
        return -1;
    }

    // 设置非阻塞发起连接
    int flags = fcntl(fd, F_GETFL, 0);//获取当前的全部标志位
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);//将非阻塞的标志位打开

	//发起连接，接受返回值
    int ret = connect(fd, (struct sockaddr *)&serv, sizeof(serv));
    if (ret < 0) 
	{
		//打印出来的错误 不是 "连接正在进行中"
        if (errno != EINPROGRESS) 
		{
            close(fd);
            return -1;
        }

        fd_set wset;    //创建一个存放fd的集合
        FD_ZERO(&wset); //将这个集合清零
        FD_SET(fd, &wset); //在这个集合中写进fd
		
		//创建一个时间结构体  tv  秒         微秒
        struct timeval tv = {timeout_sec, 0};
		
//检索的fd的长度最大为fd+1  不用   监视的集合 不用  等待的时间      
        if (select(fd + 1, NULL, &wset, NULL, &tv) <= 0) 
		{
            close(fd);
            return -1;
        }
        
        int err = 0;
        socklen_t len = sizeof(err);
	//判断socket是否连接成功  具体的协议层   返回值为0则握手成功   返回结果写进err    更新len，实际写了的字节数
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (err != 0) 
		{
            close(fd);
            return -1;
        }
    }

	// 没连接成功非阻塞，系统不卡住，连接成功后阻塞，卡住等待接受消息
    fcntl(fd, F_SETFL, flags);
	//设置阻塞3秒
    struct timeval tv = {3, 0};
	
	//发送或读取缓冲区等待的时间不能超过3秒，超过定义为卡死
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    return fd;
}

//蜂鸣器 
static void buzzer_beep(void)
{
    int fd = open("/dev/beep", O_RDWR);
    if (fd < 0)
	{
        printf("打开蜂鸣器设备失败\n");
        return;
    }
    ioctl(fd, 0, 1);     //响
	printf("beepjiao\n");
    usleep(20000);          //响 0.02 秒
    ioctl(fd, 1, 1);     //关
    printf("beepshutup\n");
	close(fd);
}

//查询当前用户的余额
void ui_update_balance(void)
{
	//没有创建用户
	if (g_current_user[0] == 0) 
	{
		printf("提示：尚未登录，不查询余额\n");
		return;
	}
	//创建套接字    非阻塞连接            ip地址          端口       超时时间
	int sockfd = connect_with_timeout(PAY_SERVER_IP, PAY_SERVER_PORT, 2);
	if (sockfd < 0) 
	{
		printf("查询余额失败，无法连接服务器 %s:%d\n", PAY_SERVER_IP, PAY_SERVER_PORT);
		return;//查询失败就保留标签上一次的显示
	}

	//余额缓冲区
	char msg[96];
	//拼成BALANCE:用户名
	snprintf(msg, sizeof(msg), "BALANCE:%s\n", g_current_user);
	if (send(sockfd, msg, strlen(msg), 0) < 0) 
	{
		printf("余额查询请求发送失败\n");
		close(sockfd);
		return;
	}
	//回复缓冲区
	char reply[64] = {0};
	int n = recv(sockfd, reply, sizeof(reply) - 1, 0);
	close(sockfd);

	//                  前面四位是BAL：
	if (n > 0 && strncmp(reply, "BAL:", 4) == 0) 
	{
		//转换成数字   atoi只获取前面的数字
		int balance = atoi(reply + 4);
		printf("当前余额：%d 元\n", balance);
		//把结果写进余额标签 
		if (ui_Label79 != NULL) {
			lv_label_set_text_fmt(ui_Label79,
				"我的余额:   \n      \n     %d元\n\n", balance);
		}
	} 
	else 
	{
		printf("余额查询未收到回复\n");
	}
}

//tcp远程支付，连接我的服务器，发送需要支付的金额，服务端确认余额支付成功
void charge1cloud(lv_event_t * e)
{
	int amount = checkout_amount();//票款 + 小吃
	if (amount <= 0) 
	{
		printf("提示：请先选择电影票或小吃\n");
		return;
	}

	// 连接服务器                       ip地址          端口号         3秒
	int sockfd = connect_with_timeout(PAY_SERVER_IP, PAY_SERVER_PORT, 3);
	if (sockfd < 0) 
	{
		printf("错误：无法连接服务器 %s:%d,支付失败\n", PAY_SERVER_IP, PAY_SERVER_PORT);
		
		_ui_screen_change(&ui_failed, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_failed_screen_init);
		return;
	}

	// 发送支付请求
	char msg[128];
	snprintf(msg, sizeof(msg), "PAY:%s:%d\n",
						(g_current_user[0] != 0) ? g_current_user : "guest", //第一个打用户名 没有即为访客 
						amount);  //金额

	if (send(sockfd, msg, strlen(msg), 0) < 0)//发送的字节小于0
	{
		printf("错误：发送支付请求失败\n");
		close(sockfd);
		_ui_screen_change(&ui_failed, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_failed_screen_init);
		return;
	}
	printf("已发送支付请求：%s", msg);

	// 回复缓冲区
	char reply[64] = {0};
	//回复返回的字节数
	int n = recv(sockfd, reply, sizeof(reply) - 1, 0);
	close(sockfd);

	if (n > 0 && strncmp(reply, "OK", 2) == 0) 
	{
		//服务端端确认支付，蜂鸣器响一声，保存合并订单
		//蜂鸣器初始化
		buzzer_beep();
		printf("beep\n");
		//标签信息缓冲区
		char desc[256];
		//创建信息标签
		build_order_desc(desc, sizeof(desc));
		//追加方式保存标签
		save_order_record(desc, amount);

		printf("服务端支付成功：%s,扣款 %d 元\n", desc, amount);

		//本次买的座位标记为已售（要在清空座位信息之前记录）
		ui_mark_seat_desc_sold(g_seat_desc);

		food_clear();
		clear_ticket_selection();

		//查询余额，刷新余额标签
		ui_update_balance();
		//跳转成功界面
		_ui_screen_change(&ui_success, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_success_screen_init);
	} 
	else 
	{
		printf("支付失败：服务端未确认（余额不足或超时）\n");
		_ui_screen_change(&ui_failed, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_failed_screen_init);
	}
}

//充值面板，没点击不可见为NULL，点击可见
static lv_obj_t *recharge_bg = NULL;

//关闭充值面板 
static void recharge_close_cb(lv_event_t * e)
{
	if (recharge_bg != NULL) 
	{
		//异步删除，不实时删除，到下一帧删
		//标记为待删除，不直接删除，等lvgl工作流程结束删除
		lv_obj_del_async(recharge_bg);
		recharge_bg = NULL;
	}
}
//充值函数
static void recharge_amount_cb(lv_event_t * e)
{
//获取屏幕上输入的金额并返回出来   void*不能直接转int    先转成intptr在转int    任何指针转intptr数据都不会丢失
	int amount = (int)(intptr_t)lv_event_get_user_data(e);
	
	//创建套接字，连接服务器的ip和端口
	int sockfd = connect_with_timeout(PAY_SERVER_IP, PAY_SERVER_PORT, 2);
	if (sockfd < 0) 
	{
		printf("充值失败：无法连接云服务器 %s:%d\n", PAY_SERVER_IP, PAY_SERVER_PORT);
		return;
	}

	//发送缓冲区
	char msg[96];
	//                     拼成  RECHARGE：用户名： 金额
	snprintf(msg, sizeof(msg), "RECHARGE:%s:%d\n", g_current_user, amount);
	if (send(sockfd, msg, strlen(msg), 0) < 0) 
	{
		printf("充值失败：请求发送失败\n");
		close(sockfd);
		return;
	}

	//回复缓冲区
	char reply[64] = {0};
	//返回读取到的字节数
	int n = recv(sockfd, reply, sizeof(reply) - 1, 0);
	close(sockfd);

	if (n > 0 && strncmp(reply, "OK", 2) == 0) 
	{
		printf("充值成功：%s 充值 %d 元\n", g_current_user, amount);
		ui_update_balance();//刷新余额
		recharge_close_cb(e);//充值成功自动关闭面板
	} 
	else 
	{
		printf("充值失败,replyerror\n");
	}
}

//充值面板
void ui_event_recharge(lv_event_t * e)
{
	//该事件没有被点击
	if (lv_event_get_code(e) != LV_EVENT_CLICKED) 
	{
		return;
	}
	if (g_current_user[0] == 0) 
	{
		printf("尚未登录，无法充值\n");
		return;
	}
	if (recharge_bg != NULL) 
	{
		return;//面板已经打开
	}

	recharge_bg = lv_obj_create(lv_layer_top());
	lv_obj_remove_style_all(recharge_bg);
	lv_obj_set_width(recharge_bg, lv_pct(100));
	lv_obj_set_height(recharge_bg, lv_pct(100));
	lv_obj_set_style_bg_color(recharge_bg, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(recharge_bg, 120, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_clear_flag(recharge_bg, LV_OBJ_FLAG_SCROLLABLE);

	lv_obj_t *panel = lv_obj_create(recharge_bg);
	lv_obj_set_width(panel, 420);
	lv_obj_set_height(panel, 330);
	lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_bg_color(panel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_radius(panel, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_border_width(panel, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

	lv_obj_t *title = lv_label_create(panel);
	lv_label_set_text(title, "充值中心");
	lv_obj_set_style_text_font(title, &ui_font_zt3, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(title, lv_color_hex(0x140202), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

	//四个金额按钮，2 行 2 列
	static const int amounts[4] = {50, 100, 200, 500};
	for (int i = 0; i < 4; i++) 
	{
		lv_obj_t *btn = lv_btn_create(panel);
		lv_obj_set_size(btn, 160, 70);
		lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 25 + (i % 2) * 205, 55 + (i / 2) * 95);

		lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFD54F), LV_PART_MAIN | LV_STATE_DEFAULT);     
		lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0xB71C1C), LV_PART_MAIN | LV_STATE_DEFAULT); 
		lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);          

		lv_obj_add_event_cb(btn, recharge_amount_cb, LV_EVENT_CLICKED, (void *)(intptr_t)amounts[i]);

		lv_obj_t *lab = lv_label_create(btn);
		lv_label_set_text_fmt(lab, "%d元", amounts[i]);
		lv_obj_set_style_text_font(lab, &ui_font_yyyy22, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_center(lab);
	}

	//关闭按钮
	lv_obj_t *close_btn = lv_btn_create(panel);
	lv_obj_set_size(close_btn, 120, 50);
	lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -12);
	lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x9E9E9E), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_add_event_cb(close_btn, recharge_close_cb, LV_EVENT_CLICKED, NULL);

	lv_obj_t *close_lab = lv_label_create(close_btn);
	lv_label_set_text(close_lab, "退出");
	lv_obj_set_style_text_font(close_lab, &ui_font_zt3, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_center(close_lab); 
}

//================================================账号云备份=========================================
//注册成功后把账号备份到服务器（存到服务器的 server_accounts.txt），
//其他板子开机或登录时会自动拉取，实现多板共享账号
void sync_account_to_server(const char *username, const char *password)
{
    int sockfd = connect_with_timeout(PAY_SERVER_IP, PAY_SERVER_PORT, 2);
    if (sockfd < 0)
    {
        printf("账号备份失败：无法连接服务器 %s:%d，账号仅保存在本机\n", PAY_SERVER_IP, PAY_SERVER_PORT);
        return;
    }

    //拼成 SYNC:用户名:密码
    char msg[160];
    snprintf(msg, sizeof(msg), "SYNC:%s:%s\n", username, password);
    if (send(sockfd, msg, strlen(msg), 0) < 0)
    {
        printf("账号备份失败：请求发送失败\n");
        close(sockfd);
        return;
    }

    char reply[32] = {0};
    int n = recv(sockfd, reply, sizeof(reply) - 1, 0);
    close(sockfd);

    if (n > 0 && strncmp(reply, "OK", 2) == 0)
    {
        printf("账号已备份到服务器：%s\n", username);
    }
    else
    {
        printf("账号备份未收到服务器确认，账号仅保存在本机\n");
    }
}

//从服务器拉取全部备份账号，本机没有的账号自动生成账号文件并加入链表，返回新增个数
//在开机时和登录失败时各调用一次，其他板子注册过的账号就能在本机登录
int fetch_server_accounts(void)
{
    int sockfd = connect_with_timeout(PAY_SERVER_IP, PAY_SERVER_PORT, 2);
    if (sockfd < 0)
    {
        printf("拉取账号失败：无法连接服务器 %s:%d\n", PAY_SERVER_IP, PAY_SERVER_PORT);
        return -1;
    }

    if (send(sockfd, "ACCS\n", 5, 0) < 0)
    {
        printf("拉取账号失败：请求发送失败\n");
        close(sockfd);
        return -1;
    }

    //服务器逐行回 ACC:用户名:密码，最后以 ACC_END 结束
    char buf[4096] = {0};
    int len = 0;
    while (strstr(buf, "ACC_END") == NULL && len < (int)sizeof(buf) - 1)
    {
        int n = recv(sockfd, buf + len, sizeof(buf) - 1 - len, 0);
        if (n <= 0) break;      //收完或超时
        len += n;
        buf[len] = 0;
    }
    close(sockfd);

    int added = 0;
    char *line = buf;
    while (line != NULL && *line)
    {
        char *nl = strchr(line, '\n');
        if (nl != NULL) *nl = 0;

        char user[64] = {0}, pass[64] = {0};
        if (sscanf(line, "ACC:%63[^:]:%63s", user, pass) == 2 && strlen(user) > 0)
        {
            char filepath[256];
            snprintf(filepath, sizeof(filepath), "./account/%s.txt", user);

            FILE *check = fopen(filepath, "r");
            if (check != NULL)
            {
                fclose(check);              //本机已有，不用同步
            }
            else
            {
                FILE *fp = fopen(filepath, "w");
                if (fp != NULL)
                {
                    fprintf(fp, "username=%s\npassword=%s\n", user, pass);
                    fclose(fp);
                    node *new_node = create_newnode(filepath);
                    insert(account_list, new_node);
                    added++;
                    printf("已从服务器同步账号：%s\n", user);
                }
            }
        }
        line = (nl != NULL) ? nl + 1 : NULL;
    }
    printf("服务器账号同步完成，本次新增 %d 个\n", added);
    return added;
}

//================================================登录注册=========================================
// 检查文件中的密码是否正确
bool check_password_from_file(const char *filepath, const char *input_username, const char *input_password)
{
    FILE *fp = fopen(filepath, "r");//以只读模式打开文件
    if (fp == NULL) 
	{
        return false;
    }

    char line[128];
    char file_username[64] = {0};
    char file_password[64] = {0};

    while (fgets(line, sizeof(line), fp)) 
	{//从文件中逐行读取

        line[strcspn(line, "\n")] = 0; // 去掉换行符

        if (strncmp(line, "username=", 9) == 0) 
		{
			//比较前九个字符，看是否是username
            strcpy(file_username, line + 9);//跳过九个，把后面的赋值过去
        }
		 else if (strncmp(line, "password=", 9) == 0) 
		{
			//匹配password
            strcpy(file_password, line + 9);//
        }
    }

    fclose(fp);//关闭释放文件

    // 比较用户名和密码  密码账号都正确则返回真
    if (strcmp(file_username, input_username) == 0 && strcmp(file_password, input_password) == 0) 
	{
        return true;
    }

    return false;
}

// 在本地账号链表里查找并验证账号密码，成功则记录登录用户并跳转，返回是否登录成功
static bool try_local_login(const char *input_username, const char *input_password)
{
    // 遍历链表查找账号
    node *tmp = account_list->next;//account_list 链表头指针   结构体指针tmp指向头指针的下一个即第一个头结点

    //链表不为空则循环遍历整个链表
    while (tmp != NULL) {
        // 调用读取账号密码函数从文件中验证用户名和密码
        if (check_password_from_file(tmp->bmp_pathname,input_username, input_password))
        //头结点存的文件路径获取的屏幕输入的账号和密码
            {
            printf("登录成功！欢迎 %s\n", input_username);

            // 记录当前登录用户（订单将保存到该用户的订单文件中）
            snprintf(g_current_user, sizeof(g_current_user), "%s", input_username);

            // 跳转到成功屏幕
            _ui_screen_change(&ui_movie, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_movie_screen_init);

            // 清空输入框
            lv_textarea_set_text(ui_username1, "");
            lv_textarea_set_text(ui_passname, "");
            return true;
        }
        tmp = tmp->next;//指向下一个节点
    }
    return false;
}

void login_login(lv_event_t * e)//登录按钮
{
	// 获取屏幕输入的用户名和密码
    const char *input_username = lv_textarea_get_text(ui_username1);
    const char *input_password = lv_textarea_get_text(ui_passname);
    // 检查输入是否为空      如果没有定义或者值为0    则不通过
    if (input_username == NULL || strlen(input_username) == 0) {
        printf("错误：用户名不能为空！\n");
        _ui_screen_change(&ui_failed, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_failed_screen_init);
        return;
    }
    if (input_password == NULL || strlen(input_password) == 0) {
        printf("错误：密码不能为空！\n");
        _ui_screen_change(&ui_failed, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_failed_screen_init);
        return;
    }

    // 先在本机账号里验证
    if (try_local_login(input_username, input_password)) {
        return;
    }

    // 本机没有该账号：从服务器拉取其他板子备份过的账号，再试一次
    printf("本机没有该账号，尝试从服务器同步账号...\n");
    if (fetch_server_accounts() > 0 && try_local_login(input_username, input_password)) {
        return;
    }

    // 遍历完没找到匹配的账号
    printf("登录失败：账号或密码错误！\n");
    _ui_screen_change(&ui_failed, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_failed_screen_init);

    // 清空输入框
    lv_textarea_set_text(ui_username1, "");
    lv_textarea_set_text(ui_passname, "");
}

void regisandlogin(lv_event_t * e)//注册登录按钮
{

    // 使用注册页面的输入框   获取屏幕获取的账号和密码
    const char *username = lv_textarea_get_text(ui_username2);
    const char *password = lv_textarea_get_text(ui_passname1);
    //判断输入的账号密码是否为空
    if (username == NULL || strlen(username) == 0) {
        printf("error: account is empty\n");
        return;
    }
    if (password == NULL || strlen(password) == 0) {
        printf("error: password is empty\n");
        return;
    }


    //
    struct stat st = {0};
    if (stat("./account", &st) == -1) {
        if (mkdir("./account", 0755) != 0) {
            printf("error: dir create error: %s\n", strerror(errno));
            return;
        }
    }

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "./account/%s.txt", username);

    FILE *check_file = fopen(filepath, "r");
    if (check_file != NULL) {
        fclose(check_file);
        printf("error: account %s already exists\n", username);
        return;
    }

    FILE *fp = fopen(filepath, "w");
    if (fp == NULL) {
        printf("error: failed to create file %s: %s\n", filepath, strerror(errno));
        return;
    }

    fprintf(fp, "username=%s\n", username);
    fprintf(fp, "password=%s\n", password);

    fclose(fp);
    printf("注册成功！账号 %s 已保存到 %s\n", username, filepath);

    // 将新账号文件加入链表
    node *new_node = create_newnode(filepath);
    insert(account_list, new_node);

    // 备份一份到服务器，其他板子拉取后即可用该账号登录
    sync_account_to_server(username, password);

    // 清空输入框
    lv_textarea_set_text(ui_username2, "");
    lv_textarea_set_text(ui_passname1, "");

    // 记录当前登录用户（订单将保存到该用户的订单文件中）
    snprintf(g_current_user, sizeof(g_current_user), "%s", username);

    // 直接登录，跳转到功能页面
    printf("自动登录成功！欢迎 %s\n", username);
    _ui_screen_change(&ui_movie, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_movie_screen_init);
}
