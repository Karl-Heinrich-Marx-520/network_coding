#pragma once
#include <thread>
#include <queue>
#include <condition_variable>
#include <unordered_map>
#include <functional>
#include "Singleton.h"
#include "Server.h" 

struct LogicNode {
    LogicNode(std::shared_ptr<Session> session, std::shared_ptr<RecvNode> recvnode)
        : _session(std::move(session)), _recvnode(std::move(recvnode)) {}

    std::shared_ptr<Session> _session;   // 持有 Session 防止其被析构
    std::shared_ptr<RecvNode> _recvnode; // 接收到的消息节点
};


class LogicSystem : public Singleton<LogicSystem> {
    friend class Singleton<LogicSystem>;
public:
    ~LogicSystem();
    void PostMsgToQue(std::shared_ptr<LogicNode> msg);

    using FunCallBack = std::function<void(std::shared_ptr<Session> session, 
        std::shared_ptr<RecvNode> node)>;

private:
    LogicSystem();
    void DealMsg();
    void RegisterCallBacks(); //注册路由表
    // 业务处理函数
    void HelloWordCallBack(std::shared_ptr<Session> session, std::shared_ptr<RecvNode> recv_node);

    std::thread _worker_thread;
    std::queue<std::shared_ptr<LogicNode>> _msg_que;
    std::mutex _mutex;
    std::condition_variable _consume;
    std::atomic<bool> _b_stop;
    std::unordered_map<short, FunCallBack> _fun_callbacks;
};