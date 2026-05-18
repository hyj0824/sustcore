# librpc

sustcore 远程过程调用(RPC)库, 规定了RPC协议, 以及封装该协议的库函数.

## RPC 协议

sustcore RPC协议基于sustcore IPC, 其中 sustcore IPC 消息包的结构为: (见 [sustcore/msg.h](../../include/sustcore/msg.h))

``` cpp
struct MsgPacket {
    /// 消息数据缓冲区.
    void *msgbuf;
    /// 指向消息字节数的用户态地址.
    size_t *msgsz;
    /// Capability索引列表缓冲区.
    CapIdx *caplist;
    /// 指向Capability数量的用户态地址.
    size_t *capsz;
};
```

其中主要信息存放在 `msgbuf` 中, RPC协议规定了 `msgbuf` 中的调用消息数据格式如下:

``` cpp
service_magic: u32,     // 只有在 service_magic 与该服务匹配的情况下, 才会被该服务处理, 否则消息将被拒收, 立即返回一个 ErrorResponse
packet_type: u32,       // 消息包类型 (RPC_CALL)
session_id: u32,        // 调用所在的会话ID
function_id: u32,       // 要调用的 RPC 函数ID, 由服务定义, 由客户端调用时指定
args_cnt: u32,          // RPC 函数参数数量, 由服务定义, 由客户端调用时指定
types: u32[args_cnt],   // RPC 函数参数类型列表, 由服务定义, 由客户端调用时指定
args_sz: u32,           // RPC 函数参数数据字节数, 由服务定义, 由客户端调用时指定
args: bytes,            // RPC 函数参数数据, 由服务定义, 由客户端调用时指定
```

其相应的响应消息数据格式如下:

``` cpp
service_magic: u32,       // 只有在 service_magic 与该服务匹配的情况下, 才是一个合适的 RPC 响应
packet_type: u32,         // 消息包类型 (RPC_RESPONSE)
session_id: u32,          // 调用所在的会话ID
function_id: u32,         // 响应的 RPC 函数ID,
return_type: u32,         // RPC 函数返回值类型, 由服务定义, 由服务器响应时指定
return_sz: u32,           // RPC 函数返回值数据字节数, 由服务定义, 由服务器响应时指定
return_value: bytes,      // RPC 函数返回值数据, 由服务定义, 由服务器响应时指定
``

其中对于 `types`, RPC协议规定了以下几种基本类型:

``` cpp
integral types:
    0: uint8_t, (unsigned char)
    1: uint16_t, (unsigned short)
    2: uint32_t, (unsigned int)
    3: uint64_t,
    4: int8_t, (char)
    5: int16_t, (short)
    6: int32_t, (int)
    7: int64_t,
floating-point types:
    8: float,
    9: double,
misc types:
    10: size_t,
    11: off_t,
    12: bool,
```

除此之外, RPC协议还允许用户自定义 POD 数据类型,
其类型ID由服务器定义.

每个 `type` 段由两个部分组成: 高16位为数组长度, 低16位为基本类型ID.
当高 16 位为0时, 表示该参数为一个基本类型, 其类型由低16位指定.
当高 16 位不为0时, 表示该参数为一个数组, 其元素类型由低16位指定, 数组长度由高16位指定, 因此最高支持的数组长度为65535.

RPC 服务通过解析 `msgbuf` 中的消息数据, 来获取要调用的函数ID, 参数类型列表, 参数数据等信息, 从而执行相应的操作.
一个合适的消息应当满足:
$arg size = \sum_{i=0}^{n-1} sizeof(type_i) * count_i$

最终 RPC 服务从包中读取数据并将其传递给 RPC 服务使用. 消息包的存活周期直到该消息得到回应, 其中数组类参数以指针形式传递.

RPC 协议规定了 `msgbuf` 中的会话消息数据格式如下:

``` cpp
service_magic: u32,     // 只有在 service_magic 与该服务匹配的情况下, 才会被该服务处理, 否则消息将被拒收, 立即返回一个 ErrorResponse
packet_type: u32,       // 消息包类型 (RPC_SESSION)
```

其相应的响应消息数据格式如下:

``` cpp
service_magic: u32,     // 只有在 service_magic 与该服务匹配的情况下, 才是一个合适的 SESSION 响应
packet_type: u32,       // 消息包类型 (RPC_SESSION_RESPONSE)
session_id: u32,        // 调用所在的会话ID, 由服务器生成
```

会话消息将会建立一个长期的 RPC 会话, 该会话将会持续存在直到客户端或服务器主动关闭它, 期间客户端可以通过该会话发送多个RPC调用消息, 服务器也可以通过该会话发送多个RPC响应消息.

此外还有结束会话消息, 其数据格式如下:

``` cpp
service_magic: u32,     // 只有在 service_magic 与该服务匹配的情况下, 才会被该服务处理, 否则消息将被拒收,
packet_type: u32,       // 消息包类型 (RPC_SESSION_END)
session_id: u32,        // 要结束的会话ID
```

以及相应的响应消息数据格式如下:

``` cpp
service_magic: u32,     // 只有在 service_magic 与该服务匹配的情况下, 才是一个合适的 SESSION_END 响应
packet_type: u32,       // 消息包类型 (RPC_SESSION_END_RESPONSE)
session_id: u32,        // 要结束的会话ID
```

## RPC 库函数

如果接入 C++26 的静态反射系统, 则以下代码将是预想中 RPC 服务器的实现方式:

``` cpp
class MyServiceInterface {
public:
    [[=rpc::service_name]]
    constexpr static const char *SERVICE_NAME = "MyService";
    [[=rpc::service_magic]]
    constexpr static b32 SERVICE_MAGIC = std::hash<>(SERVICE_NAME);
    [[=rpc::expose]]
    virtual int add(int a, int b) = 0;
};

class MyServiceImpl : public MyServiceInterface, rpc::Service<MyServiceInterface> {
public:
    int add(int a, int b) override {
        return a + b;
    }
};

class MyServiceClient : public MyServiceInterface, rpc::Client<MyServiceInterface> {
public:
    int add(int a, int b) override
    {
        return rpc::Client::call<&MyServiceInterface::add>(a, b);
    }
};
```

服务端的接收逻辑则变为

```cpp
int mainloop()
{
    MyServiceImpl service_impl;

    rpc::Message msg;
    rpc::Message reply;
    rpc::receive(&msg);

    while (true)
    {
        service_impl.dispatch(&msg, &reply);
        rpc::replyAndReceive(&msg, &reply);
    }
}
```