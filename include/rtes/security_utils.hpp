#pragma once

#include <string>
#include <cstdint>

namespace rtes {

// Authentication context — simplified for demo (no real crypto)
struct AuthContext {
    uint64_t    client_id{0};
    std::string user_id;
    bool        is_authenticated{true};
};

} // namespace rtes
