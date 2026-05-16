好的，我将你提供的 **传统 C 语言** 的服务器/客户端代码与 **现代 C++ (Boost.Asio)** 的实现并列展示，并进行详细的对比复述。

---

### 服务端代码对比

#### 传统 C 语言风格 (基于你的图片)

这是你书中第 67-68 页的代码，使用了标准的 BSD Socket API。

```c
/* hello_server.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

void error_handling(char *message);

int main(int argc, char *argv[])
{
    int serv_sock;
    int clnt_sock;

    struct sockaddr_in serv_addr;
    struct sockaddr_in clnt_addr;
    socklen_t clnt_addr_size;

    char message[] = "Hello World!";

    // 1. 参数检查
    if (argc != 2) {
        printf("Usage : %s <port>\n", argv[0]);
        exit(1);
    }

    // 2. 创建套接字 (Socket)
    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (serv_sock == -1)
        error_handling("socket() error");

    // 3. 初始化地址结构体 (Bind准备)
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 任意IP
    serv_addr.sin_port = htons(atoi(argv[1]));     // 端口转换

    // 4. 绑定 (Bind)
    if (bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("bind() error");

    // 5. 监听 (Listen)
    if (listen(serv_sock, 5) == -1)
        error_handling("listen() error");

    // 6. 接受连接 (Accept)
    clnt_addr_size = sizeof(clnt_addr);
    clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
    if (clnt_sock == -1)
        error_handling("accept() error");

    // 7. 写数据 (Write)
    write(clnt_sock, message, sizeof(message));

    // 8. 关闭连接 (Close)
    close(clnt_sock);
    close(serv_sock);
    return 0;
}

void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}
```

#### 现代 C++ Boost.Asio 风格

```cpp
/* hello_server_cpp.cpp */
#include <boost/asio.hpp>
#include <iostream>
#include <string>

using boost::asio::ip::tcp;

int main(int argc, char* argv[]) {
    try {
        // 1. 参数检查
        if (argc != 2) {
            std::cerr << "Usage: " << argv[0] << " <port>\n";
            return 1;
        }

        // 2. 创建 I/O 上下文 (管理所有资源)
        boost::asio::io_context io_context;

        // 3. 创建端点并监听 (对应 Socket + Bind + Listen)
        // 现代风格：直接指定版本(tcp::v4())和端口，构造函数内部自动完成 bind 和 listen
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), std::stoi(argv[1])));

        // 4. 接受连接 (Accept)
        tcp::socket socket(io_context); // 准备一个socket对象
        acceptor.accept(socket);        // 阻塞直到有连接进来，自动填充 socket

        // 5. 写数据 (Write)
        std::string message = "Hello World!";
        boost::asio::write(socket, boost::asio::buffer(message));

    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}
```

---

### 客户端代码对比

#### 传统 C 语言风格 (基于你的图片)

这是你书中展示的客户端代码。

```c
/* hello_client.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

void error_handling(char *message);

int main(int argc, char *argv[])
{
    int sock;
    struct sockaddr_in serv_addr;
    char message[30];
    int str_len;

    // 1. 参数检查
    if (argc != 3) {
        printf("Usage : %s <IP> <port>\n", argv[0]);
        exit(1);
    }

    // 2. 创建套接字
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        error_handling("socket() error");

    // 3. 初始化服务器地址结构
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);   // IP 字符串转二进制
    serv_addr.sin_port = htons(atoi(argv[2]));        // 端口转网络字节序

    // 4. 发起连接 (Connect)
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("connect() error");

    // 5. 读数据 (Read)
    str_len = read(sock, message, sizeof(message) - 1);
    if (str_len == -1)
        error_handling("read() error");

    printf("Message from server: %s \n", message);

    // 6. 关闭 (Close)
    close(sock);
    return 0;
}

void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}
```

#### 现代 C++ Boost.Asio 风格

