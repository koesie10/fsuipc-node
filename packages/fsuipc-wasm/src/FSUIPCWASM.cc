#include "FSUIPCWASM.h"

#include <uv.h>
#include <windows.h>

#include <iostream>
#include <string>

namespace FSUIPCWASM {

Napi::ObjectReference FSUIPCWASMError;

void DebugLog(const char* logString) {
  std::cout << "FSUIPCWASM: " << logString << std::endl;
}

void FSUIPCWASM::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function ctor =
      DefineClass(env, "FSUIPCWASM",
                  {
                      InstanceMethod<&FSUIPCWASM::Start>("start"),
                      InstanceMethod<&FSUIPCWASM::Close>("close"),

                      InstanceAccessor<&FSUIPCWASM::GetLvarValues, nullptr>("lvarValues"),
                  });

  Napi::FunctionReference* constructor = new Napi::FunctionReference();
  *constructor = Napi::Persistent(ctor);

  env.SetInstanceData<Napi::FunctionReference>(constructor);

  exports.Set("FSUIPCWASM", ctor);
}

FSUIPCWASM::FSUIPCWASM(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<FSUIPCWASM>(info) {
  Napi::Env env = info.Env();

  // throw an error if constructor is called without new keyword
  if (!info.IsConstructCall()) {
    throw Napi::Error::New(env,
                           "FSUIPCWASM.new - called without new keyword");
  }

  bool debug = false;

  if (info.Length() > 0) {
    if (!info[0].IsObject()) {
      throw Napi::TypeError::New(
        env, "FSUIPCWASM.new: expected first argument to be object");
    }

    auto options = info[0].As<Napi::Object>();
    Napi::Value debug_value = options["debug"];
    if (debug_value.ToBoolean().Value()) {
      debug = true;
    }
  }

  if (debug) {
    this->wasmif = WASMIF::GetInstance(&DebugLog);
  } else {
    this->wasmif = WASMIF::GetInstance();
  }
}

Napi::Value FSUIPCWASM::Start(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(env);
  auto worker = new StartAsyncWorker(env, deferred, this);
  worker->Queue();

  return deferred.Promise();
}

Napi::Value FSUIPCWASM::Close(const Napi::CallbackInfo& info) {
  std::lock_guard<std::mutex> guard(this->wasmif_mutex);

  Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(info.Env());

  this->wasmif->end();

  deferred.Resolve(this->Value());

  return deferred.Promise();
}

Napi::Value FSUIPCWASM::GetLvarValues(const Napi::CallbackInfo& info) {
  std::lock_guard<std::mutex> guard(this->wasmif_mutex);

  Napi::Env env = info.Env();

  map<string, double> lvarValues;

  this->wasmif->getLvarValues(lvarValues);

  Napi::Object lvarValuesObject = Napi::Object::New(env);
  for (auto const& [key, val] : lvarValues) {
    lvarValuesObject.Set(key, val);
  }
  return lvarValuesObject;
}

void StartAsyncWorker::Execute() {
  std::lock_guard<std::mutex> guard(this->fsuipcWasm->wasmif_mutex);

  if (!this->fsuipcWasm->wasmif->start()) {
    this->SetError("Failed to start WASMIF");
    return;
  }
}

void StartAsyncWorker::OnOK() {
  Napi::Env env = this->Env();
  Napi::HandleScope scope(env);

  this->deferred.Resolve(this->fsuipcWasm->Value());
}

void StartAsyncWorker::OnError(const Napi::Error& e) {
  Napi::Env env = this->Env();
  Napi::HandleScope scope(env);

  napi_value args[2] = {Napi::String::New(env, e.Message())};
  Napi::Value error = FSUIPCWASMError.Value().As<Napi::Function>().New(1, args);

  this->deferred.Reject(error);
}

void InitError(Napi::Env env, Napi::Object exports) {
  std::string code =
      "class FSUIPCWASMError extends Error {"
      "  constructor (message) {"
      "    super(message);"
      "    this.name = this.constructor.name;"
      "    Error.captureStackTrace(this, this.constructor);"
      "  }"
      "};"
      "FSUIPCWASMError";

  Napi::Object errorFunc = env.RunScript(code).ToObject();
  FSUIPCWASMError = Napi::ObjectReference::New(errorFunc, 1);

  exports.Set("FSUIPCWASMError", errorFunc);
}

}  // namespace FSUIPCWASM
