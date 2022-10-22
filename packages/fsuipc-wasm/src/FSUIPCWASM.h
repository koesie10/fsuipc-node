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

void ConvertLVarUpdateCallbackData(Napi::Env env,
                                   Napi::Function callback,
                                   nullptr_t* context,
                                   std::map<string, double>* data);

class FSUIPCWASM : public Napi::ObjectWrap<FSUIPCWASM> {
  friend class StartAsyncWorker;

 public:
  static void Init(Napi::Env env, Napi::Object exports);

  FSUIPCWASM(const Napi::CallbackInfo& info);
  Napi::Value Start(const Napi::CallbackInfo& info);
  Napi::Value Close(const Napi::CallbackInfo& info);

  Napi::Value GetLvarValues(const Napi::CallbackInfo& info);
  Napi::Value SetLvarUpdateCallback(const Napi::CallbackInfo& info);
  Napi::Value FlagLvarForUpdate(const Napi::CallbackInfo& info);

  Napi::Value SetLvar(const Napi::CallbackInfo& info);

  static Napi::FunctionReference constructor;

  ~FSUIPCWASM() {
    if (this->wasmif) {
      this->wasmif->end();
    }

    if (this->lvar_update_callback) {
      this->lvar_update_callback.Release();
    }
  }

 protected:
  std::mutex wasmif_mutex;
  WASMIF* wasmif;

  bool started;
  std::condition_variable start_cv;

  void updateCallback();
  void lvarUpdateCallback(const char* lvarName[], double newValue[]);

  Napi::TypedThreadSafeFunction<nullptr_t,
                                std::map<string, double>,
                                ConvertLVarUpdateCallbackData>
      lvar_update_callback;
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
