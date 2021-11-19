// fsuipc.cc
#include "FSUIPC.h"

#include <nan.h>
#include <node.h>
#include <windows.h>

#include <string>

#include "IPCUser.h"

namespace FSUIPC {

Nan::Persistent<v8::FunctionTemplate> FSUIPC::constructor;
Nan::Persistent<v8::Object> FSUIPCError;

NAN_MODULE_INIT(FSUIPC::Init) {
  v8::Local<v8::FunctionTemplate> ctor =
      Nan::New<v8::FunctionTemplate>(FSUIPC::New);
  constructor.Reset(ctor);
  ctor->InstanceTemplate()->SetInternalFieldCount(1);
  ctor->SetClassName(Nan::New("FSUIPC").ToLocalChecked());

  Nan::SetPrototypeMethod(ctor, "open", Open);
  Nan::SetPrototypeMethod(ctor, "close", Close);

  Nan::SetPrototypeMethod(ctor, "process", Process);

  Nan::SetPrototypeMethod(ctor, "add", Add);
  Nan::SetPrototypeMethod(ctor, "remove", Remove);

  Nan::SetPrototypeMethod(ctor, "write", Write);

  target->Set(Nan::GetCurrentContext(), Nan::New("FSUIPC").ToLocalChecked(),
              ctor->GetFunction(Nan::GetCurrentContext()).ToLocalChecked());
}

NAN_METHOD(FSUIPC::New) {
  // throw an error if constructor is called without new keyword
  if (!info.IsConstructCall()) {
    return Nan::ThrowError(
        Nan::New("FSUIPC.new - called without new keyword").ToLocalChecked());
  }

  if (info.Length() != 0) {
    return Nan::ThrowError(
        Nan::New("FSUIPC.new - expected no arguments").ToLocalChecked());
  }

  FSUIPC* fsuipc = new FSUIPC();
  fsuipc->ipc = new IPCUser();
  fsuipc->Wrap(info.Holder());

  info.GetReturnValue().Set(info.Holder());
}

NAN_METHOD(FSUIPC::Open) {
  FSUIPC* self = Nan::ObjectWrap::Unwrap<FSUIPC>(info.This());

  Simulator requestedSim = Simulator::ANY;

  if (info.Length() > 0) {
    if (!info[0]->IsUint32()) {
      return Nan::ThrowTypeError(
          Nan::New("FSUIPC.open - expected first argument to be Simulator")
              .ToLocalChecked());
    }

    requestedSim = static_cast<Simulator>(
        info[0]->Uint32Value(Nan::GetCurrentContext()).ToChecked());
  }

  auto worker = new OpenAsyncWorker(self, requestedSim);

  PromiseQueueWorker(worker);

  info.GetReturnValue().Set(worker->GetPromise());
}

NAN_METHOD(FSUIPC::Close) {
  FSUIPC* self = Nan::ObjectWrap::Unwrap<FSUIPC>(info.This());

  auto worker = new CloseAsyncWorker(self);

  PromiseQueueWorker(worker);

  info.GetReturnValue().Set(worker->GetPromise());
}

NAN_METHOD(FSUIPC::Process) {
  FSUIPC* self = Nan::ObjectWrap::Unwrap<FSUIPC>(info.This());

  auto worker = new ProcessAsyncWorker(self);

  PromiseQueueWorker(worker);

  info.GetReturnValue().Set(worker->GetPromise());
}

NAN_METHOD(FSUIPC::Add) {
  FSUIPC* self = Nan::ObjectWrap::Unwrap<FSUIPC>(info.This());

  if (info.Length() < 3) {
    return Nan::ThrowError(
        Nan::New("FSUIPC.Add: requires at least 3 arguments").ToLocalChecked());
  }

  if (!info[0]->IsString()) {
    return Nan::ThrowTypeError(
        Nan::New("FSUIPC.Add: expected first argument to be string")
            .ToLocalChecked());
  }

  if (!info[1]->IsUint32()) {
    return Nan::ThrowTypeError(
        Nan::New("FSUIPC.Add: expected second argument to be uint")
            .ToLocalChecked());
  }

  if (!info[2]->IsInt32()) {
    return Nan::ThrowTypeError(
        Nan::New("FSUIPC.Add: expected third argument to be int")
            .ToLocalChecked());
  }

  std::string name = std::string(*Nan::Utf8String(info[0]));
  DWORD offset = info[1]->Uint32Value(Nan::GetCurrentContext()).ToChecked();
  Type type = (Type)info[2]->Int32Value(Nan::GetCurrentContext()).ToChecked();

  DWORD size;

  if (type == Type::ByteArray || type == Type::BitArray ||
      type == Type::String) {
    if (info.Length() < 4) {
      return Nan::ThrowTypeError(
          Nan::New("FSUIPC.Add: requires at least 4 arguments if type is "
                   "byteArray, bitArray or string")
              .ToLocalChecked());
    }

    if (!info[3]->IsUint32()) {
      return Nan::ThrowTypeError(
          Nan::New("FSUIPC.Add: expected fourth argument to be uint")
              .ToLocalChecked());
    }

    size = (int)info[3]->Uint32Value(Nan::GetCurrentContext()).ToChecked();
  } else {
    size = get_size_of_type(type);
  }

  if (size == 0) {
    return Nan::ThrowTypeError(
        Nan::New("FSUIPC.Add: expected fourth argument to be a size > 0")
            .ToLocalChecked());
  }

  self->offsets[name] = Offset{name, type, offset, size, malloc(size)};

  v8::Local<v8::Object> obj = Nan::New<v8::Object>();

  Nan::Set(obj, Nan::New("name").ToLocalChecked(),
           Nan::New(name).ToLocalChecked());
  Nan::Set(obj, Nan::New("offset").ToLocalChecked(), info[1]);
  Nan::Set(obj, Nan::New("type").ToLocalChecked(), Nan::New((int)type));
  Nan::Set(obj, Nan::New("size").ToLocalChecked(), Nan::New((int)size));

  info.GetReturnValue().Set(obj);
}

NAN_METHOD(FSUIPC::Remove) {
  FSUIPC* self = Nan::ObjectWrap::Unwrap<FSUIPC>(info.This());

  if (info.Length() != 1) {
    return Nan::ThrowError(
        Nan::New("FSUIPC.Remove: requires one argument").ToLocalChecked());
  }

  if (!info[0]->IsString()) {
    return Nan::ThrowTypeError(
        Nan::New("FSUIPC.Remove: expected first argument to be string")
            .ToLocalChecked());
  }

  std::string name = std::string(*Nan::Utf8String(info[0]));

  auto it = self->offsets.find(name);

  v8::Local<v8::Object> obj = Nan::New<v8::Object>();

  Nan::Set(obj, Nan::New("name").ToLocalChecked(),
           Nan::New(it->second.name).ToLocalChecked());
  Nan::Set(obj, Nan::New("offset").ToLocalChecked(),
           Nan::New((int)it->second.offset));
  Nan::Set(obj, Nan::New("type").ToLocalChecked(),
           Nan::New((int)it->second.type));
  Nan::Set(obj, Nan::New("size").ToLocalChecked(),
           Nan::New((int)it->second.size));

  self->offsets.erase(it);

  info.GetReturnValue().Set(obj);
}

NAN_METHOD(FSUIPC::Write) {
  FSUIPC* self = Nan::ObjectWrap::Unwrap<FSUIPC>(info.This());

  if (info.Length() < 3) {
    return Nan::ThrowError(
        Nan::New("FSUIPC.Write: requires at least 3 arguments")
            .ToLocalChecked());
  }

  if (!info[0]->IsUint32()) {
    return Nan::ThrowTypeError(
        Nan::New("FSUIPC.Write: expected first argument to be uint")
            .ToLocalChecked());
  }

  if (!info[1]->IsInt32()) {
    return Nan::ThrowTypeError(
        Nan::New("FSUIPC.Write: expected second argument to be int")
            .ToLocalChecked());
  }

  DWORD offset = info[0]->Uint32Value(Nan::GetCurrentContext()).ToChecked();
  Type type = (Type)info[1]->Int32Value(Nan::GetCurrentContext()).ToChecked();

  DWORD size;
  void* value;

  if (type == Type::ByteArray || type == Type::BitArray ||
      type == Type::String) {
    if (info.Length() < 4) {
      return Nan::ThrowTypeError(
          Nan::New("FSUIPC.Write: requires at least 4 arguments if type is "
                   "byteArray, bitArray or string")
              .ToLocalChecked());
    }

    if (!info[2]->IsUint32()) {
      return Nan::ThrowTypeError(
          Nan::New("FSUIPC.Write: expected third argument to be uint")
              .ToLocalChecked());
    }

    size = (int)info[2]->Uint32Value(Nan::GetCurrentContext()).ToChecked();
  } else {
    size = get_size_of_type(type);
  }

  if (size == 0) {
    return Nan::ThrowTypeError(
        Nan::New("FSUIPC.Add: expected size to be > 0").ToLocalChecked());
  }

  value = malloc(size);

  switch (type) {
    case Type::Byte: {
      uint8_t x =
          (uint8_t)info[2]->Uint32Value(Nan::GetCurrentContext()).ToChecked();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::SByte: {
      int8_t x =
          (int8_t)info[2]->Int32Value(Nan::GetCurrentContext()).ToChecked();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::Int16: {
      int16_t x =
          (int16_t)info[2]->Int32Value(Nan::GetCurrentContext()).ToChecked();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::Int32: {
      int32_t x = info[2]->Int32Value(Nan::GetCurrentContext()).ToChecked();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::UInt16: {
      uint16_t x =
          (uint16_t)info[2]->Uint32Value(Nan::GetCurrentContext()).ToChecked();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::UInt32: {
      uint32_t x = info[2]->Uint32Value(Nan::GetCurrentContext()).ToChecked();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::Double: {
      double x = info[2]->NumberValue(Nan::GetCurrentContext()).ToChecked();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::Single: {
      float x =
          (float)info[2]->NumberValue(Nan::GetCurrentContext()).ToChecked();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::Int64: {
      int64_t x;

      if (info[3]->IsString()) {
        std::string x_str = std::string(*Nan::Utf8String(info[3]));
        x = std::stoll(x_str);
      } else if (info[3]->IsInt32()) {
        x = (int64_t)info[3]->Int32Value(Nan::GetCurrentContext()).ToChecked();
      } else {
        return Nan::ThrowTypeError(
            Nan::New("FSUIPC.Write: expected fourth argument to be a string or "
                     "int when type is int64")
                .ToLocalChecked());
      }

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::UInt64: {
      uint64_t x;

      if (info[3]->IsString()) {
        std::string x_str = std::string(*Nan::Utf8String(info[3]));
        x = std::stoll(x_str);
      } else if (info[3]->IsUint32()) {
        x = (uint64_t)info[3]
                ->Uint32Value(Nan::GetCurrentContext())
                .ToChecked();
      } else {
        return Nan::ThrowTypeError(
            Nan::New("FSUIPC.Write: expected fourth argument to be a string or "
                     "int when type is uint64")
                .ToLocalChecked());
      }

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::String: {
      std::memset(value, 0, size);

      std::string x_str = std::string(*Nan::Utf8String(info[3]));
      if (x_str.length() >= size) {
        return Nan::ThrowTypeError(
            Nan::New("FSUIPC.Write: expected string's length to be less than "
                     "the supplied size")
                .ToLocalChecked());
      }

      const char* x_c_str = x_str.c_str();

      strcpy_s((char*)value, size, x_str.c_str());

      break;
    }
    case Type::ByteArray: {
      std::memset(value, 0, size);

      if (info[3]->IsArrayBuffer()) {
        v8::Local<v8::ArrayBufferView> view =
            v8::Local<v8::ArrayBufferView>::Cast(info[3]);

        view->CopyContents(value, size);
      } else {
        return Nan::ThrowTypeError(
            Nan::New("FSUIPC.Write: expected to receive ArrayBufferView for "
                     "byte array type")
                .ToLocalChecked());
      }

      break;
    }
    default: {
      return Nan::ThrowTypeError(
          Nan::New("FSUIPC.Write: unsupported type for write")
              .ToLocalChecked());
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
      this->SetErrorMessage(ErrorToString(result));
      this->errorCode = static_cast<int>(result);
      return;
    }
  }

  auto offset_writes = this->fsuipc->offset_writes;

  std::vector<OffsetWrite>::iterator write_it = offset_writes.begin();

  for (; write_it != offset_writes.end(); ++write_it) {
    if (!this->fsuipc->ipc->Write(write_it->offset, write_it->size,
                                  write_it->src, &result)) {
      this->SetErrorMessage(ErrorToString(result));
      this->errorCode = static_cast<int>(result);
      return;
    }

    free(write_it->src);
  }

  this->fsuipc->offset_writes.clear();

  if (!this->fsuipc->ipc->Process(&result)) {
    this->SetErrorMessage(ErrorToString(result));
    this->errorCode = static_cast<int>(result);
    return;
  }
}

void ProcessAsyncWorker::HandleOKCallback() {
  Nan::HandleScope scope;

  std::lock_guard<std::mutex> guard(this->fsuipc->offsets_mutex);

  auto offsets = this->fsuipc->offsets;

  v8::Local<v8::Object> obj = Nan::New<v8::Object>();
  std::map<std::string, Offset>::iterator it = offsets.begin();

  for (; it != offsets.end(); ++it) {
    Nan::Set(obj, Nan::New(it->second.name).ToLocalChecked(),
             this->GetValue(it->second.type, it->second.dest, it->second.size));
  }

  Nan::New(resolver)->Resolve(Nan::GetCurrentContext(), obj);
}

void ProcessAsyncWorker::HandleErrorCallback() {
  Nan::HandleScope scope;

  v8::Local<v8::Value> argv[] = {Nan::New(ErrorMessage()).ToLocalChecked(),
                                 Nan::New(this->errorCode)};
  v8::Local<v8::Value> error =
      Nan::CallAsConstructor(Nan::New(FSUIPCError), 2, argv).ToLocalChecked();

  Nan::New(resolver)->Reject(Nan::GetCurrentContext(), error);
}

v8::Local<v8::Value> ProcessAsyncWorker::GetValue(Type type,
                                                  void* data,
                                                  size_t length) {
  Nan::EscapableHandleScope scope;

  switch (type) {
    case Type::Byte:
      return scope.Escape(Nan::New(*((uint8_t*)data)));
    case Type::SByte:
      return scope.Escape(Nan::New(*((int8_t*)data)));
    case Type::Int16:
      return scope.Escape(Nan::New(*((int16_t*)data)));
    case Type::Int32:
      return scope.Escape(Nan::New(*((int32_t*)data)));
    case Type::Int64:
      return scope.Escape(
          Nan::New(std::to_string(*((int64_t*)data))).ToLocalChecked());
    case Type::UInt16:
      return scope.Escape(Nan::New(*((uint16_t*)data)));
    case Type::UInt32:
      return scope.Escape(Nan::New(*((uint32_t*)data)));
    case Type::UInt64:
      return scope.Escape(
          Nan::New(std::to_string(*((uint64_t*)data))).ToLocalChecked());
    case Type::Double:
      return scope.Escape(Nan::New(*((double*)data)));
    case Type::Single:
      return scope.Escape(Nan::New(*((float*)data)));
    case Type::String: {
      char* str = (char*)data;
      return scope.Escape(Nan::New(str).ToLocalChecked());
    }
    case Type::BitArray: {
      v8::Local<v8::Array> arr = Nan::New<v8::Array>(length * 8);
      uint8_t* bits = (uint8_t*)data;
      for (int i = 0; i < length * 8; i++) {
        int byte_index = i / 8;
        int bit_index = i % 8;
        int mask = 1 << bit_index;
        bool value = (bits[byte_index] & mask) != 0;
        arr->Set(Nan::GetCurrentContext(), i, Nan::New(value));
      }
      return scope.Escape(arr);
    }
    case Type::ByteArray: {
      v8::Local<v8::Array> arr = Nan::New<v8::Array>(length);
      uint8_t* bytes = (uint8_t*)data;
      for (int i = 0; i < length; i++) {
        arr->Set(Nan::GetCurrentContext(), i, Nan::New(*bytes));
        bytes++;
      }
      return scope.Escape(arr);
    }
  }

  return scope.Escape(Nan::Undefined());
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
    this->SetErrorMessage(ErrorToString(result));
    this->errorCode = static_cast<int>(result);
    return;
  }
}

void OpenAsyncWorker::HandleOKCallback() {
  Nan::HandleScope scope;

  Nan::New(resolver)->Resolve(Nan::GetCurrentContext(), this->fsuipc->handle());
}

void OpenAsyncWorker::HandleErrorCallback() {
  Nan::HandleScope scope;

  v8::Local<v8::Value> argv[] = {Nan::New(ErrorMessage()).ToLocalChecked(),
                                 Nan::New(this->errorCode)};
  v8::Local<v8::Value> error =
      Nan::CallAsConstructor(Nan::New(FSUIPCError), 2, argv).ToLocalChecked();

  Nan::New(resolver)->Reject(Nan::GetCurrentContext(), error);
}

void CloseAsyncWorker::Execute() {
  std::lock_guard<std::mutex> fsuipc_guard(this->fsuipc->fsuipc_mutex);

  this->fsuipc->ipc->Close();
}

void CloseAsyncWorker::HandleOKCallback() {
  Nan::HandleScope scope;

  Nan::New(resolver)->Resolve(Nan::GetCurrentContext(), this->fsuipc->handle());
}

NAN_MODULE_INIT(InitType) {
  v8::Local<v8::Object> obj = Nan::New<v8::Object>();
  Nan::DefineOwnProperty(obj, Nan::New("Byte").ToLocalChecked(),
                         Nan::New((int)Type::Byte), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("SByte").ToLocalChecked(),
                         Nan::New((int)Type::SByte), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("Int16").ToLocalChecked(),
                         Nan::New((int)Type::Int16), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("Int32").ToLocalChecked(),
                         Nan::New((int)Type::Int32), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("Int64").ToLocalChecked(),
                         Nan::New((int)Type::Int64), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("UInt16").ToLocalChecked(),
                         Nan::New((int)Type::UInt16), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("UInt32").ToLocalChecked(),
                         Nan::New((int)Type::UInt32), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("UInt64").ToLocalChecked(),
                         Nan::New((int)Type::UInt64), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("Double").ToLocalChecked(),
                         Nan::New((int)Type::Double), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("Single").ToLocalChecked(),
                         Nan::New((int)Type::Single), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("ByteArray").ToLocalChecked(),
                         Nan::New((int)Type::ByteArray), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("String").ToLocalChecked(),
                         Nan::New((int)Type::String), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("BitArray").ToLocalChecked(),
                         Nan::New((int)Type::BitArray), v8::ReadOnly);

  target->Set(Nan::GetCurrentContext(), Nan::New("Type").ToLocalChecked(), obj);
}

NAN_MODULE_INIT(InitError) {
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
  v8::Local<Nan::BoundScript> script =
      Nan::CompileScript(Nan::New(code).ToLocalChecked()).ToLocalChecked();
  v8::Local<v8::Object> errorFunc = Nan::RunScript(script)
                                        .ToLocalChecked()
                                        ->ToObject(Nan::GetCurrentContext())
                                        .ToLocalChecked();

  FSUIPCError.Reset(errorFunc);

  target->Set(Nan::GetCurrentContext(),
              Nan::New("FSUIPCError").ToLocalChecked(), errorFunc);

  v8::Local<v8::Object> obj = Nan::New<v8::Object>();
  Nan::DefineOwnProperty(obj, Nan::New("OK").ToLocalChecked(),
                         Nan::New(static_cast<int>(Error::OK)), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("OPEN").ToLocalChecked(),
                         Nan::New(static_cast<int>(Error::OPEN)), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("NOFS").ToLocalChecked(),
                         Nan::New(static_cast<int>(Error::NOFS)), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("REGMSG").ToLocalChecked(),
                         Nan::New(static_cast<int>(Error::REGMSG)),
                         v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("ATOM").ToLocalChecked(),
                         Nan::New(static_cast<int>(Error::ATOM)), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("MAP").ToLocalChecked(),
                         Nan::New(static_cast<int>(Error::MAP)), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("VIEW").ToLocalChecked(),
                         Nan::New(static_cast<int>(Error::VIEW)), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("VERSION").ToLocalChecked(),
                         Nan::New(static_cast<int>(Error::VERSION)),
                         v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("WRONGFS").ToLocalChecked(),
                         Nan::New(static_cast<int>(Error::WRONGFS)),
                         v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("NOTOPEN").ToLocalChecked(),
                         Nan::New(static_cast<int>(Error::NOTOPEN)),
                         v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("NODATA").ToLocalChecked(),
                         Nan::New(static_cast<int>(Error::NODATA)),
                         v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("TIMEOUT").ToLocalChecked(),
                         Nan::New(static_cast<int>(Error::TIMEOUT)),
                         v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("SENDMSG").ToLocalChecked(),
                         Nan::New(static_cast<int>(Error::SENDMSG)),
                         v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("DATA").ToLocalChecked(),
                         Nan::New(static_cast<int>(Error::DATA)), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("RUNNING").ToLocalChecked(),
                         Nan::New(static_cast<int>(Error::RUNNING)),
                         v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("SIZE").ToLocalChecked(),
                         Nan::New(static_cast<int>(Error::SIZE)), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("EPERMISSION").ToLocalChecked(),
                         Nan::New(static_cast<int>(Error::EPERMISSION)), v8::ReadOnly);

  target->Set(Nan::GetCurrentContext(), Nan::New("ErrorCode").ToLocalChecked(),
              obj);
}

NAN_MODULE_INIT(InitSimulator) {
  v8::Local<v8::Object> obj = Nan::New<v8::Object>();
  Nan::DefineOwnProperty(obj, Nan::New("ANY").ToLocalChecked(),
                         Nan::New(static_cast<int>(Simulator::ANY)),
                         v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("FS98").ToLocalChecked(),
                         Nan::New(static_cast<int>(Simulator::FS98)),
                         v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("FS2K").ToLocalChecked(),
                         Nan::New(static_cast<int>(Simulator::FS2K)),
                         v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("CFS2").ToLocalChecked(),
                         Nan::New(static_cast<int>(Simulator::CFS2)),
                         v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("CFS1").ToLocalChecked(),
                         Nan::New(static_cast<int>(Simulator::CFS1)),
                         v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("FLY").ToLocalChecked(),
                         Nan::New(static_cast<int>(Simulator::FLY)),
                         v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("FS2K2").ToLocalChecked(),
                         Nan::New(static_cast<int>(Simulator::FS2K2)),
                         v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("FS2K4").ToLocalChecked(),
                         Nan::New(static_cast<int>(Simulator::FS2K4)),
                         v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("FSX").ToLocalChecked(),
                         Nan::New(static_cast<int>(Simulator::FSX)),
                         v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("ESP").ToLocalChecked(),
                         Nan::New(static_cast<int>(Simulator::ESP)),
                         v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("P3D").ToLocalChecked(),
                         Nan::New(static_cast<int>(Simulator::P3D)),
                         v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("FSX64").ToLocalChecked(),
                         Nan::New(static_cast<int>(Simulator::FSX64)),
                         v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("P3D64").ToLocalChecked(),
                         Nan::New(static_cast<int>(Simulator::P3D64)),
                         v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("MSFS").ToLocalChecked(),
                         Nan::New(static_cast<int>(Simulator::MSFS)),
                         v8::ReadOnly);

  target->Set(Nan::GetCurrentContext(), Nan::New("Simulator").ToLocalChecked(),
              obj);
}

}  // namespace FSUIPC
