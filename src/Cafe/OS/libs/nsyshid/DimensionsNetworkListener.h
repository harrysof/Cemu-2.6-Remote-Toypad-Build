#pragma once

#include <boost/asio.hpp>

namespace nsyshid
{
	class DimensionsNetworkListener final
	{
	public:
		DimensionsNetworkListener() = default;
		~DimensionsNetworkListener();

		DimensionsNetworkListener(const DimensionsNetworkListener&) = delete;
		DimensionsNetworkListener& operator=(const DimensionsNetworkListener&) = delete;

		bool Start(uint16 port);
		void Stop();

	private:
		using Tcp = boost::asio::ip::tcp;

		void Run();
		void HandleClient(const std::shared_ptr<Tcp::socket>& client);
		bool ReceiveExact(Tcp::socket& client, std::span<uint8> buffer);
		bool IsValidSlot(uint8 pad, uint8 index) const;

		std::atomic<bool> m_running{false};
		boost::asio::io_context m_ioContext;
		std::unique_ptr<Tcp::acceptor> m_acceptor;
		std::shared_ptr<Tcp::socket> m_client;
		std::mutex m_socketMutex;
		std::thread m_thread;
	};
} // namespace nsyshid
