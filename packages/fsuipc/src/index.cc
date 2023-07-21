#include <FSUIPC.h>
#include <napi.h>

namespace FSUIPC {

Napi::Object InitModule(Napi::Env env, Napi::Object exports) {
  FSUIPC::Init(env, exports);
  InitType(env, exports);
  InitError(env, exports);
  InitSimulator(env, exports);

  return exports;
}

NODE_API_MODULE(NODE_GYP_MODULE_NAME, InitModule);

}  // namespace FSUIPC
