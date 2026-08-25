#include "DimensionsNetworkListener.h"

#include "Dimensions.h"

#include <chrono>
#include <thread>

namespace nsyshid
{
	namespace
	{
		constexpr uint8 kLoadCommand = 0x01;
		constexpr uint8 kRemoveCommand = 0x02;
		constexpr uint8 kMoveCommand = 0x03;
		constexpr uint8 kGetLedCommand = 0x04;
		constexpr size_t kHeaderSize = 5;
		constexpr size_t kFigureDataSize = 0x2D * 0x04;
		constexpr auto kMovePickupDelay = std::chrono::milliseconds(500);
		// The GET_LED response is a fixed-length snapshot: { 'L', serial, 3,
		// then 3 regions x 9 bytes (pad, mode, r, g, b, onMs, offMs, count,
		// speedMs) } = 30 bytes.
		constexpr size_t kLedResponseSize = 3 + 3 * 9;
	}

	DimensionsNetworkListener::~DimensionsNetworkListener()
	{
		Stop();
	}

	bool DimensionsNetworkListener::Start(uint16 port)
	{
		bool expected = false;
		if (!m_running.compare_exchange_strong(expected, true))
			return true;

		boost::system::error_code error;
		m_acceptor = std::make_unique<Tcp::acceptor>(m_ioContext);
		m_acceptor->open(Tcp::v4(), error);
		if (!error)
			m_acceptor->set_option(Tcp::acceptor::reuse_address(true), error);
		if (!error)
			m_acceptor->bind(Tcp::endpoint(boost::asio::ip::address_v4::loopback(), port), error);
		if (!error)
			m_acceptor->listen(boost::asio::socket_base::max_listen_connections, error);

		if (error)
		{
			cemuLog_log(LogType::Force, "Dimensions network listener could not listen on 127.0.0.1:{}: {}", port, error.message());
			m_acceptor.reset();
			m_running = false;
			return false;
		}

		m_thread = std::thread(&DimensionsNetworkListener::Run, this);
		cemuLog_logDebug(LogType::Force, "Dimensions network listener started on 127.0.0.1:{}", port);
		return true;
	}

	void DimensionsNetworkListener::Stop()
	{
		if (!m_running.exchange(false))
			return;

		boost::system::error_code error;
		if (m_acceptor)
			m_acceptor->close(error);

		{
			std::lock_guard lock(m_socketMutex);
			if (m_client)
				m_client->close(error);
		}

		if (m_thread.joinable())
			m_thread.join();

		m_acceptor.reset();
	}

	void DimensionsNetworkListener::Run()
	{
		while (m_running)
		{
			auto client = std::make_shared<Tcp::socket>(m_ioContext);
			boost::system::error_code error;
			m_acceptor->accept(*client, error);
			if (error)
			{
				if (m_running)
					cemuLog_log(LogType::Force, "Dimensions network listener accept failed: {}", error.message());
				break;
			}

			{
				std::lock_guard lock(m_socketMutex);
				m_client = client;
			}

			if (m_running)
				HandleClient(client);

			client->close(error);
			{
				std::lock_guard lock(m_socketMutex);
				if (m_client == client)
					m_client.reset();
			}
		}
	}

