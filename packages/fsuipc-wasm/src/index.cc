#define NODE_ADDON_API_DISABLE_DEPRECATED

#include <napi.h>
#include <uv.h>

#include "FSUIPCWASM.h"

namespace FSUIPCWASM {

Napi::Object InitModule(Napi::Env env, Napi::Object exports) {
  FSUIPCWASM::Init(env, exports);
  InitError(env, exports);
  InitLogLevel(env, exports);
  return exports;
}

NODE_API_MODULE(NODE_GYP_MODULE_NAME, InitModule);

}  // namespace FSUIPCWASM
