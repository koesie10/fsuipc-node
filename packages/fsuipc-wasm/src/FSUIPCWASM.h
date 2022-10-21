#pragma once

// Disable winsock.h
#include <napi.h>
#include <winsock2.h>

#include <iostream>

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "FSUIPC_WAPI/WASMIF.h"

namespace FSUIPCWASM {

void InitError(Napi::Env env, Napi::Object exports);
void InitLogLevel(Napi::Env env, Napi::Object exports);

class FSUIPCWASM : public Napi::ObjectWrap<FSUIPCWASM> {
  friend class StartAsyncWorker;

 public:
  static void Init(Napi::Env env, Napi::Object exports);

  FSUIPCWASM(const Napi::CallbackInfo& info);
  Napi::Value Start(const Napi::CallbackInfo& info);
  Napi::Value Close(const Napi::CallbackInfo& info);

  Napi::Value GetLvarValues(const Napi::CallbackInfo& info);

  static Napi::FunctionReference constructor;

  ~FSUIPCWASM() {
    if (this->wasmif) {
      this->wasmif->end();
    }
  }

 protected:
  std::mutex wasmif_mutex;
  WASMIF* wasmif;

  bool started;
  std::condition_variable start_cv;

  void updateCallback();
};

class StartAsyncWorker : public Napi::AsyncWorker {
 public:
  FSUIPCWASM* fsuipcWasm;

  StartAsyncWorker(Napi::Env& env,
                     Napi::Promise::Deferred deferred,
                     FSUIPCWASM* fsuipcWasm)
      : Napi::AsyncWorker(env), deferred(deferred), fsuipcWasm(fsuipcWasm) {}

  void Execute() override;

  void OnOK() override;
  void OnError(const Napi::Error& e) override;

 private:
  Napi::Promise::Deferred deferred;
};

}  // namespace FSUIPCWASM
