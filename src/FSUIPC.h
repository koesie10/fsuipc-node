#ifndef FSUIPC_H
#define FSUIPC_H

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <nan.h>

#include "helpers.h"

namespace FSUIPC {
enum class Type {
  Byte,
  SByte,
  Int16,
  Int32,
  Int64,
  UInt16,
  UInt32,
  UInt64,
  Double,
  Single,
  ByteArray,
  String,
  BitArray,
};

NAN_MODULE_INIT(InitType);
NAN_MODULE_INIT(InitError);
NAN_MODULE_INIT(InitSimulator);

DWORD get_size_of_type(Type type);

struct Offset {
  std::string name;
  Type type;
  DWORD offset;
  DWORD size;
  void* dest;
};

// https://medium.com/netscape/tutorial-building-native-c-modules-for-node-js-using-nan-part-1-755b07389c7c
class FSUIPC : public Nan::ObjectWrap {
  friend class ProcessAsyncWorker;

 public:
  static NAN_MODULE_INIT(Init);

  static NAN_METHOD(New);
  static NAN_METHOD(Open);
  static NAN_METHOD(Close);

  static NAN_METHOD(Process);
  static NAN_METHOD(Add);
  static NAN_METHOD(Remove);

  static Nan::Persistent<v8::FunctionTemplate> constructor;

 protected:
  std::map<std::string, Offset> offsets;
  std::mutex offsets_mutex;
};

class ProcessAsyncWorker : public PromiseWorker {
 public:
  FSUIPC* fsuipc;

  ProcessAsyncWorker(FSUIPC* fsuipc) : PromiseWorker() {
    this->fsuipc = fsuipc;
  }

  void Execute();

  void HandleOKCallback();
  void HandleErrorCallback();

  v8::Local<v8::Value> GetValue(Type type, void* data, size_t length);

 private:
  int errorCode;
};

class OpenAsyncWorker : public PromiseWorker {
 public:
  FSUIPC* fsuipc;

  OpenAsyncWorker(FSUIPC* fsuipc, int requestedSim) : PromiseWorker() {
    this->fsuipc = fsuipc;
    this->requestedSim = requestedSim;
  }

  void Execute();

  void HandleOKCallback();
  void HandleErrorCallback();

 private:
  int requestedSim;
  int errorCode;
};

class CloseAsyncWorker : public PromiseWorker {
 public:
  FSUIPC* fsuipc;

  CloseAsyncWorker(FSUIPC* fsuipc) : PromiseWorker() { this->fsuipc = fsuipc; }

  void Execute();

  void HandleOKCallback();
};

}  // namespace FSUIPC

#endif
