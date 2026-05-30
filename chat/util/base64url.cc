#include "chat/util/base64url.h"
#include "include/hash_util.h"

namespace chat {
namespace util {

bool Base64Url::Encode(const std::string& in, std::string& out) {
    return Encode(in.data(), in.size(), out);
}

bool Base64Url::Encode(const char* data, size_t len, std::string& out) {
    out = flz::base64encode(data, len);
    for(size_t i = 0; i < out.size(); ++i) {
        if(out[i] == '+') {
            out[i] = '-';
        } else if(out[i] == '/') {
            out[i] = '_';
        }
    }
    while(!out.empty() && out[out.size() - 1] == '=') {
        out.resize(out.size() - 1);
    }
    return true;
}

bool Base64Url::Decode(const std::string& in, std::string& out) {
    std::string base = in;
    for(size_t i = 0; i < base.size(); ++i) {
        if(base[i] == '-') {
            base[i] = '+';
        } else if(base[i] == '_') {
            base[i] = '/';
        }
    }
    while(base.size() % 4 != 0) {
        base.push_back('=');
    }
    out = flz::base64decode(base);
    return true;
}

}
}
