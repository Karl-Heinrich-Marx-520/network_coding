#pragma once
#include <iostream>
#include <string>
#include <algorithm>
#include <boost\asio.hpp>

using boost::asio::ip::tcp;

// 协议常量规定
constexpr short HEAD_ID_LEN = 2;
constexpr short HEAD_DATA_LEN = 2;
constexpr short HEAD_TOTAL_LEN = 4;
constexpr short MAX_LENGTH = 8192;
constexpr int MAX_SNEDQUE = 1024; //最大队列长度
constexpr int MAX_RECVQUE = 1024;
constexpr int MSG_HELLO_WORD = 1001;

class MsgNode{
public:
	MsgNode(short max_len) : _total_len(max_len), _cur_len(0) {
		_data.resize(_total_len, '\0');
	}
	~MsgNode() = default;

	void Clear() {
		std::fill(_data.begin(), _data.end(), '\0');
		_cur_len = 0;
	}

	char* data() noexcept { return _data.data(); }
	const char* data() const noexcept { return _data.data(); }
	size_t size() const noexcept { return _data.size(); }
	// 获取底层 string 引用
	std::string& GetString() noexcept { return _data; }
	const std::string& GetString() const noexcept { return _data; }

protected:
	std::string _data;

private:
	short _cur_len;
	short _total_len;
};
//==============================多态====================================
class RecvNode: public MsgNode {
public:
	RecvNode(short max_len, short msg_id) : MsgNode(max_len), _msg_id(msg_id) {}
	~RecvNode() = default;

	short GetMsgId() const { return _msg_id; }
private:
	short _msg_id;
};


class SendNode: public MsgNode {
public:
	SendNode(const std::string& msg, short msg_id) :
		MsgNode(static_cast<short>(msg.length()) + HEAD_TOTAL_LEN),
		_msg_id(msg_id) {
		short msg_len = static_cast<short>(msg.length());

		// 转换为网络字节序
		short msg_id_host =
			boost::asio::detail::socket_ops::host_to_network_short(msg_id);
		short msg_len_host =
			boost::asio::detail::socket_ops::host_to_network_short(msg_len);

		// 写入包头
		std::memcpy(_data.data(), &msg_id_host, HEAD_ID_LEN);
		std::memcpy(_data.data() + HEAD_ID_LEN, &msg_len_host, HEAD_DATA_LEN);
		// 写入包体
		std::memcpy(_data.data() + HEAD_TOTAL_LEN, msg.data(), msg_len);
	}
	~SendNode() = default;

	short GetMsgId() const { return _msg_id; }
private:
	short _msg_id;
};
