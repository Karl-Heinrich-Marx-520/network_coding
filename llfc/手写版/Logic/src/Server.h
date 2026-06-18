#pragma once
#include <iostream>
#include <string>
#include <unordered_map>
#include <boost/asio.hpp>
#include <deque>
#include <string_view>
// uuid algorithm
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
//json
#include <nlohmann/json.hpp> 

#include "MsgNode.h"

using json = nlohmann::json;
using boost::asio::ip::tcp;

class Server: public std::enable_shared_from_this<Server>
{
public:
	Server(boost::asio::io_context& ioc, unsigned short port);
	~Server() = default;

	void Start();
	void Stop();
	void RemoveSession(const std::string& uuid);

private:
	void DoAccept();
	
	std::unordered_map<std::string, std::shared_ptr<Session>> _sessions;
	boost::asio::ip::tcp::acceptor _acceptor;
	boost::asio::io_context& _ioc;
};
//-------------------------------------------Session------------------------------------------------

class Session : public std::enable_shared_from_this<Session> {
public:
	Session(tcp::socket socket, std::weak_ptr<Server> server);
	~Session() = default;

	void Start();
	void Close(const std::string& reason, boost::system::error_code ec = {});
	void Send(std::string msg, short msg_id);
	const std::string& GetUuid() const;
	void Send(const json& j_msg);

private:
	void DoWrite();
	void DoReadHeader();
	void DoReadBody(uint16_t body_length);

	std::weak_ptr<Server> _server;
	std::string _uuid;
	tcp::socket _socket;
	//read buffer
	std::shared_ptr<RecvNode> _recv_msg_node; //消息结构
	std::shared_ptr<MsgNode> _recv_head_node; //头部结构

	//Send queue and status
	std::deque<std::shared_ptr<SendNode>> _send_queue;
	bool _is_writing = false;
	bool _is_closed = false;
};

