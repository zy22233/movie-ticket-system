# 影票自助售票机（buymovie_arm）

## 技术栈

C · LVGL 8.3 · Linux framebuffer 直驱 / evdev · TCP Socket · pthread · CMake（arm-linux-gcc 交叉编译）· 巴法云 IoT

## 核心功能 / 亮点

- 实现了 LVGL 直驱 framebuffer + evdev 的触摸售票 UI，13 个页面完整购票闭环：选电影 → 选场次座位 → 卖品加购 → 确认下单 → 支付出票 → 查看订单
- 实现了自研 TCP 文本协议的云端支付服务器（扣款 / 充值 / 余额查询），余额表落盘持久化，服务器重启不丢账
- 实现了多台售票机共享账号：注册账号通过 SYNC 备份到云端，其他板子用 ACCS 拉取后即可登录
- 解决了网络请求卡死 UI 的问题：socket 非阻塞 connect + select 超时，支付 / 充值失败可安全回退
- 解决了重复售票问题：已售座位持久化到 `sold_seats.txt`，再次进入选座页自动置灰不可选
- 解决了嵌入式 UI 卡顿问题：天气刷新、云端消息接收、心跳保活、LED 流水灯全部放到后台线程，与 LVGL 主循环隔离

## 演示视频

[点击观看完整演示（B 站）](https://www.bilibili.com/video/BV1yyaR6CEps/)

## 快速启动

bash
mkdir build && cd build
cmake ..
make -j8

## 截图

| 主界面 | 选座（已售置灰） |
|:---:|:---:|
| ![主界面](https://github.com/user-attachments/assets/5c2d326f-4072-47b2-bf7b-c98258488ebe) | ![选座](https://github.com/user-attachments/assets/34aafb0e-e733-4bea-8e10-4bf70aa3cd9d) |

| 卖品 | 支付成功 |
|:---:|:---:|
| ![卖品](https://github.com/user-attachments/assets/0d8e2873-bf46-44b2-bcee-81f5981f18ee) | ![支付](https://github.com/user-attachments/assets/7df8e57b-4839-44d8-a335-a3fde9ffb73a) |


| 充值 | 订单 |
|:---:|:---:|
| ![充值](https://github.com/user-attachments/assets/ff094438-c8e5-4a0e-ae58-d7cd980d132b) | ![订单](https://github.com/user-attachments/assets/16f45783-0de9-4198-a09a-d1e46f693902) |

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
