# 影票自助售票机（buymovie_arm）

在线演示：<!-- 有录屏/演示视频就放这里，如 [B站演示视频](https://xxx) -->

## 技术栈

C · LVGL 8.3 · Linux framebuffer / evdev · TCP Socket · pthread · CMake（arm-linux-gcc 交叉编译）· 巴法云 IoT

## 核心功能 / 亮点

- 实现了 LVGL 直驱 framebuffer + evdev 的触摸售票 UI，13 个页面完整购票闭环：选电影 → 选场次座位 → 卖品加购 → 确认下单 → 支付出票 → 查看订单
- 实现了自研 TCP 文本协议的云端支付服务器（扣款 / 充值 / 余额查询），余额表落盘持久化，服务器重启不丢账
- 实现了多台售票机共享账号：注册账号通过 SYNC 备份到云端，其他板子用 ACCS 拉取后即可登录
- 解决了网络请求卡死 UI 的问题：socket 非阻塞 connect + select 超时，支付 / 充值失败可安全回退
- 解决了重复售票问题：已售座位持久化到 `sold_seats.txt`，再次进入选座页自动置灰不可选
- 解决了嵌入式 UI 卡顿问题：天气刷新、云端消息接收、心跳保活、LED 流水灯全部放到后台线程，与 LVGL 主循环隔离

## 快速启动

```bash
git clone https://github.com/你的账号/buymovie_arm.git
cd buymovie_arm

# 1. 云服务器上编译支付服务器（任意 Linux）
gcc pay_server.c -o pay_server
./pay_server                # 默认监听 8888 端口

# 2. 交叉编译客户端并烧到开发板（依赖 arm-linux-gcc 5.4.0 + CMake ≥ 3.28）
mkdir -p build && cd build
cmake .. && make -j4

# 3. 开发板上运行
./buymovie_arm
```

> 记得把 `UI/ui_event.c` 顶部的 `PAY_SERVER_IP` / `PAY_SERVER_PORT` 改成你的云服务器地址。

## 截图

<!-- 截图占位：建议放 主界面 / 选座 / 卖品 / 支付成功 / 订单 五张，两列排布 -->

<details>
<summary>更多：配置说明与通信协议</summary>

### 配置说明

| 配置项 | 位置 | 说明 |
| --- | --- | --- |
| 支付服务器地址 | `UI/ui_event.c` 顶部 `PAY_SERVER_IP` / `PAY_SERVER_PORT` | 改成你的云服务器 IP 和端口 |
| 巴法云接入 | `main.c` 中 `bemfa_Client()` 调用处的 `uid` 与 `topic` | 订阅 `deng` 主题，用于消息下发与 LED 控制 |
| 显示分辨率 | `main.c` 中 `hor_res` / `ver_res` | 默认 800×480，按屏改 |
| 新用户默认余额 | `pay_server.c` 中 `DEFAULT_BALANCE` | 默认 1000 元 |
| 影厅座位数 | `UI/ui_event.c` 中 `SEAT_ROWS` / `SEAT_COLS` | 默认 5×10 |

### 通信协议（售票机 ↔ 支付服务器）

TCP 明文协议，每条命令以 `\n` 结尾，服务器逐条应答：

| 客户端发送 | 服务器回复 | 说明 |
| --- | --- | --- |
| `PAY:用户名:金额` | `OK` / `NO` | 扣款，余额不足回 `NO` |
| `RECHARGE:用户名:金额` | `OK` | 充值 |
| `BALANCE:用户名` | `BAL:当前余额` | 查询余额 |
| `SYNC:用户名:密码` | `OK` | 备份账号到服务器 |
| `ACCS` | 逐行 `ACC:用户名:密码`，最后 `ACC_END` | 拉取全部备份账号 |
| 其他 | `ERR` | 未知命令 |

</details>