```cpp
/* hello_client_cpp.cpp */
#include <boost/asio.hpp>
#include <iostream>
#include <string>

using boost::asio::ip::tcp;

int main(int argc, char* argv[]) {
    try {
        // 1. 参数检查
        if (argc != 3) {
            std::cerr << "Usage: " << argv[0] << " <IP> <port>\n";
            return 1;
        }

        // 2. 创建 I/O 上下文
        boost::asio::io_context io_context;

        // 3. 解析地址 (Resolver)
        // 现代风格：不需要手动 memset 或调用 inet_addr
        // resolver 会自动处理 DNS 解析和地址结构转换
        tcp::resolver resolver(io_context);
        auto endpoints = resolver.resolve(argv[1], argv[2]);

        // 4. 创建 Socket 并连接
        tcp::socket socket(io_context);
        // connect 会自动尝试 endpoints 列表中的地址直到成功
        boost::asio::connect(socket, endpoints);

        // 5. 读数据 (Read)
        char buffer[30];
        // read_some 是同步读取，直到收到数据
        size_t len = socket.read_some(boost::asio::buffer(buffer));

        // 6. 打印结果
        std::cout << "Message from server: " << std::string(buffer, len) << std::endl;

    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}
```

---

### 详细对比与复述

通过对比这四份代码，我们可以总结出从传统 C 到现代 C++ 的进化主要体现在以下几个方面：

#### 资源管理 (RAII vs 手动管理)

- **传统 C**: 你需要手动调用 `socket()` 获取一个整数描述符，并在最后必须记得调用 `close()`。如果中间出错忘记关闭，就会造成资源泄漏（文件描述符泄漏）。
- **现代 C++**: 使用了 **RAII** 机制。`tcp::socket`、`tcp::acceptor`、`tcp::resolver` 都是对象。当这些对象超出作用域（例如函数结束或发生异常）时，析构函数会自动调用底层的 `close`。你不需要写 `close()`，也不需要写 `free()`。

#### 地址处理 (封装 vs 结构体)

- **传统 C**: 代码中充斥着 `struct sockaddr_in`，你需要手动 `memset` 清零，手动调用 `htonl`、`htons` 处理字节序（大端/小端），手动调用 `inet_addr` 转换 IP 字符串。这不仅繁琐而且容易出错。
- **现代 C++**:
    - **服务端**: `tcp::endpoint(tcp::v4(), port)` 一行代码就搞定了协议版本和端口绑定。
    - **客户端**: 引入了 `resolver`（解析器）。你只需要给它 IP 字符串和端口字符串，它会在内部处理所有复杂的地址解析和字节序转换，返回一个可以直接使用的端点列表。

#### 错误处理 (异常 vs 返回码)

- **传统 C**: 每个系统调用（`socket`, `bind`, `listen`, `accept`...）都必须检查返回值是否为 `-1`。代码中充斥着 `if (xxx == -1) error_handling(...)`，这使得核心逻辑被大量的错误检查代码淹没。
- **现代 C++**: 使用 **C++ 异常**。如果出错（比如端口被占用、网络不通），Boost.Asio 会抛出 `std::system_error` 异常。我们可以用 `try-catch` 块将所有逻辑包起来，代码看起来非常干净，只关注成功的流程。

#### 读写操作 (流 vs 文件描述符)

- **传统 C**: 使用 `read()` 和 `write()` 函数，操作的是整数型的文件描述符。
- **现代 C++**: 使用 `boost::asio::read/write` 或 socket 对象的成员函数。配合 `boost::asio::buffer`，它可以更好地处理缓冲区大小和数据类型，甚至可以方便地扩展为异步非阻塞模式。

### 总结

**传统 C 代码** 就像是**手动挡汽车**：你需要踩离合（socket）、挂挡（bind）、给油（listen）、松手刹（accept），每一步都要亲力亲为，一旦操作失误（忘记 close 或搞错字节序）车就会熄火（程序崩溃或 Bug）。

**现代 C++ (Boost.Asio) 代码** 就像是**自动挡汽车**：你只需要告诉它去哪（Resolver）、踩油门（Connect/Write），底层的复杂机械操作都被封装在库内部了，不仅代码量少了一半，而且更安全、更易读。
