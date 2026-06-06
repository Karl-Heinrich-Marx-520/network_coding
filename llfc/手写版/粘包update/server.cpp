#include "Server.h"

//=========================================Server===============================================
Server::Server(boost::asio::io_context& ioc, unsigned short port) : _acceptor(ioc, tcp::endpoint(tcp::v4(), port)), _ioc(ioc) {}

void Server::Start() {
	DoAccept();
}

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
			}
			else if (ec == boost::asio::error::operation_aborted) {
				std::cout << "[Server] Acceptor stopped.\n";
			}
			else {
				std::cerr << "[Server] Accept error" << ec.message() << "\n";
				DoAccept();
			}
		}
	);
}
//===========================================Session================================

Session::Session(tcp::socket socket, std::weak_ptr<Server> server) : _socket(std::move(socket)), _server(server) {
	static thread_local boost::uuids::random_generator gen; //question left 1
	_uuid = boost::uuids::to_string(gen());
	_body_buf.resize(protocol::MAX_BODY_LENGTH);
}

const std::string& Session::GetUuid() const { return _uuid; }

void Session::Start() {
	DoReadHeader();
}
void Session::Send(std::string msg) {
	if (_send_queue.size() >= protocol::MAX_SEND_QUEUE_LENGTH) {
		std::cerr << "[Session " << _uuid << "] Send queue full, dropping connection.\n";
		boost::system::error_code ec;
		Close("Send Queue Full", ec); // 主动断开
		return;
	}
	boost::asio::post(
		_socket.get_executor(),
		[this, self = shared_from_this(), data = std::move(msg)]() mutable {
			if (_is_closed) return; //防御 1：任务入队之前，确认socket打开，丢弃新数据
			uint16_t net_body_len = htons(static_cast<uint16_t>(data.size()));


			std::string packet(protocol::HEADER_SIZE + data.size(), '\0');
			std::memcpy(packet.data(), &net_body_len, protocol::HEADER_SIZE);
			std::memcpy(packet.data() + protocol::HEADER_SIZE, data.data(), data.size());

			bool write_in_progress = _is_writing;
			_send_queue.push_back(std::move(packet));

			if (!write_in_progress) {
				_is_writing = true;
				DoWrite();
			}
		}
	);
}

void Session::DoWrite() {
	//防御 2: 在发起异步写之前，再次确认socket打开的，拒绝发起新I/O 
	if (!_socket.is_open() || _is_closed) {
		_is_writing = false;
		return;
	}
	boost::asio::async_write(
		_socket,
		boost::asio::buffer(_send_queue.front()),
		[this, self = shared_from_this()](boost::system::error_code ec, size_t bytes_transferrd) {
			if (!ec) {
				_send_queue.pop_front();
				if (!_send_queue.empty()) {
					DoWrite();
				}
				else {
					_is_writing = false;
				}
			}
			else {
				Close("Write", ec);
			}
		}
	);
}

void Session::Close(const std::string& reason, boost::system::error_code ec = {}) {
	// 状态检查：如果已经关闭过，直接返回，拦截所有重复回调
	if (_is_closed) return;
	_is_closed = true;

	if (ec) {
		std::cerr << "[Session " << _uuid << "] " << reason << " Error: " << ec.message() << "\n";
	}
	else {
		std::cout << "[Session " << _uuid << "] Closed normally.\n";
	}

	_socket.close(ec);
	if (auto server = _server.lock()) {
		server->RemoveSession(_uuid);
	}
}

void Session::DoReadHeader() {
	boost::asio::async_read(
		_socket,
		boost::asio::buffer(_header_buf),
		[this, self = shared_from_this()](boost::system::error_code ec, size_t) {
			if (!ec) {
				uint16_t net_body_len;
				
				std::memcpy(&net_body_len, _header_buf.data(), protocol::HEADER_SIZE);
				uint16_t body_len = ntohs(net_body_len); //字节序转换

				if (body_len > protocol::MAX_BODY_LENGTH || body_len == 0) {
					std::cerr << "Malicios packet length: " << body_len << "\n";
					Close("Read Error", ec);
					return;
				}
				else {
					DoReadBody(body_len);
				}
			}
			else {
				Close("Read Error", ec);
			}
		}
	);
}

void Session::DoReadBody(uint16_t body_length) {
	boost::asio::async_read(
		_socket,
		boost::asio::buffer(_body_buf, body_length),
		[this, self = shared_from_this(), body_length](boost::system::error_code ec, size_t) {
			if (!ec) {
				std::string_view msg(_body_buf.data(), body_length);
				std::cout << "Receive: " << msg << "\n";

				Send(std::string(msg));
				DoReadHeader();
			}
			else {
				Close("Read Error", ec);
			}
		}
	);
}
