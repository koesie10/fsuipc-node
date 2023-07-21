// fsuipc.cc
#include "FSUIPC.h"

#include <windows.h>

#include <string>

#include "IPCUser.h"

namespace FSUIPC {

Napi::ObjectReference FSUIPCError;

void FSUIPC::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function ctor =
      DefineClass(env, "FSUIPC",
                  {
                      InstanceMethod<&FSUIPC::Open>("open"),
                      InstanceMethod<&FSUIPC::Close>("close"),

                      InstanceMethod<&FSUIPC::Process>("process"),

                      InstanceMethod<&FSUIPC::Add>("add"),
                      InstanceMethod<&FSUIPC::Remove>("remove"),

                      InstanceMethod<&FSUIPC::Write>("write"),
                  });

  Napi::FunctionReference* constructor = new Napi::FunctionReference();
  *constructor = Napi::Persistent(ctor);

  env.SetInstanceData<Napi::FunctionReference>(constructor);

  exports.Set("FSUIPC", ctor);
}

FSUIPC::FSUIPC(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<FSUIPC>(info) {
  // throw an error if constructor is called without new keyword
  if (!info.IsConstructCall()) {
    throw Napi::Error::New(info.Env(),
                           "FSUIPC.new - called without new keyword");
  }

  if (info.Length() != 0) {
    throw Napi::Error::New(info.Env(), "FSUIPC.new - expected no arguments");
  }

  this->ipc = new IPCUser();
}

Napi::Value FSUIPC::Open(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  Simulator requestedSim = Simulator::ANY;

  if (info.Length() > 0) {
    requestedSim = static_cast<Simulator>(info[0].ToNumber().Uint32Value());
  }

  Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(env);
  auto worker = new OpenAsyncWorker(env, deferred, this, requestedSim);
  worker->Queue();

  return deferred.Promise();
}

Napi::Value FSUIPC::Close(const Napi::CallbackInfo& info) {
  Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(info.Env());

  auto worker = new CloseAsyncWorker(info.Env(), deferred, this);
  worker->Queue();

  return deferred.Promise();
}

Napi::Value FSUIPC::Process(const Napi::CallbackInfo& info) {
  Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(info.Env());

  auto worker = new ProcessAsyncWorker(info.Env(), deferred, this);
  worker->Queue();

  return deferred.Promise();
}

Napi::Value FSUIPC::Add(const Napi::CallbackInfo& info) {
  FSUIPC* self = this;
  Napi::Env env = info.Env();

  if (info.Length() < 3) {
    throw Napi::Error::New(env, "FSUIPC.Add: requires at least 3 arguments");
  }

  if (!info[0].IsString()) {
    throw Napi::Error::New(env,
                           "FSUIPC.Add: expected first argument to be string");
  }

  if (!info[1].IsNumber()) {
    throw Napi::Error::New(env,
                           "FSUIPC.Add: expected second argument to be int");
  }

  if (!info[2].IsNumber()) {
    throw Napi::Error::New(env,
                           "FSUIPC.Add: expected third argument to be int");
  }

  std::string name =
      std::string(info[0].As<Napi::String>().Utf8Value().c_str());
  DWORD offset = info[1].ToNumber().Uint32Value();
  Type type = (Type)info[2].ToNumber().Int32Value();

  DWORD size;

  if (type == Type::ByteArray || type == Type::BitArray ||
      type == Type::String) {
    if (info.Length() < 4) {
      throw Napi::TypeError::New(
          env,
          "FSUIPC.Add: requires at least 4 arguments if type is "
          "byteArray, bitArray or string");
    }

    if (!info[3].IsNumber()) {
      throw Napi::TypeError::New(
          env, "FSUIPC.Add: expected fourth argument to be uint");
    }

    size = (int)info[3].ToNumber().Uint32Value();
  } else {
    size = get_size_of_type(type);
  }

  if (size == 0) {
    throw Napi::TypeError::New(
        env, "FSUIPC.Add: expected fourth argument to be a size > 0");
  }

  self->offsets[name] = Offset{name, type, offset, size, malloc(size)};

  Napi::Object obj = Napi::Object::New(env);

  obj.Set("name", Napi::String::New(env, name));
  obj.Set("offset", info[1]);
  obj.Set("type", Napi::Number::New(env, (int)type));
  obj.Set("size", Napi::Number::New(env, (int)size));

  return obj;
}

Napi::Value FSUIPC::Remove(const Napi::CallbackInfo& info) {
  FSUIPC* self = this;
  Napi::Env env = info.Env();

  if (info.Length() != 1) {
    throw Napi::TypeError::New(env, "FSUIPC.Remove: requires one argument");
  }

  if (!info[0].IsString()) {
    throw Napi::TypeError::New(
        env, "FSUIPC.Remove: expected first argument to be string");
  }

  std::string name =
      std::string(info[0].As<Napi::String>().Utf8Value().c_str());

  auto it = self->offsets.find(name);

  Napi::Object obj = Napi::Object::New(env);

  obj.Set("name", Napi::String::New(env, it->second.name));
  obj.Set("offset", Napi::Number::New(env, (int)it->second.offset));
  obj.Set("type", Napi::Number::New(env, (int)it->second.type));
  obj.Set("size", Napi::Number::New(env, (int)it->second.size));

  self->offsets.erase(it);

  return obj;
}

void FSUIPC::Write(const Napi::CallbackInfo& info) {
  FSUIPC* self = this;
  Napi::Env env = info.Env();

  if (info.Length() < 3) {
    throw Napi::TypeError::New(env,
                               "FSUIPC.Write: requires at least 3 arguments");
  }

  if (!info[0].IsNumber()) {
    throw Napi::TypeError::New(
        env, "FSUIPC.Write: expected first argument to be uint");
  }

  if (!info[1].IsNumber()) {
    throw Napi::TypeError::New(
        env, "FSUIPC.Write: expected second argument to be int");
  }

  DWORD offset = info[0].ToNumber().Uint32Value();
  Type type = (Type)info[1].ToNumber().Int32Value();

  DWORD size;
  void* value;

  if (type == Type::ByteArray || type == Type::BitArray ||
      type == Type::String) {
    if (info.Length() < 4) {
      throw Napi::TypeError::New(env,
                                 "FSUIPC.Write: requires at least 4 arguments "
                                 "if type is byteArray, bitArray or string");
    }

    if (!info[2].IsNumber()) {
      throw Napi::TypeError::New(
          env, "FSUIPC.Write: expected third argument to be uint");
    }

    size = (int)info[2].ToNumber().Uint32Value();
  } else {
    size = get_size_of_type(type);
  }

  if (size == 0) {
    throw Napi::TypeError::New(env, "FSUIPC.Add: expected size to be > 0");
  }

  value = malloc(size);

  switch (type) {
    case Type::Byte: {
      uint8_t x = (uint8_t)info[2].ToNumber().Uint32Value();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::SByte: {
      int8_t x = (int8_t)info[2].ToNumber().Int32Value();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::Int16: {
      int16_t x = (int16_t)info[2].ToNumber().Int32Value();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::Int32: {
      int32_t x = info[2].ToNumber().Int32Value();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::UInt16: {
      uint16_t x = (uint16_t)info[2].ToNumber().Uint32Value();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::UInt32: {
      uint32_t x = info[2].ToNumber().Uint32Value();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::Double: {
      double x = info[2].ToNumber().DoubleValue();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::Single: {
      float x = (float)info[2].ToNumber().FloatValue();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::Int64: {
      int64_t x;

      if (info[3].IsString()) {
        std::string x_str =
            std::string(info[3].As<Napi::String>().Utf8Value().c_str());
        x = std::stoll(x_str);
      } else if (info[3].IsNumber()) {
        x = (int64_t)info[3].ToNumber().Int64Value();
      } else {
        throw Napi::TypeError::New(env,
                                   "FSUIPC.Write: expected fourth argument to "
                                   "be a string or int when type is int64");
      }

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::UInt64: {
      uint64_t x;

      if (info[3].IsString()) {
        std::string x_str =
            std::string(info[3].As<Napi::String>().Utf8Value().c_str());
        x = std::stoll(x_str);
      } else if (info[3].IsNumber()) {
        x = (uint64_t)info[3].ToNumber().Uint32Value();
      } else {
        throw Napi::TypeError::New(env,
                                   "FSUIPC.Write: expected fourth argument to "
                                   "be a string or int when type is uint64");
      }

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::String: {
      std::memset(value, 0, size);

      std::string x_str =
          std::string(info[3].As<Napi::String>().Utf8Value().c_str());
      if (x_str.length() >= size) {
        throw Napi::TypeError::New(env,
                                   "FSUIPC.Write: expected string's length to "
                                   "be less than the supplied size");
      }

      const char* x_c_str = x_str.c_str();

      strcpy_s((char*)value, size, x_str.c_str());

      break;
    }
    case Type::ByteArray: {
      std::memset(value, 0, size);

      if (info[3].IsArrayBuffer()) {
        Napi::ArrayBuffer view = info[3].As<Napi::ArrayBuffer>();

        std::memcpy(value, view.Data(), size);
      } else {
        throw Napi::TypeError::New(env,
                                   "FSUIPC.Write: expected to receive "
                                   "ArrayBufferView for byte array type");
      }

      break;
    }
    default: {
      throw Napi::TypeError::New(env,
                                 "FSUIPC.Write: unsupported type for write");
    }
  }

  self->offset_writes.push_back(OffsetWrite{type, offset, size, value});
}

void ProcessAsyncWorker::Execute() {
  Error result;

  std::lock_guard<std::mutex> guard(this->fsuipc->offsets_mutex);
  std::lock_guard<std::mutex> fsuipc_guard(this->fsuipc->fsuipc_mutex);

  auto offsets = this->fsuipc->offsets;

  std::map<std::string, Offset>::iterator it = offsets.begin();

  for (; it != offsets.end(); ++it) {
    if (!this->fsuipc->ipc->Read(it->second.offset, it->second.size,
                                 it->second.dest, &result)) {
      this->SetError(ErrorToString(result));
      this->errorCode = static_cast<int>(result);
      return;
    }
  }

  auto offset_writes = this->fsuipc->offset_writes;

  std::vector<OffsetWrite>::iterator write_it = offset_writes.begin();

  for (; write_it != offset_writes.end(); ++write_it) {
    if (!this->fsuipc->ipc->Write(write_it->offset, write_it->size,
                                  write_it->src, &result)) {
      this->SetError(ErrorToString(result));
      this->errorCode = static_cast<int>(result);
      return;
    }

    free(write_it->src);
  }

  this->fsuipc->offset_writes.clear();

  if (!this->fsuipc->ipc->Process(&result)) {
    this->SetError(ErrorToString(result));
    this->errorCode = static_cast<int>(result);
    return;
  }
}

void ProcessAsyncWorker::OnOK() {
  Napi::Env env = this->Env();
  Napi::HandleScope scope(env);

  std::lock_guard<std::mutex> guard(this->fsuipc->offsets_mutex);

  auto offsets = this->fsuipc->offsets;

  Napi::Object obj = Napi::Object::New(env);
  std::map<std::string, Offset>::iterator it = offsets.begin();

  for (; it != offsets.end(); ++it) {
    (obj).Set(it->second.name, this->GetValue(it->second.type, it->second.dest,
                                              it->second.size));
  }

  this->deferred.Resolve(obj);
}

void ProcessAsyncWorker::OnError(const Napi::Error& e) {
  Napi::Env env = this->Env();
  Napi::HandleScope scope(env);

  napi_value args[2] = {Napi::String::New(env, e.Message()),
                        Napi::Number::New(env, this->errorCode)};
  Napi::Value error = FSUIPCError.Value().As<Napi::Function>().New(2, args);

  this->deferred.Reject(error);
}

Napi::Value ProcessAsyncWorker::GetValue(Type type, void* data, size_t length) {
  Napi::Env env = this->Env();
  Napi::EscapableHandleScope scope(env);

  switch (type) {
    case Type::Byte:
      return scope.Escape(Napi::Value::From(env, *((uint8_t*)data)));
    case Type::SByte:
      return scope.Escape(Napi::Value::From(env, *((int8_t*)data)));
    case Type::Int16:
      return scope.Escape(Napi::Value::From(env, *((int16_t*)data)));
    case Type::Int32:
      return scope.Escape(Napi::Value::From(env, *((int32_t*)data)));
    case Type::Int64:
      return scope.Escape(
          Napi::Value::From(env, std::to_string(*((int64_t*)data))));
    case Type::UInt16:
      return scope.Escape(Napi::Value::From(env, *((uint16_t*)data)));
    case Type::UInt32:
      return scope.Escape(Napi::Value::From(env, *((uint32_t*)data)));
    case Type::UInt64:
      return scope.Escape(
          Napi::Value::From(env, std::to_string(*((uint64_t*)data))));
    case Type::Double:
      return scope.Escape(Napi::Value::From(env, *((double*)data)));
    case Type::Single:
      return scope.Escape(Napi::Value::From(env, *((float*)data)));
    case Type::String: {
      char* str = (char*)data;
      return scope.Escape(Napi::Value::From(env, str));
    }
    case Type::BitArray: {
      Napi::Array arr = Napi::Array::New(env, length * 8);
      uint8_t* bits = (uint8_t*)data;
      for (int i = 0; i < length * 8; i++) {
        int byte_index = i / 8;
        int bit_index = i % 8;
        int mask = 1 << bit_index;
        bool value = (bits[byte_index] & mask) != 0;
        arr.Set(i, Napi::Value::From(env, value));
      }
      return scope.Escape(arr);
    }
    case Type::ByteArray: {
      Napi::Array arr = Napi::Array::New(env, length);
      uint8_t* bytes = (uint8_t*)data;
      for (int i = 0; i < length; i++) {
        arr.Set(i, Napi::Value::From(env, *bytes));
        bytes++;
      }
      return scope.Escape(arr);
    }
  }

  return scope.Escape(env.Undefined());
}

DWORD get_size_of_type(Type type) {
  switch (type) {
    case Type::Byte:
    case Type::SByte:
      return 1;
    case Type::Int16:
    case Type::UInt16:
      return 2;
    case Type::Int32:
    case Type::UInt32:
      return 4;
    case Type::Int64:
    case Type::UInt64:
      return 8;
    case Type::Double:
      return 8;
    case Type::Single:
      return 4;
  }
  return 0;
}

void OpenAsyncWorker::Execute() {
  Error result;

  std::lock_guard<std::mutex> fsuipc_guard(this->fsuipc->fsuipc_mutex);

  if (!this->fsuipc->ipc->Open(this->requestedSim, &result)) {
    this->SetError(ErrorToString(result));
    this->errorCode = static_cast<int>(result);
    return;
  }
}

void OpenAsyncWorker::OnOK() {
  Napi::Env env = this->Env();
  Napi::HandleScope scope(env);

  this->deferred.Resolve(this->fsuipc->Value());
}

void OpenAsyncWorker::OnError(const Napi::Error& e) {
  Napi::Env env = this->Env();
  Napi::HandleScope scope(env);

  napi_value args[2] = {Napi::String::New(env, e.Message()),
                        Napi::Number::New(env, this->errorCode)};
  Napi::Value error = FSUIPCError.Value().As<Napi::Function>().New(2, args);

  this->deferred.Reject(error);
}

void CloseAsyncWorker::Execute() {
  std::lock_guard<std::mutex> fsuipc_guard(this->fsuipc->fsuipc_mutex);

  this->fsuipc->ipc->Close();
}

void CloseAsyncWorker::OnOK() {
  Napi::Env env = this->Env();
  Napi::HandleScope scope(env);

  this->deferred.Resolve(this->fsuipc->Value());
}

void InitType(Napi::Env env, Napi::Object exports) {
  Napi::Object obj = Napi::Object::New(env);
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "Byte", Napi::Number::New(env, (int)Type::Byte)));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "SByte", Napi::Number::New(env, (int)Type::SByte)));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "Int16", Napi::Number::New(env, (int)Type::Int16)));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "Int32", Napi::Number::New(env, (int)Type::Int32)));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "Int64", Napi::Number::New(env, (int)Type::Int64)));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "UInt16", Napi::Number::New(env, (int)Type::UInt16)));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "UInt32", Napi::Number::New(env, (int)Type::UInt32)));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "UInt64", Napi::Number::New(env, (int)Type::UInt64)));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "Double", Napi::Number::New(env, (int)Type::Double)));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "Single", Napi::Number::New(env, (int)Type::Single)));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "ByteArray", Napi::Number::New(env, (int)Type::ByteArray)));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "String", Napi::Number::New(env, (int)Type::String)));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "BitArray", Napi::Number::New(env, (int)Type::BitArray)));

  exports.Set(Napi::String::New(env, "Type"), obj);
}

