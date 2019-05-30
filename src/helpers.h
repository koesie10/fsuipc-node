#ifndef HELPERS_H
#define HELPERS_H

namespace FSUIPC {

// https://github.com/nodejs/nan/blob/v2.8.0/nan.h#L1504
class PromiseWorker {
 public:
  explicit PromiseWorker() : errmsg_(NULL) {
    request.data = this;

    Nan::HandleScope scope;

    resolver.Reset(
        v8::Promise::Resolver::New(Nan::GetCurrentContext()).ToLocalChecked());

    v8::Local<v8::Object> obj = Nan::New<v8::Object>();
    persistentHandle.Reset(obj);

    async_resource = new Nan::AsyncResource("PromiseWorker", obj);
  }

  virtual ~PromiseWorker() {
    Nan::HandleScope scope;

    if (!persistentHandle.IsEmpty())
      persistentHandle.Reset();
    if (!resolver.IsEmpty())
      resolver.Reset();
    delete[] errmsg_;

    delete async_resource;
  }

  virtual void WorkComplete() {
    Nan::HandleScope scope;

    if (errmsg_ == NULL)
      HandleOKCallback();
    else
      HandleErrorCallback();

    // KickNextTick(), which will make sure our promises work even with
    // setTimeout or setInterval See https://github.com/nodejs/nan/issues/539
    Nan::Callback(Nan::New<v8::Function>(
                      [](const Nan::FunctionCallbackInfo<v8::Value>& info) {},
                      Nan::Null()))
        .Call(0, nullptr, async_resource);
  }

  inline void SaveToPersistent(const char* key,
                               const v8::Local<v8::Value>& value) {
    Nan::HandleScope scope;
    Nan::New(persistentHandle)
        ->Set(Nan::GetCurrentContext(), Nan::New(key).ToLocalChecked(), value);
  }

  inline void SaveToPersistent(const v8::Local<v8::String>& key,
                               const v8::Local<v8::Value>& value) {
    Nan::HandleScope scope;
    Nan::New(persistentHandle)->Set(Nan::GetCurrentContext(), key, value);
  }

  inline void SaveToPersistent(uint32_t index,
                               const v8::Local<v8::Value>& value) {
    Nan::HandleScope scope;
    Nan::New(persistentHandle)->Set(Nan::GetCurrentContext(), index, value);
  }

  inline v8::Local<v8::Value> GetFromPersistent(const char* key) const {
    Nan::EscapableHandleScope scope;
    return scope.Escape(
        Nan::New(persistentHandle)
            ->Get(Nan::GetCurrentContext(), Nan::New(key).ToLocalChecked())
            .ToLocalChecked());
  }

  inline v8::Local<v8::Value> GetFromPersistent(
      const v8::Local<v8::String>& key) const {
    Nan::EscapableHandleScope scope;
    return scope.Escape(Nan::New(persistentHandle)
                            ->Get(Nan::GetCurrentContext(), key)
                            .ToLocalChecked());
  }

  inline v8::Local<v8::Value> GetFromPersistent(uint32_t index) const {
    Nan::EscapableHandleScope scope;
    return scope.Escape(Nan::New(persistentHandle)
                            ->Get(Nan::GetCurrentContext(), index)
                            .ToLocalChecked());
  }

  virtual void Execute() = 0;

  uv_work_t request;

  virtual void Destroy() { delete this; }

  v8::Local<v8::Promise> GetPromise() {
    Nan::EscapableHandleScope scope;
    return scope.Escape(Nan::New(resolver)->GetPromise());
  }

 protected:
  Nan::Persistent<v8::Object> persistentHandle;
  Nan::Persistent<v8::Promise::Resolver> resolver;
  Nan::AsyncResource* async_resource;

  virtual void HandleOKCallback() {
    Nan::HandleScope scope;

    Nan::New(resolver)->Resolve(Nan::GetCurrentContext(), Nan::Undefined());
  }

  virtual void HandleErrorCallback() {
    Nan::HandleScope scope;

    Nan::New(resolver)->Reject(
        Nan::GetCurrentContext(),
        v8::Exception::Error(
            Nan::New<v8::String>(ErrorMessage()).ToLocalChecked()));
  }

  void SetErrorMessage(const char* msg) {
    delete[] errmsg_;

    size_t size = strlen(msg) + 1;
    errmsg_ = new char[size];
    memcpy(errmsg_, msg, size);
  }

  const char* ErrorMessage() const { return errmsg_; }

 private:
  NAN_DISALLOW_ASSIGN_COPY_MOVE(PromiseWorker)
  char* errmsg_;
};

inline void PromiseExecute(uv_work_t* req) {
  PromiseWorker* worker = static_cast<PromiseWorker*>(req->data);
  worker->Execute();
}

inline void PromiseExecuteComplete(uv_work_t* req) {
  PromiseWorker* worker = static_cast<PromiseWorker*>(req->data);
  worker->WorkComplete();
  worker->Destroy();
}

inline void PromiseQueueWorker(PromiseWorker* worker) {
  uv_queue_work(uv_default_loop(), &worker->request, PromiseExecute,
                reinterpret_cast<uv_after_work_cb>(PromiseExecuteComplete));
}

}  // namespace FSUIPC

#endif
