/////////////////////////////////////////////////////////////////////////////
// Small HTTP transport helpers for the template wizard.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_TEMPLATE_WIZARD_HTTP_H_
#define _IB_TEMPLATE_WIZARD_HTTP_H_

#include <string>

#include "3rdparty/nlohmann/json.hpp"

namespace ibTemplateWizardHttp {

struct Response {
	int         status = 0;
	std::string body;
	std::string error;

	bool Ok() const { return status >= 200 && status < 300 && error.empty(); }
};

std::string NormalizeEndpoint(std::string raw, const std::string& fallback);

Response PostJson(const std::string& endpoint,
                  const std::string& token,
                  const std::string& tenant,
                  const nlohmann::json& body,
                  int timeoutSec);

bool GetBytes(const std::string& url, std::string& body, int timeoutSec = 10);

}  // namespace ibTemplateWizardHttp

#endif // _IB_TEMPLATE_WIZARD_HTTP_H_
