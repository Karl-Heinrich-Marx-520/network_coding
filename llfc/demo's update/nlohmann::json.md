将自定义的二进制 TLV 协议替换为 `nlohmann::json` 是一个非常实用的现代化改造。

在 TCP 传输 JSON 时，最大的挑战是**JSON 是纯文本，没有内置的消息边界**（接收方不知道一个 JSON 对象在哪里结束）。工业界最优雅的解决方案是使用 **NDJSON (Newline Delimited JSON)**，即**每个 JSON 对象以换行符 `\n` 结尾**。配合 Asio 的 `async_read_until`，我们可以完美解决粘包问题。

以下是使用 `nlohmann::json` 重构后的完整代码。

### 1. 准备工作
请确保您的项目中包含了 `nlohmann/json.hpp`（可以通过 vcpkg、conan 安装，或直接下载单头文件放入项目）。

### 2. 重写 Server.h
我们移除了 `MsgNode` 和复杂的二进制缓冲区，改为使用 Asio 的 `streambuf` 来处理基于分隔符的文本流。

```cpp
#pragma once
#include <iostream>
#include <string>
#include <unordered_map>
#include <deque>
#include <memory>
#include <boost/asio.hpp>
#include <nlohmann/json.hpp> // 引入 JSON 库

using boost::asio::ip::tcp;
using json = nlohmann::json;

class Session;

class Server : public std::enable_shared_from_this<Server> {
public:
    Server(boost::asio::io_context& ioc, unsigned short port);
    ~Server() = default;

    void Start();
    void Stop();
    void RemoveSession(const std::string& uuid);

private:
    void DoAccept();
    
    std::unordered_map<std::string, std::shared_ptr<Session>> _sessions;
    tcp::acceptor _acceptor;
    boost::asio::io_context& _ioc;
};

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket, std::weak_ptr<Server> server);
    ~Session() = default;

    void Start();
    void Close(const std::string& reason, boost::system::error_code ec = {});
    
    // 【重构】发送接口直接接收 JSON 对象
    void Send(const json& j_msg); 
    
    const std::string& GetUuid() const;

private:
    void DoWrite();
    // 【重构】使用 async_read_until 读取直到换行符
    void DoRead(); 
    
    // 业务分发
    void HandleMessage(const json& j_msg); 

    std::weak_ptr<Server> _server;
    std::string _uuid;
    tcp::socket _socket;
    
    // 【重构】使用 streambuf 处理文本流和粘包
    boost::asio::streambuf _read_buf; 
    std::deque<std::string> _send_queue;
    bool _is_writing = false;
    bool _is_closed = false;
};
```

### 3. 重写 Server.cpp
重点观察 `DoRead` 中 `async_read_until` 的用法，以及它如何巧妙地处理 TCP 粘包（多读的数据会自动保留在 `_read_buf` 中供下次使用）。

