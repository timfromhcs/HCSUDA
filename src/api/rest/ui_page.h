#pragma once

#include <string>

namespace hcs::rest {

/* Returns a self-contained HTML page. The bearer token is injected for same-origin use only;
 * the page is served exclusively from 127.0.0.1. No remote resources are referenced. */
std::string uiPage(const std::string& token);

} // namespace hcs::rest
