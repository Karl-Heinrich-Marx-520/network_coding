#include "LogicSystem.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// 假设在 const.h 中定义: enum MSG_IDS { MSG_HELLO_WORD = 1001 }

LogicSystem::LogicSystem() : _b_stop(false) {
    RegisterCallBacks();
    _worker_thread = std::thread(&LogicSystem::DealMsg, this);
}

void LogicSystem::RegisterCallBacks() {
    _fun_callbacks[MSG_HELLO_WORD] = [this](std::shared_ptr<Session> session,
        std::shared_ptr<RecvNode> node) {
            HelloWordCallBack(session, node);
        };
}

void LogicSystem::HelloWordCallBack(std::shared_ptr<Session> session, std::shared_ptr<RecvNode> recv_node) {
    try {
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
    bool need_notify = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        need_notify = _msg_que.empty();
        _msg_que.push(msg);
    }// 离开作用域自动解锁
    // 队列由空变非空时，唤醒工作线程
    if (need_notify) {
        _consume.notify_one();
    }
}

void LogicSystem::DealMsg() {
    while(true){
        std::shared_ptr<LogicNode> msg_node;

        {
            std::unique_lock<std::mutex> lock(_mutex);
            _consume.wait(lock,
                [this]() {return !_msg_que.empty() || _b_stop.load();});
            if (_b_stop && _msg_que.empty()) {
                break;
            }
            msg_node = std::move(_msg_que.front());
            _msg_que.pop();
        }// 锁在这里自动释放

        auto it = _fun_callbacks.find(msg_node->_recvnode->GetMsgId());
        if(it != _fun_callbacks.end()) {
            it->second(msg_node->_session, msg_node->_recvnode);
        }
        else {
            std::cerr << "[Logic] Unknown msg id: " << msg_node->_recvnode->GetMsgId()
                << "\n";
        }
    }
}

LogicSystem::~LogicSystem() {
    _b_stop = true;
    _consume.notify_one(); // 唤醒可能处于等待状态的工作线程
    if (_worker_thread.joinable()) {
        _worker_thread.join();
    }
}