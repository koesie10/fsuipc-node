#ifndef FSUIPC_H
#define FSUIPC_H

// Disable winsock.h
#include <napi.h>
#include <winsock2.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "IPCUser.h"

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

void InitType(Napi::Env env, Napi::Object exports);
void InitError(Napi::Env env, Napi::Object exports);
void InitSimulator(Napi::Env env, Napi::Object exports);

DWORD get_size_of_type(Type type);

struct Offset {
  std::string name;
  Type type;
  DWORD offset;
  DWORD size;
  void* dest;
};

struct OffsetWrite {
  Type type;
  DWORD offset;
  DWORD size;
  void* src;  // Will be freed on Process()
};

// https://medium.com/netscape/tutorial-building-native-c-modules-for-node-js-using-nan-part-1-755b07389c7c
class FSUIPC : public Napi::ObjectWrap<FSUIPC> {
  friend class ProcessAsyncWorker;
  friend class OpenAsyncWorker;
  friend class CloseAsyncWorker;

 public:
  static void Init(Napi::Env env, Napi::Object exports);

  FSUIPC(const Napi::CallbackInfo& info);
  Napi::Value Open(const Napi::CallbackInfo& info);
  Napi::Value Close(const Napi::CallbackInfo& info);

  Napi::Value Process(const Napi::CallbackInfo& info);
  Napi::Value Add(const Napi::CallbackInfo& info);
  Napi::Value Remove(const Napi::CallbackInfo& info);
  void Write(const Napi::CallbackInfo& info);

  static Napi::FunctionReference constructor;

  ~FSUIPC() {
    if (this->ipc) {
      delete this->ipc;
    }
  }

 protected:
  std::map<std::string, Offset> offsets;
  std::vector<OffsetWrite> offset_writes;
  std::mutex offsets_mutex;
  std::mutex fsuipc_mutex;
  IPCUser* ipc;
};

class ProcessAsyncWorker : public Napi::AsyncWorker {
 public:
  FSUIPC* fsuipc;

  ProcessAsyncWorker(Napi::Env& env,
                     Napi::Promise::Deferred deferred,
                     FSUIPC* fsuipc)
      : Napi::AsyncWorker(env), deferred(deferred), fsuipc(fsuipc) {}

  void Execute() override;

  void OnOK() override;
  void OnError(const Napi::Error& e) override;

  Napi::Value GetValue(Type type, void* data, size_t length);

 private:
  int errorCode;
  Napi::Promise::Deferred deferred;
};

class OpenAsyncWorker : public Napi::AsyncWorker {
 public:
  FSUIPC* fsuipc;

  OpenAsyncWorker(Napi::Env& env,
                  Napi::Promise::Deferred deferred,
                  FSUIPC* fsuipc,
                  Simulator requestedSim)
      : Napi::AsyncWorker(env),
        deferred(deferred),
        fsuipc(fsuipc),
        requestedSim(requestedSim) {}

  void Execute() override;

  void OnOK() override;
  void OnError(const Napi::Error& e) override;

 private:
  Simulator requestedSim;
  int errorCode;
  Napi::Promise::Deferred deferred;
};

class CloseAsyncWorker : public Napi::AsyncWorker {
 public:
  FSUIPC* fsuipc;

  CloseAsyncWorker(Napi::Env& env,
                   Napi::Promise::Deferred deferred,
                   FSUIPC* fsuipc)
      : Napi::AsyncWorker(env), deferred(deferred), fsuipc(fsuipc) {}

  void Execute() override;

  void OnOK() override;

 private:
  Napi::Promise::Deferred deferred;
};

}  // namespace FSUIPC

#endif
