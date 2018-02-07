#include <FSUIPC.h>
#include <nan.h>

namespace FSUIPC {

NAN_MODULE_INIT(InitModule) {
  FSUIPC::Init(target);
  InitType(target);
  InitError(target);
  InitSimulator(target);
}

NODE_MODULE(NODE_GYP_MODULE_NAME, InitModule);

}  // namespace FSUIPC
