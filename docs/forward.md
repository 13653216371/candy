# 服务端中继

## 最简单的 TUN 使用方式

参考 [simpletun](https://github.com/gregnietsky/simpletun) 实现最简单的 P2P 连接.这里我们命名两个设备为 Client A 和 client B.

```plaintext
┌──────────┐  ┌──────────┐
│ Client A ├──┤ Client B │
└──────────┘  └──────────┘
```

## 服务端转发流量

在最简单的 P2P 连接基础上,添加一个中间设备进行流量转发,不改变报文内容,对于两个客户端来说与上一个场景的流量没有任何区别.

```plaintext
┌──────────┐  ┌────────┐  ┌──────────┐
│ Client A ├──┤ Server ├──┤ Client B │
└──────────┘  └────────┘  └──────────┘
```

## 添加路由功能

服务端添加路由功能,记录每个客户端虚拟地址与连接之间的映射关系,分析原始 IPv4 报文的目的地址,并通过映射找到对应连接发送数据.

```plaintext
              ┌──────────┐
              │ Client D │
              └────┬─────┘
                   │
┌──────────┐   ┌───┴────┐   ┌──────────┐
│ Client A ├───┤ Server ├───┤ Client B │
└──────────┘   └───┬────┘   └──────────┘
                   │
              ┌────┴─────┐
              │ Client C │
              └──────────┘
```
