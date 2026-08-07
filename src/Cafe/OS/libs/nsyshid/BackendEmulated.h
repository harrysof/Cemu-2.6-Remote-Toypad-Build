#include "nsyshid.h"
#include "Backend.h"

namespace nsyshid
{
	class DimensionsNetworkListener;
}

namespace nsyshid::backend::emulated
{
	class BackendEmulated : public nsyshid::Backend {
	  public:
		BackendEmulated();
		~BackendEmulated();

		bool IsInitialisedOk() override;

	  protected:
		void AttachVisibleDevices() override;

	  private:
		std::unique_ptr<nsyshid::DimensionsNetworkListener> m_dimensionsNetworkListener;
	};
} // namespace nsyshid::backend::emulated