void InitError(Napi::Env env, Napi::Object exports) {
  std::string code =
      "class FSUIPCError extends Error {"
      "  constructor (message, code) {"
      "    super(message);"
      "    this.name = this.constructor.name;"
      "    Error.captureStackTrace(this, this.constructor);"
      "    this.code = code;"
      "  }"
      "};"
      "FSUIPCError";

  Napi::Object errorFunc = env.RunScript(code).ToObject();
  FSUIPCError = Napi::ObjectReference::New(errorFunc, 1);

  exports.Set("FSUIPCError", errorFunc);

  Napi::Object obj = Napi::Object::New(env);
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "OK", Napi::Value::From(env, static_cast<int>(Error::OK))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "OPEN", Napi::Value::From(env, static_cast<int>(Error::OPEN))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "NOFS", Napi::Value::From(env, static_cast<int>(Error::NOFS))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "REGMSG", Napi::Value::From(env, static_cast<int>(Error::REGMSG))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "ATOM", Napi::Value::From(env, static_cast<int>(Error::ATOM))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "MAP", Napi::Value::From(env, static_cast<int>(Error::MAP))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "VIEW", Napi::Value::From(env, static_cast<int>(Error::VIEW))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "VERSION", Napi::Value::From(env, static_cast<int>(Error::VERSION))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "WRONGFS", Napi::Value::From(env, static_cast<int>(Error::WRONGFS))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "NOTOPEN", Napi::Value::From(env, static_cast<int>(Error::NOTOPEN))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "NODATA", Napi::Value::From(env, static_cast<int>(Error::NODATA))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "TIMEOUT", Napi::Value::From(env, static_cast<int>(Error::TIMEOUT))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "SENDMSG", Napi::Value::From(env, static_cast<int>(Error::SENDMSG))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "DATA", Napi::Value::From(env, static_cast<int>(Error::DATA))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "RUNNING", Napi::Value::From(env, static_cast<int>(Error::RUNNING))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "SIZE", Napi::Value::From(env, static_cast<int>(Error::SIZE))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "NOPERMISSION",
      Napi::Value::From(env, static_cast<int>(Error::NOPERMISSION))));

  exports.Set("ErrorCode", obj);
}

