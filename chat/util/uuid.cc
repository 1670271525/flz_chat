#include "chat/util/uuid.h"
#include <openssl/rand.h>
#include <stdio.h>

namespace chat {
namespace util {

std::string Uuid::V4() {
    unsigned char b[16];
    if(RAND_bytes(b, sizeof(b)) != 1) {
        return "";
    }
    b[6] = (b[6] & 0x0F) | 0x40;
    b[8] = (b[8] & 0x3F) | 0x80;
    char out[37];
    snprintf(out, sizeof(out),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
             b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return std::string(out, 36);
}

}
}
