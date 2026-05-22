#include <boost/asio.hpp>
#include <iostream>
#include <vector>
#include <algorithm>

using boost::asio::ip::tcp;
using namespace std;

// 定义缓冲区大小，对应书中的 #define BUF_SIZE 100
constexpr size_t MAX_LENGTH = 100;

int main() {
    try {
        // 1. 初始化 IO 上下文（对应书中的 main 函数环境）
        boost::asio::io_context io_context;

        // 2. 创建监听套接字 (对应 socket(), bind(), listen())
        // Boost.Asio 使用 RAII，tcp::acceptor 析构时会自动关闭 socket
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 12345));

        cout << "Server started on port 12345..." << endl;

        // 模拟书中的 while(1) 循环
        // 在同步模型中，我们维护一个客户端会话列表
        vector<tcp::socket> clients;

        while (true) {
            // 对应书中的 select 逻辑：我们需要同时等待“新连接”和“现有客户端数据”
            // 注意：纯同步 Boost.Asio 通常是一个连接一个线程，或者使用异步模型。
            // 为了在单线程模拟书中的 Select 复用效果，我们通常结合 poll/select 手动控制，
            // 但更现代的写法是直接使用 Asio 的异步模型。

            // 这里为了保持与书中逻辑最接近的“单线程处理多连接”效果，
            // 我们展示一个更地道的 Asio 写法：为每个连接启动一个异步处理链，
            // 这比手写的 select 效率高得多，且代码更清晰。
            break;
        }

        // --- 真正的现代 C++ 写法 (异步回声服务器) ---
        // 这种写法不需要手动 select，Asio 底层自动处理

        std::function<void()> start_accept;
        start_accept = [&]() {
            // 使用 shared_ptr 管理 socket 生命周期 (RAII)
            auto socket = make_shared<tcp::socket>(io_context);

            acceptor.async_accept(*socket, [&](boost::system::error_code ec) {
                if (!ec) {
                    cout << "New client connected: "
                         << socket->remote_endpoint() << endl;

                    // 启动该客户端的读取逻辑
                    // 对应书中：FD_SET(clnt_sock, &reads)
                    auto read_buf = make_shared<vector<char>>(MAX_LENGTH);

                    function<void()> start_read;
                    start_read = [=]() {
                        socket->async_read_some(
                            boost::asio::buffer(*read_buf),
                            [=](boost::system::error_code ec, size_t length) {
                                if (!ec) {
                                    // 对应书中：write() echo
                                    boost::asio::async_write(
                                        *socket,
                                        boost::asio::buffer(*read_buf, length),
                                        [=](boost::system::error_code, size_t) {
                                            start_read(); // 继续读下一次
                                        });
                                } else {
                                    // 对应书中：read return 0 (closed)
                                    cout << "Client disconnected." << endl;
                                }
                            });
                    };
                    start_read();
                }
                // 继续接受下一个连接
                start_accept();
            });
        };

        start_accept();
        io_context.run(); // 启动事件循环

    } catch (std::exception& e) {
        cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