void InitSimulator(Napi::Env env, Napi::Object exports) {
  Napi::Object obj = Napi::Object::New(env);
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "ANY", Napi::Value::From(env, static_cast<int>(Simulator::ANY))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "FS98", Napi::Value::From(env, static_cast<int>(Simulator::FS98))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "FS2K", Napi::Value::From(env, static_cast<int>(Simulator::FS2K))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "CFS2", Napi::Value::From(env, static_cast<int>(Simulator::CFS2))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "CFS1", Napi::Value::From(env, static_cast<int>(Simulator::CFS1))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "FLY", Napi::Value::From(env, static_cast<int>(Simulator::FLY))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "FS2K2", Napi::Value::From(env, static_cast<int>(Simulator::FS2K2))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "FS2K4", Napi::Value::From(env, static_cast<int>(Simulator::FS2K4))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "FSX", Napi::Value::From(env, static_cast<int>(Simulator::FSX))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "ESP", Napi::Value::From(env, static_cast<int>(Simulator::ESP))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "P3D", Napi::Value::From(env, static_cast<int>(Simulator::P3D))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "FSX64", Napi::Value::From(env, static_cast<int>(Simulator::FSX64))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "P3D64", Napi::Value::From(env, static_cast<int>(Simulator::P3D64))));
  obj.DefineProperty(Napi::PropertyDescriptor::Value(
      "MSFS", Napi::Value::From(env, static_cast<int>(Simulator::MSFS))));

  exports.Set("Simulator", obj);
}

}  // namespace FSUIPC
