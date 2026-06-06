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
using boost::asio::ip::tcp;

namespace protocol {
	constexpr size_t HEADER_SIZE = sizeof(uint16_t);
	constexpr uint16_t MAX_BODY_LENGTH = 8 * 1024;
	constexpr size_t MAX_SEND_QUEUE_LENGTH = 1024;
}

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


class Session : public std::enable_shared_from_this<Session> {
public:
	Session(tcp::socket socket, std::weak_ptr<Server> server);
	~Session() = default;

	void Start();
	void Close(const std::string& reason, boost::system::error_code ec = {});
	void Send(std::string msg);
	const std::string& GetUuid() const;

private:
	void DoWrite();
	void DoReadHeader();
	void DoReadBody(uint16_t body_length);

	std::weak_ptr<Server> _server;
	std::string _uuid;
	tcp::socket _socket;
	//read buffer
	std::vector<char> _body_buf;
	std::array<char, protocol::HEADER_SIZE> _header_buf{};

	//Send queue and status
	std::deque<std::string> _send_queue;
	bool _is_writing = false;
	bool _is_closed = false;
};

