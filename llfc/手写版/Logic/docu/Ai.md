### 🚨 现有 `LogicSystem` 的核心问题诊断
---

### 🛠️ 重构后的 `LogicSystem` (仅修改这两份文件)

我为你重写了 `LogicSystem.h` 和 `LogicSystem.cpp`，采用了现代 C++ 的并发范式，修复了所有错误，并完美适配你 `Server.cpp` 中的调用方式。

#### 1. `LogicSystem.h`
```cpp
#pragma once
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <functional>
#include <atomic>
#include "Singleton.h"
#include "Server.h" 

// 消息 ID 枚举 (替代硬编码)
enum MSG_IDS : short {
    MSG_HELLO_WORD = 1001
};

// 逻辑节点：持有智能指针，避免 RecvNode 内部 string 的深拷贝
struct LogicNode {
    LogicNode(std::shared_ptr<Session> session, std::shared_ptr<RecvNode> recvnode)
        : _session(std::move(session)), _recvnode(std::move(recvnode)) {}
        
    std::shared_ptr<Session> _session;   
    std::shared_ptr<RecvNode> _recvnode; 
};

class LogicSystem : public Singleton<LogicSystem> {
    friend class Singleton<LogicSystem>;
public:
    ~LogicSystem();
    
    // 完美适配 Server.cpp 中的调用方式
    void PostMsgToQue(std::shared_ptr<LogicNode> msg);

    // 统一回调函数签名：直接传递 Session 和 RecvNode 智能指针
    using FunCallBack = std::function<void(std::shared_ptr<Session>, std::shared_ptr<RecvNode>)>;

private:
    LogicSystem();
    void DealMsg();
    void RegisterCallBacks();
    
    // 具体的业务处理函数
    void HelloWordCallBack(std::shared_ptr<Session> session, std::shared_ptr<RecvNode> recv_node);

    std::thread _worker_thread;
    std::queue<std::shared_ptr<LogicNode>> _msg_que;
    std::mutex _mutex;
    std::condition_variable _consume;
    std::atomic<bool> _b_stop; // 使用 atomic 保证多线程可见性
    std::unordered_map<short, FunCallBack> _fun_callbacks;
};
```

#### 2. `LogicSystem.cpp`
```cpp
#include "LogicSystem.h"
#include <nlohmann/json.hpp>
#include <iostream>

using json = nlohmann::json;

LogicSystem::LogicSystem() : _b_stop(false) {
    RegisterCallBacks();
    _worker_thread = std::thread(&LogicSystem::DealMsg, this);
}

LogicSystem::~LogicSystem() {
    _b_stop = true;
    _consume.notify_one(); // 唤醒可能处于等待状态的工作线程
    if (_worker_thread.joinable()) {
        _worker_thread.join();
    }
}

void LogicSystem::RegisterCallBacks() {
    // 使用 lambda 替代 std::bind，更现代、类型推导更安全
    _fun_callbacks[MSG_HELLO_WORD] = [this](std::shared_ptr<Session> session, std::shared_ptr<RecvNode> node) {
        HelloWordCallBack(session, node);
    };
}

void LogicSystem::HelloWordCallBack(std::shared_ptr<Session> session, std::shared_ptr<RecvNode> recv_node) {
    try {
        // 使用 GetString() 获取消息体，避免直接访问 protected 成员 _data
        json root = json::parse(recv_node->GetString());
        
        std::cout << "[Logic] Receive msg id: " << root["id"].get<int>()
                  << ", data: " << root["data"].get<std::string>() << "\n";

        // 构造响应
        root["data"] = "server has received msg, data is " + root["data"].get<std::string>();
        std::string return_str = root.dump();

        // 响应给客户端
        session->Send(std::move(return_str), root["id"].get<short>());
    }
    catch (const json::exception& e) {
        std::cerr << "[Logic] JSON parse error: " << e.what() << "\n";
    }
}

void LogicSystem::PostMsgToQue(std::shared_ptr<LogicNode> msg) {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _msg_que.push(std::move(msg));
    } // 离开作用域自动解锁
    
    // 在锁外唤醒线程，减少锁竞争
    _consume.notify_one(); 
}

void LogicSystem::DealMsg() {
    while (true) {
        std::shared_ptr<LogicNode> msg_node;
        
        {
            // 使用作用域块管理锁的生命周期，彻底避免提前 unlock 导致的崩溃
            std::unique_lock<std::mutex> lock(_mutex);
            _consume.wait(lock, [this] { return !_msg_que.empty() || _b_stop.load(); });

            if (_b_stop && _msg_que.empty()) {
                break; // 收到停止信号且队列已清空，安全退出
            }

            msg_node = std::move(_msg_que.front());
            _msg_que.pop();
        } // 锁在这里自动释放，后续业务逻辑在无锁状态下执行，极大提高并发

        // 执行回调
        auto it = _fun_callbacks.find(msg_node->_recvnode->GetMsgId());
        if (it != _fun_callbacks.end()) {
            it->second(msg_node->_session, msg_node->_recvnode);
        } else {
            std::cerr << "[Logic] Unknown msg id: " << msg_node->_recvnode->GetMsgId() << "\n";
        }
    }
}
```
### 🌟 重构后的优势总结
1. **零拷贝传递**：`LogicNode` 持有 `shared_ptr<RecvNode>`，从网络层到逻辑层，`std::string` 数据没有发生任何多余的深拷贝。
2. **锁粒度最小化**：`DealMsg` 中使用 `{}` 严格控制锁的作用域，业务逻辑（如 JSON 解析和发送）全部在**无锁状态**下执行，吞吐量大幅提升。
3. **消除崩溃隐患**：彻底废弃了危险的 `unique_lk.unlock()`，改用 RAII 机制自动管理锁，代码更健壮。
4. **现代化 C++ 风格**：使用 `std::atomic<bool>` 替代普通 `bool` 保证多线程可见性；使用 Lambda 替代 `std::bind`，类型推导更清晰。
