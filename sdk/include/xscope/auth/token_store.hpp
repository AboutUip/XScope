#pragma once

#include "xscope/auth/types.hpp"
#include "xscope/utils/json.hpp"

#include <optional>
#include <string>

namespace xscope::auth {

/// Serialize token set (includes access_token — store only in encrypted secrets).
utils::Json token_set_to_json(const TokenSet& token);
TokenSet token_set_from_json(const utils::Json& json);

/// Accept either JSON TokenSet or a raw bearer/PAT string.
TokenSet parse_secret_payload(const std::string& plaintext, const std::string& default_provider);

std::string serialize_token_set(const TokenSet& token);

/// Status view without exposing the access token value.
utils::Json token_status_json(const TokenSet& token, bool connected);

} // namespace xscope::auth