	void DimensionsNetworkListener::HandleClient(const std::shared_ptr<Tcp::socket>& client)
	{
		std::array<uint8, kHeaderSize> header{};
		while (m_running && ReceiveExact(*client, header))
		{
			const uint8 command = header[0];
			const uint8 pad = header[1];
			const uint8 index = header[2];

			if (command == kLoadCommand)
			{
				std::array<uint8, kFigureDataSize> figureData{};
				if (!ReceiveExact(*client, figureData))
				{
					cemuLog_log(LogType::Force, "Dimensions network listener received a truncated LOAD message");
					return;
				}

				if (!IsValidSlot(pad, index) || header[3] != 0 || header[4] != 0)
				{
					cemuLog_log(LogType::Force, "Dimensions network listener ignored invalid LOAD message");
					continue;
				}

				// A LOAD message may carry a UTF-8 source file path (2-byte little-endian
				// length, then that many bytes) for the .bin the figure data came from.
				// Attaching a real FileStream makes the game's writes persist to that
				// file, exactly like the built-in toypad window. An empty path keeps the
				// old in-memory-only behavior.
				std::string pathUtf8;
				std::array<uint8, 2> pathLengthBytes{};
				if (!ReceiveExact(*client, pathLengthBytes))
				{
					cemuLog_log(LogType::Force, "Dimensions network listener received a truncated LOAD message");
					return;
				}
				const uint16 pathLength = static_cast<uint16>(pathLengthBytes[0]) | (static_cast<uint16>(pathLengthBytes[1]) << 8);
				if (pathLength != 0)
				{
					pathUtf8.resize(pathLength);
					if (!ReceiveExact(*client, std::span<uint8>(reinterpret_cast<uint8*>(pathUtf8.data()), pathUtf8.size())))
					{
						cemuLog_log(LogType::Force, "Dimensions network listener received a truncated LOAD message");
						return;
					}
				}

				std::unique_ptr<FileStream> dimFile;
				if (!pathUtf8.empty())
				{
					dimFile.reset(FileStream::openFile2(_utf8ToPath(pathUtf8), true));
					if (!dimFile)
						cemuLog_log(LogType::Force, "Dimensions network listener failed to open figure file: {}", pathUtf8);
				}

				g_dimensionstoypad.RemoveFigure(pad, index, true);
				g_dimensionstoypad.LoadFigure(figureData, std::move(dimFile), pad, index);
				continue;
			}

			if (command == kRemoveCommand)
			{
				if (!IsValidSlot(pad, index) || header[3] != 0 || header[4] != 0)
				{
					cemuLog_log(LogType::Force, "Dimensions network listener ignored invalid REMOVE message");
					continue;
				}

				g_dimensionstoypad.RemoveFigure(pad, index, true);
				continue;
			}

			if (command == kMoveCommand)
			{
				const uint8 oldPad = header[3];
				const uint8 oldIndex = header[4];
				if (!IsValidSlot(pad, index) || !IsValidSlot(oldPad, oldIndex))
				{
					cemuLog_log(LogType::Force, "Dimensions network listener ignored invalid MOVE message");
					continue;
				}

				if (!g_dimensionstoypad.TempRemove(oldIndex))
				{
					cemuLog_log(LogType::Force, "Dimensions network listener ignored MOVE from empty source slot {}", oldIndex);
					continue;
				}

				std::this_thread::sleep_for(kMovePickupDelay);
				g_dimensionstoypad.MoveFigure(pad, index, oldPad, oldIndex);
				continue;
			}

			if (command == kGetLedCommand)
			{
				// Provide the current LED snapshot so a polling client (e.g. the
				// LegoToypad controller app) can render the pads glowing without
				// any push. The serial tells the client when a command changed.
				const auto states = g_dimensionstoypad.GetLedStates();
				const uint8 serial = g_dimensionstoypad.GetLedSerial();

				std::array<uint8, kLedResponseSize> response{};
				response[0] = 0x4C; // 'L' magic
				response[1] = serial;
				response[2] = 0x03; // region count
				for (size_t i = 0; i < 3; ++i)
				{
					const size_t off = 3 + i * 9;
					response[off + 0] = states[i].pad;
					response[off + 1] = states[i].mode;
					response[off + 2] = states[i].r;
					response[off + 3] = states[i].g;
					response[off + 4] = states[i].b;
					response[off + 5] = states[i].onMs;
					response[off + 6] = states[i].offMs;
					response[off + 7] = states[i].count;
					response[off + 8] = states[i].speedMs;
				}

				boost::system::error_code writeError;
				boost::asio::write(*client, boost::asio::buffer(response.data(), response.size()), writeError);
				if (writeError && m_running)
					cemuLog_log(LogType::Force, "Dimensions network listener failed to send LED state: {}", writeError.message());
				continue;
			}

			cemuLog_log(LogType::Force, "Dimensions network listener received unknown command {:02x}", command);
			return;
		}
	}

	bool DimensionsNetworkListener::ReceiveExact(Tcp::socket& client, std::span<uint8> buffer)
	{
		boost::system::error_code error;
		boost::asio::read(client, boost::asio::buffer(buffer.data(), buffer.size()), error);
		if (!error)
			return true;

		if (m_running && error != boost::asio::error::eof && error != boost::asio::error::operation_aborted)
			cemuLog_log(LogType::Force, "Dimensions network listener receive failed: {}", error.message());
		return false;
	}

	bool DimensionsNetworkListener::IsValidSlot(uint8 pad, uint8 index) const
	{
		return pad >= 1 && pad <= 3 && index < 7;
	}
} // namespace nsyshid
