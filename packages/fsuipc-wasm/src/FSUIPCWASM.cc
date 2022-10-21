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
    : Napi::ObjectWrap<FSUIPCWASM>(info), started(false) {
  Napi::Env env = info.Env();

  // throw an error if constructor is called without new keyword
  if (!info.IsConstructCall()) {
    throw Napi::Error::New(env,
                           "FSUIPCWASM.new - called without new keyword");
  }

  LOGLEVEL log_level = DISABLE_LOG;

  if (info.Length() > 0) {
    if (!info[0].IsObject()) {
      throw Napi::TypeError::New(
        env, "FSUIPCWASM.new: expected first argument to be object");
    }

    auto options = info[0].As<Napi::Object>();
    Napi::Value log_level_value = options["logLevel"];
    log_level = (LOGLEVEL)log_level_value.ToNumber().Int32Value();
  }

  this->wasmif = WASMIF::GetInstance(&DebugLog);

  // this->wasmif->setLogLevel(log_level);
  this->wasmif->registerUpdateCallback(std::bind(&FSUIPCWASM::updateCallback, this));
}

void FSUIPCWASM::updateCallback() {
  {
    std::lock_guard<std::mutex> guard(this->wasmif_mutex);
    this->started = true;
  }

  this->start_cv.notify_all();
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
  {
    std::lock_guard<std::mutex> guard(this->fsuipcWasm->wasmif_mutex);

    if (!this->fsuipcWasm->wasmif->start()) {
      this->SetError("Failed to start WASMIF");
      return;
    }
  }

  std::unique_lock lock(this->fsuipcWasm->wasmif_mutex);
  this->fsuipcWasm->start_cv.wait(lock, [this]{return this->fsuipcWasm->started;});

  lock.unlock();
}

void StartAsyncWorker::OnOK() {
  Napi::Env env = Env();
  Napi::HandleScope scope(env);

  deferred.Resolve(this->fsuipcWasm->Value());
}

void StartAsyncWorker::OnError(const Napi::Error& e) {
  Napi::Env env = this->Env();
  Napi::HandleScope scope(env);

  napi_value args[1] = {Napi::String::New(env, e.Message())};
  Napi::Value error = FSUIPCWASMError.Value().As<Napi::Function>().New(1, args);

  this->deferred.Reject(error);
}

void InitLogLevel(Napi::Env env, Napi::Object exports) {
  Napi::Object obj = Napi::Object::New(env);
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "Disable", Napi::Number::New(env, (int)DISABLE_LOG)));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "Info", Napi::Number::New(env, (int)LOG_LEVEL_INFO)));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "Buffer", Napi::Number::New(env, (int)LOG_LEVEL_BUFFER)));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "Debug", Napi::Number::New(env, (int)LOG_LEVEL_DEBUG)));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "Trace", Napi::Number::New(env, (int)LOG_LEVEL_TRACE)));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "Enable", Napi::Number::New(env, (int)ENABLE_LOG)));

  exports.Set(Napi::String::New(env, "LogLevel"), obj);
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
