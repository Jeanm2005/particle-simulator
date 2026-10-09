#include "ApiSession.hpp"
#include <emscripten/bind.h>
namespace {
class BrowserSession {
public:
    std::string execute(const std::string& command) { return session_.execute(command); }
private:
    qm::ApiSession session_{"", 50000};
};
}
EMSCRIPTEN_BINDINGS(orbital_browser) {
    emscripten::class_<BrowserSession>("BrowserSession")
        .constructor<>()
        .function("execute", &BrowserSession::execute);
}