```cpp
#include "Server.h"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

//=========================================Server===============================================
Server::Server(boost::asio::io_context& ioc, unsigned short port) 
    : _acceptor(ioc, tcp::endpoint(tcp::v4(), port)), _ioc(ioc) {}

void Server::Start() { DoAccept(); }

void Server::RemoveSession(const std::string& uuid) {
    _sessions.erase(uuid);
    std::cout << "[Server] remove session: " << uuid << "\n";
}

void Server::Stop(){
    boost::system::error_code ec;
    _acceptor.close(ec);
    auto sessions_to_close = std::move(_sessions);
    for (auto& [uuid, session] : sessions_to_close) {
        session->Close("Server Close", ec);
    }
}

void Server::DoAccept() {
    _acceptor.async_accept(
        [this, self = shared_from_this()](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                auto session = std::make_shared<Session>(std::move(socket), weak_from_this());
                session->Start();
                _sessions[session->GetUuid()] = session;
                std::cout << "[Server] new connection: " << session->GetUuid() << "\n";
                DoAccept();
            } else if (ec == boost::asio::error::operation_aborted) {
                std::cout << "[Server] Acceptor stopped.\n";
            } else {
                std::cerr << "[Server] Accept error: " << ec.message() << "\n";
                DoAccept();
            }
        }
    );
}

//===========================================Session================================
Session::Session(tcp::socket socket, std::weak_ptr<Server> server) 
    : _socket(std::move(socket)), _server(server) {
    static thread_local boost::uuids::random_generator gen;
    _uuid = boost::uuids::to_string(gen());
}

const std::string& Session::GetUuid() const { return _uuid; }

void Session::Start() { DoRead(); }

// 【重构】Send：将 JSON 序列化为字符串，并追加换行符 `\n` 作为边界
void Session::Send(const json& j_msg) {
    boost::asio::post(
        _socket.get_executor(),
        [this, self = shared_from_this(), msg = j_msg]() {
            if (_is_closed) return;
            
            try {
                // dump() 将 JSON 转为紧凑字符串，+ "\n" 作为消息分隔符
                std::string packet = msg.dump() + "\n"; 

                bool write_in_progress = !_send_queue.empty();
                _send_queue.push_back(std::move(packet));

                if (!write_in_progress) {
                    DoWrite();
                }
            } catch (const std::exception& e) {
                std::cerr << "[Session " << _uuid << "] JSON dump error: " << e.what() << "\n";
            }
        }
    );
}

void Session::DoWrite() {
    if (!_socket.is_open() || _is_closed) {
        _is_writing = false;
        return;
    }
    _is_writing = true;
    boost::asio::async_write(
        _socket,
        boost::asio::buffer(_send_queue.front()),
        [this, self = shared_from_this()](boost::system::error_code ec, size_t) {
            if (!ec) {
                _send_queue.pop_front();
                if (!_send_queue.empty()) {
                    DoWrite();
                } else {
                    _is_writing = false;
                }
            } else {
                Close("Write", ec);
            }
        }
    );
}

void Session::Close(const std::string& reason, boost::system::error_code ec) {
    if (_is_closed) return;
    _is_closed = true;

    if (ec) std::cerr << "[Session " << _uuid << "] " << reason << " Error: " << ec.message() << "\n";
    else std::cout << "[Session " << _uuid << "] Closed normally.\n";

    _socket.close(ec);
    if (auto server = _server.lock()) {
        server->RemoveSession(_uuid);
    }
}

// 【重构】DoRead：核心魔法，利用 async_read_until 自动处理粘包/半包
void Session::DoRead() {
    boost::asio::async_read_until(
        _socket,
        _read_buf,
        '\n', // 告诉 Asio：一直读，直到遇见换行符
        [this, self = shared_from_this()](boost::system::error_code ec, std::size_t bytes_transferred) {
            if (!ec) {
                // 1. 从 streambuf 中提取完整的一行（包含 \n）
                std::istream is(&_read_buf);
                std::string line;
                std::getline(is, line); // getline 会自动剥离 \n

                // 2. 解析 JSON
                try {
                    json j_msg = json::parse(line);
                    HandleMessage(j_msg);
                } catch (const json::parse_error& e) {
                    std::cerr << "[Session " << _uuid << "] JSON Parse Error: " << e.what() << "\n";
                    // 忽略错误包，继续读取下一个
                }

                // 3. 递归调用，等待下一个 \n
                // 注意：如果 TCP 一次性送达了多条消息（粘包），_read_buf 中会残留剩余数据，
                // async_read_until 会立即触发，不会丢失数据！
                DoRead(); 
            } else {
                Close("Read", ec);
            }
        }
    );
}

// 业务逻辑分发层
void Session::HandleMessage(const json& j_msg) {
    // 学习点：利用 JSON 自带的 type 字段进行路由，无需二进制 Header
    if (!j_msg.contains("type")) {
        std::cerr << "[Session " << _uuid << "] Missing 'type' field.\n";
        return;
    }

    std::string type = j_msg["type"].get<std::string>();

    if (type == "heartbeat") {
        std::cout << "[Session " << _uuid << "] Recv Heartbeat.\n";
    } 
    else if (type == "chat") {
        std::string content = j_msg.value("content", ""); // 安全取值
        std::cout << "[Session " << _uuid << "] Recv Chat: " << content << "\n";
        
        // 模拟 Echo 回复
        json reply;
        reply["type"] = "chat";
        reply["content"] = "Server Echo: " + content;
        Send(reply);
    } 
    else if (type == "login") {
        std::string user = j_msg.value("username", "unknown");
        std::cout << "[Session " << _uuid << "] User Login: " << user << "\n";
    } 
    else {
        std::cerr << "[Session " << _uuid << "] Unknown type: " << type << "\n";
    }
}
```

### 核心重构亮点解析：

1. **消灭了 `MsgNode` 和 字节序转换**：
   JSON 是纯文本，天然跨平台、跨语言，彻底告别了 `htons/ntohs` 和手动拼接二进制 Header 的痛苦。
2. **`async_read_until` 的粘包处理魔法**：
   当客户端瞬间发送 `{"type":"chat"}\n{"type":"login"}\n` 时，`async_read_until` 会一次性把它们都读进 `_read_buf`。
   `std::getline` 提取出第一行后，**第二行数据依然安全地留在 `_read_buf` 中**。当代码再次调用 `DoRead()` 时，Asio 发现 buffer 里已经有 `\n` 了，会**零网络 I/O 立即触发回调**，完美且高效地处理了粘包。
3. **更灵活的业务扩展**：
   在 `HandleMessage` 中，我们使用 `j_msg.value("content", "")` 这种安全取值方式。即使客户端少传了字段，服务器也不会崩溃（如果是二进制结构体，字段错位会导致直接读取到脏内存）。

### 客户端测试建议：
您可以使用 Python 或 Node.js 快速编写一个测试客户端，只需向服务器发送带 `\n` 的字符串即可：
```python
# Python 测试脚本
import socket, json, time

s = socket.socket()
s.connect(('127.0.0.1', 8080))

# 发送 JSON，切记加上 \n
msg1 = json.dumps({"type": "login", "username": "Alice"}) + "\n"
msg2 = json.dumps({"type": "chat", "content": "Hello Asio"}) + "\n"

s.sendall(msg1.encode())
time.sleep(0.1)
s.sendall(msg2.encode())

# 接收回复
print(s.recv(1024).decode())
```
