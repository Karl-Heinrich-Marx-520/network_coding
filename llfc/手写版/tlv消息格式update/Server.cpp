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

Session::Session(tcp::socket socket, std::weak_ptr<Server> server) :
	_socket(std::move(socket)), _server(server) {
	static thread_local boost::uuids::random_generator gen;
	_uuid = boost::uuids::to_string(gen());
	_recv_head_node = std::make_shared<MsgNode>(HEAD_TOTAL_LEN);
	_recv_msg_node = std::make_shared<RecvNode>(MAX_LENGTH, 0); 
}

const std::string& Session::GetUuid() const { return _uuid; }

void Session::Start() {
	DoReadHeader();
}

void Session::Send(std::string msg, short msg_id) {
	if (_send_queue.size() >= MAX_SNEDQUE) {
		std::cerr << "[Session " << _uuid << "] Send queue full, dropping connection.\n";
		boost::system::error_code ec;
		Close("Send Queue Full", ec);
		return;
	}
	boost::asio::post(
		_socket.get_executor(),
		[this, self = shared_from_this(), msg = std::move(msg), msg_id](){
			if (_is_closed) return; //防御 1：任务入队之前，确认socket打开，丢弃新数据
			auto send_node = std::make_shared<SendNode>(std::move(msg), msg_id);

			bool write_in_progress = _is_writing;
			_send_queue.push_back(send_node);

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

	auto& front_node = _send_queue.front();

	boost::asio::async_write(
		_socket,
		boost::asio::buffer(front_node->data(), front_node->size()),
		[this, self = shared_from_this()](boost::system::error_code ec, size_t) {
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
	_recv_head_node->Clear();
	boost::asio::async_read(
		_socket,
		boost::asio::buffer(_recv_head_node->data(), HEAD_TOTAL_LEN),
		[this, self = shared_from_this()](boost::system::error_code ec, size_t) {
			if (!ec) {
				short net_msg_id, net_msg_len;
				std::memcpy(&net_msg_id, _recv_head_node->data(), HEAD_ID_LEN);
				std::memcpy(&net_msg_len, _recv_head_node->data() + HEAD_ID_LEN, HEAD_DATA_LEN);

				short msg_id = 
					boost::asio::detail::socket_ops::network_to_host_short(net_msg_id);
				short body_len = 
					boost::asio::detail::socket_ops::network_to_host_short(net_msg_len);
				//恶意包防御
				if(body_len <= 0 || body_len > MAX_LENGTH){
					std::cerr << "[Session " << _uuid << "] Malicious packet length: " 
						<< body_len << "\n";
					Close("Read Error", ec);
					return;
				}
				_recv_msg_node = std::make_shared<RecvNode>(body_len, msg_id);
				DoReadBody(body_len);
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
		boost::asio::buffer(_recv_msg_node->data(), body_length),
		[this, self = shared_from_this(), body_length](boost::system::error_code ec, size_t) {
			if (!ec) {
				std::string_view msg_body(_recv_msg_node->data(), _recv_msg_node->size());
				short msg_id = _recv_msg_node->GetMsgId();
				std::cout << "[Session " << _uuid << "] Receive MsgID: " << msg_id
					<< ", Body: " << msg_body << "\n";

				Send(std::string(msg_body), msg_id);
				DoReadHeader();
			}
			else {
				Close("Read Error", ec);
			}
		}
	);
}
