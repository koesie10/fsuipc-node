#ifndef IPCUSER_H
#define IPCUSER_H

#include <windows.h>

#include <mutex>
#include <vector>

namespace FSUIPC {

enum class Error : int {
  OK = 0,
  OPEN = 1,
  NOFS = 2,
  REGMSG = 3,
  ATOM = 4,
  MAP = 5,
  VIEW = 6,
  VERSION = 7,
  WRONGFS = 8,
  NOTOPEN = 9,
  NODATA = 10,
  TIMEOUT = 11,
  SENDMSG = 12,
  DATA = 13,
  RUNNING = 14,
  SIZE = 15,
  NOPERMISSION = 16  // Operation not permitted DWORD error code 0x5
};

static const char* ErrorToString(const Error error) {
  switch (error) {
    case Error::OK:
      return "Okay";
    case Error::OPEN:
      return "Attempt to Open when already open";
    case Error::NOFS:
      return "Cannot link to FSUIPC or WideClient";
    case Error::REGMSG:
      return "Failed to register common message with Windows";
    case Error::ATOM:
      return "Failed to create Atom for mapping filename";
    case Error::MAP:
      return "Failed to create a file mapping object";
    case Error::VIEW:
      return "Failed to open a view to the file map";
    case Error::VERSION:
      return "Incorrect version of FSUIPC, or not FSUIPC";
    case Error::WRONGFS:
      return "Sim is not version requested";
    case Error::NOTOPEN:
      return "Call cannot execute, link not open";
    case Error::NODATA:
      return "Call cannot execute: no requests accumulated";
    case Error::TIMEOUT:
      return "IPC timed out all retries";
    case Error::SENDMSG:
      return "IPC SendMessage failed all retries";
    case Error::DATA:
      return "IPC request contains bad data";
    case Error::RUNNING:
      return "Maybe running on WideClient, but FS not running on server, or "
             "wrong FSUIPC";
    case Error::SIZE:
      return "Read or Write request cannot be added, memory for process is "
             "full";
    case Error::NOPERMISSION:  // Operation not permitted
      return "Connection denied by the connecting party: please run this "
             "application as admin";
  }

  return "";
}

enum class Simulator : int {
  ANY = 0,
  FS98 = 1,
  FS2K = 2,
  CFS2 = 3,
  CFS1 = 4,
  FLY = 5,
  FS2K2 = 6,
  FS2K4 = 7,
  FSX = 8,
  ESP = 9,
  P3D = 10,
  FSX64 = 11,
  P3D64 = 12,
  MSFS = 13,
};

class IPCUser {
 public:
  ~IPCUser() { this->Close(); }

  bool Open(Simulator requestedVersion, Error* result);
  void Close();
  bool Write(DWORD offset, DWORD size, void* src, Error* result);
  bool Process(Error* result);

  bool Read(DWORD offset, DWORD size, void* dest, Error* result) {
    return this->ReadCommon(false, offset, size, dest, result);
  }
  bool ReadSpecial(DWORD offset, DWORD size, void* dest, Error* result) {
    return this->ReadCommon(true, offset, size, dest, result);
  }

 protected:
  DWORD Version;
  DWORD FSVersion;
  DWORD LibVersion = 2002;

  HWND windowHandle;  // FS6 window handle
  UINT msgId;         // Id of registered window message
  ATOM atom;          // Atom containing name of file-mapping object
  HANDLE mapHandle;   // Handle of file-mapping object
  BYTE* viewPointer;  // Pointer to view of file-mapping object
  BYTE* nextPointer;

  std::vector<void*> destinations;

 private:
  bool ReadCommon(bool special,
                  DWORD offset,
                  DWORD size,
                  void* dest,
                  Error* result);
};

}  // namespace FSUIPC

#endif