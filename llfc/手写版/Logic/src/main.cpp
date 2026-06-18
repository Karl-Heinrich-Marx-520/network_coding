#include <iostream>
#include <boost/asio.hpp>
#include "Server.h"

int main() {
	try {
		boost::asio::io_context io_context;
		auto server = std::make_shared<Server>(io_context, 10086);

		// 定义信号集，监听 Ctrl + C(SIGINT) 和 kill 命令(SIGTERM)
		boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
		signals.async_wait(
			[&io_context, &server](const boost::system::error_code& ec, int signal_number) {
				if (!ec) {
					std::cout << "\n[Main] Shutdown signal recevied. Stopping server...\n";
					server->Stop();
					io_context.stop();
				}
			});

		server->Start();
		io_context.run();
		std::cout << "[main] Server exited cleanly.\n";

	}
	catch (std::exception& e) {
		std::cerr << "Exception: " << e.what() << "\n";
	}
	return 0;
}