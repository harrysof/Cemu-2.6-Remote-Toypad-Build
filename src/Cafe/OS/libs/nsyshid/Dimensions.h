#include <mutex>

#include "nsyshid.h"
#include "Backend.h"

#include "Common/FileStream.h"

namespace nsyshid
{
	class DimensionsToypadDevice final : public Device
	{
	  public:
		DimensionsToypadDevice();
		~DimensionsToypadDevice() = default;

		bool Open() override;

		void Close() override;

		bool IsOpened() override;

		ReadResult Read(ReadMessage* message) override;

		WriteResult Write(WriteMessage* message) override;

		bool GetDescriptor(uint8 descType,
						   uint8 descIndex,
						   uint16 lang,
						   uint8* output,
						   uint32 outputMaxLength) override;

		bool SetIdle(uint8 ifIndex,
					 uint8 reportId,
					 uint8 duration) override;

		bool SetProtocol(uint8 ifIndex, uint8 protocol) override;

		bool SetReport(ReportMessage* message) override;

	  private:
		bool m_IsOpened;
	};

	class DimensionsUSB
	{
	  public:
		struct DimensionsMini final
		{
			std::unique_ptr<FileStream> dimFile;
			std::array<uint8, 0x2D * 0x04> data{};
			uint8 index = 255;
			uint8 pad = 255;
			uint32 id = 0;
			void Save();
		};

		void SendCommand(std::span<const uint8, 32> buf);
		std::array<uint8, 32> GetStatus();

		void GenerateRandomNumber(std::span<const uint8, 8> buf, uint8 sequence,
								  std::array<uint8, 32>& replyBuf);
		void InitializeRNG(uint32 seed);
		void GetChallengeResponse(std::span<const uint8, 8> buf, uint8 sequence,
								  std::array<uint8, 32>& replyBuf);
		void QueryBlock(uint8 index, uint8 page, std::array<uint8, 32>& replyBuf,
						uint8 sequence);
		void WriteBlock(uint8 index, uint8 page, std::span<const uint8, 4> toWriteBuf, std::array<uint8, 32>& replyBuf,
						uint8 sequence);
		void GetModel(std::span<const uint8, 8> buf, uint8 sequence,
					  std::array<uint8, 32>& replyBuf);

		bool RemoveFigure(uint8 pad, uint8 index, bool fullRemove);
		bool TempRemove(uint8 index);
		bool CancelRemove(uint8 index);
		uint32 LoadFigure(const std::array<uint8, 0x2D * 0x04>& buf, std::unique_ptr<FileStream> file, uint8 pad, uint8 index);
		bool CreateFigure(fs::path pathName, uint32 id);
		bool MoveFigure(uint8 pad, uint8 index, uint8 oldPad, uint8 oldIndex);

		// Per-pad-region LED state, mirrored out via the network listener so an
		// external controller app can render the pads glowing like the real
		// toypad during keystone puzzles. `mode` matches the app's encoding:
		// 0 = off, 1 = solid, 2 = flash, 3 = fade. `pad` is the wire pad value
		// (1 = center, 2 = left, 3 = right).
		struct LedPadState
		{
			uint8 pad = 0;
			uint8 mode = 0;
			uint8 r = 0, g = 0, b = 0;
			uint8 onMs = 0, offMs = 0, count = 0, speedMs = 0;
		};

		// The current LED snapshot (one entry per region, ordered center, left,
		// right) and the serial that increments whenever any region changes, so
		// a polling client only re-applies a command once.
		std::array<LedPadState, 3> GetLedStates();
		uint8 GetLedSerial();
		LedPadState GetLedState(uint8 pad);

		static std::map<const uint32, const char*> GetListMinifigs();
		static std::map<const uint32, const char*> GetListTokens();
		std::string FindFigure(uint32 figNum);

	  protected:
		std::mutex m_dimensionsMutex;
		std::array<DimensionsMini, 7> m_figures{};

	  private:
		void HandleLedCommand(std::span<const uint8, 32> buf);
		void SetLedState(uint8 pad, uint8 mode, uint8 r, uint8 g, uint8 b,
						 uint8 onMs, uint8 offMs, uint8 count, uint8 speedMs);

		void RandomUID(std::array<uint8, 0x2D * 0x04>& uidBuffer);
		uint8 GenerateChecksum(const std::array<uint8, 32>& data,
							   int numOfBytes) const;
		std::array<uint8, 8> Decrypt(std::span<const uint8, 8> buf, std::optional<std::array<uint8, 16>> key);
		std::array<uint8, 8> Encrypt(std::span<const uint8, 8> buf, std::optional<std::array<uint8, 16>> key);
		std::array<uint8, 16> GenerateFigureKey(const std::array<uint8, 0x2D * 0x04>& uid);
		std::array<uint8, 4> PWDGenerate(const std::array<uint8, 0x2D * 0x04>& uid);
		std::array<uint8, 4> DimensionsRandomize(const std::vector<uint8> key, uint8 count);
		uint32 GetFigureId(const std::array<uint8, 0x2D * 0x04>& buf);
		uint32 Scramble(const std::array<uint8, 7>& uid, uint8 count);
		uint32 GetNext();
		DimensionsMini& GetFigureByIndex(uint8 index);

		uint32 m_randomA;
		uint32 m_randomB;
		uint32 m_randomC;
		uint32 m_randomD;

		bool m_isAwake = false;

		std::queue<std::array<uint8, 32>> m_figureAddedRemovedResponses;
		std::queue<std::array<uint8, 32>> m_queries;

		// LED mirror state, guarded by m_ledMutex (updated on the HID I/O
		// thread by SendCommand, read by the network listener's GET_LED handler).
		std::mutex m_ledMutex;
		std::array<LedPadState, 3> m_ledState{};
		uint8 m_ledSerial = 0;
	};
	extern DimensionsUSB g_dimensionstoypad;

} // namespace nsyshid