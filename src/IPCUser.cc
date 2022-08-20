#include "IPCUser.h"

#define MSGNAME "FsasmLib:IPC"

#define MAX_SIZE \
  0x7F00  // Largest data (kept below 32k to avoid any possible 16-bit sign
          // problems)

#define FS6IPC_MESSAGE_SUCCESS 1
#define FS6IPC_MESSAGE_FAILURE 0

// IPC message types
#define F64IPC_READSTATEDATA_ID 1
#define FS6IPC_WRITESTATEDATA_ID 2

#pragma pack(push, r1, 1)
// read request structure
typedef struct tagF64IPC_READSTATEDATA_HDR {
  DWORD dwId;      // F64IPC_READSTATEDATA_ID
  DWORD dwOffset;  // state table offset
  DWORD nBytes;    // number of bytes of state data to read
  uint32_t pDest;  // destination buffer for data (client use only)
} F64IPC_READSTATEDATA_HDR;

// write request structure
typedef struct tagFS6IPC_WRITESTATEDATA_HDR {
  DWORD dwId;      // FS6IPC_WRITESTATEDATA_ID
  DWORD dwOffset;  // state table offset
  DWORD nBytes;    // number of bytes of state data to write
} FS6IPC_WRITESTATEDATA_HDR;

#pragma pack(pop, r1)

namespace FSUIPC {
bool IPCUser::Open(Simulator requestedVersion, Error* result) {
  char szName[MAX_PATH];
  static int nTry = 0;
  bool isWideFS = false;
  int i = 0;

  // abort if already started
  if (this->viewPointer) {
    *result = Error::OPEN;
    return false;
  }

  // Clear version information, so know when connected
  this->Version = this->FSVersion = 0;

  // Connect via FSUIPC, which is known to be FSUIPC's own
  // and isn't subject to user modification
  this->windowHandle = FindWindowEx(nullptr, nullptr, "UIPCMAIN", nullptr);
  if (!this->windowHandle) {
    // If there's no UIPCMAIN, we may be using WideClient,
    // which only simulates FS98
    this->windowHandle = FindWindowEx(nullptr, nullptr, "FS98MAIN", nullptr);
    isWideFS = true;
    if (!this->windowHandle) {
      *result = Error::NOFS;
      return false;
    }
  }

  // Register the window message
  this->msgId = RegisterWindowMessage(MSGNAME);
  if (this->msgId == 0) {
    *result = Error::REGMSG;
    return false;
  }

  // Create the name of our file-mapping object
  nTry++;  // Ensures a unique string is used in case user closes and reopens
  wsprintf(szName, MSGNAME, ":%X:%X", GetCurrentProcessId(), nTry);

  // Stuff the name into a global atom
  this->atom = GlobalAddAtom(szName);
  if (this->atom == 0) {
    *result = Error::ATOM;
    this->Close();
    return false;
  }

  // Create the file-mapping object
  this->mapHandle =
      CreateFileMapping(INVALID_HANDLE_VALUE,  // Use system paging file
                        nullptr,               // Security
                        PAGE_READWRITE,        // Protection
                        0, MAX_SIZE + 256,     // Size
                        szName                 // Name
      );
  if (this->mapHandle == 0 || GetLastError() == ERROR_ALREADY_EXISTS) {
    *result = Error::MAP;
    this->Close();
    return false;
  }

  // Get a view of the file-mapping object
  this->viewPointer =
      (BYTE*)MapViewOfFile(this->mapHandle, FILE_MAP_WRITE, 0, 0, 0);
  if (this->viewPointer == nullptr) {
    *result = Error::VIEW;
    this->Close();
    return false;
  }

  // Now determine FSUIPC version and FS type
  this->nextPointer = this->viewPointer;

  this->destinations = std::vector<void*>();

  // Try up to 5 times with a 100ms rest between each
  // Note that WideClient returns zeroes initially, whilst waiting
  // for the server to get the data
  while (i++ < 5 && (this->Version == 0) || (this->FSVersion == 0)) {
    // Read FSUIPC Version
    if (!this->Read(0x3304, 4, &this->Version, result)) {
      this->Close();
      return false;
    }

    // Read FS version and validity check pattern
    if (!this->Read(0x3308, 4, &this->FSVersion, result)) {
      this->Close();
      return false;
    }

    // Write our library version number to special read-only offset
    // This is to assist diagnostics from FSUIPC logging
    // But only do this on first try
    if (i == 1 && !this->Write(0x330a, 2, &this->LibVersion, result)) {
      this->Close();
      return false;
    }

    // Actually send the requests and get the responses
    if (!this->Process(result)) {
      this->Close();
      return false;
    }

    // Maybe running on WideClient and need another try
    Sleep(100);
  }

  // Only allow running on FSUIPC 1.998e or later
  // with correct check pattern 0xFADE
  if (this->Version < 0x19980005 ||
      (this->FSVersion & 0xFFFF0000L) != 0xFADE0000) {
    *result = isWideFS ? Error::RUNNING : Error::VERSION;
    this->Close();
    return false;
  }

  this->FSVersion &= 0xffff;  // Isolsates the FS version number
  if (requestedVersion != Simulator::ANY &&
      static_cast<int>(requestedVersion) != this->FSVersion) {
    *result = Error::WRONGFS;
    this->Close();
    return false;
  }

  *result = Error::OK;
  return true;
}

void IPCUser::Close() {
  this->windowHandle = 0;
  this->msgId = 0;

  if (this->atom) {
    GlobalDeleteAtom(this->atom);
    this->atom = 0;
  }

  if (this->viewPointer) {
    UnmapViewOfFile((LPVOID)this->viewPointer);
    this->viewPointer = 0;
  }

  if (this->mapHandle) {
    CloseHandle(this->mapHandle);
    this->mapHandle = 0;
  }

  this->destinations = std::vector<void*>();
}

bool IPCUser::Process(Error* result) {
  DWORD_PTR error;
  DWORD* pdw;

  F64IPC_READSTATEDATA_HDR* readHeader;
  FS6IPC_WRITESTATEDATA_HDR* writeHeader;
  int i = 0;

  if (!this->viewPointer) {
    *result = Error::NOTOPEN;
    this->destinations.clear();
    return false;
  }

  if (this->viewPointer == this->nextPointer) {
    *result = Error::NODATA;
    this->destinations.clear();
    return false;
  }

  ZeroMemory(this->nextPointer, 4);  // Terminator
  this->nextPointer = this->viewPointer;

  // Send the request with 9 retries
  while (++i < 10 &&
         !SendMessageTimeout(
             this->windowHandle,  // FS6 window handle
             this->msgId,         // Our registered message id
             this->atom,          // wParam: name of file-mapping object
             0,           // lParam: offset of request into file-mapping object
             SMTO_BLOCK,  // Halt this thread until we get a response
             2000,        // Time-out interval
             &error       // Return value
             )) {
    Sleep(100);
  }

  if (i >= 10) {  // Failed all tries?
    DWORD lastError = GetLastError();
    if (lastError == 0) {
      *result = Error::TIMEOUT;
    } else if (lastError == 5) {
      /*
       * Error code 5 means that we don't have permission to communicate with
       * any window For more information, see:
       * https://docs.microsoft.com/en-us/windows/win32/debug/system-error-codes--0-499-
       */
      *result = Error::NOPERMISSION;
    } else {
      *result = Error::SENDMSG;
    }
    this->destinations.clear();
    return false;
  }

  if (error != FS6IPC_MESSAGE_SUCCESS) {
    *result = Error::DATA;  // FSUIPC didn't like something in the data
    this->destinations.clear();
    return false;
  }

  // Decode and store results of read requests
  pdw = (DWORD*)this->viewPointer;

  while (*pdw) {
    switch (*pdw) {
      case F64IPC_READSTATEDATA_ID: {
        readHeader = (F64IPC_READSTATEDATA_HDR*)pdw;
        this->nextPointer += sizeof(F64IPC_READSTATEDATA_HDR);
        void* dest = this->destinations.at(readHeader->pDest);
        if (dest && readHeader->nBytes) {
          CopyMemory(dest, this->nextPointer, readHeader->nBytes);
        }
        this->nextPointer += readHeader->nBytes;
        break;
      }
      case FS6IPC_WRITESTATEDATA_ID: {
        // This is a write, so there's no returned data to store
        writeHeader = (FS6IPC_WRITESTATEDATA_HDR*)pdw;
        this->nextPointer +=
            sizeof(FS6IPC_WRITESTATEDATA_HDR) + writeHeader->nBytes;
        break;
      }
      default: {
        // Error, so terminate the scan
        *pdw = 0;
        break;
      }
    }

    pdw = (DWORD*)this->nextPointer;
  }

  this->destinations.clear();

  this->nextPointer = this->viewPointer;
  *result = Error::OK;
  return true;
}

bool IPCUser::ReadCommon(bool special,
                         DWORD offset,
                         DWORD size,
                         void* dest,
                         Error* result) {
  F64IPC_READSTATEDATA_HDR* header =
      (F64IPC_READSTATEDATA_HDR*)this->nextPointer;

  if (!this->viewPointer) {
    *result = Error::NOTOPEN;
    return false;
  }

  if (this->nextPointer - this->viewPointer + size +
          sizeof(F64IPC_READSTATEDATA_HDR) >
      MAX_SIZE) {
    *result = Error::SIZE;
    return false;
  }

  header->dwId = F64IPC_READSTATEDATA_ID;
  header->dwOffset = offset;
  header->nBytes = size;
  header->pDest = this->destinations.size();

  this->destinations.push_back(dest);

  // Initialize the reception area, so rubbish won't be returned
  if (size) {
    if (special) {
      CopyMemory(&this->nextPointer[sizeof(F64IPC_READSTATEDATA_HDR)], dest,
                 size);
    } else {
      ZeroMemory(&this->nextPointer[sizeof(F64IPC_READSTATEDATA_HDR)], size);
    }
  }

  this->nextPointer += sizeof(F64IPC_READSTATEDATA_HDR) + size;

  *result = Error::OK;
  return true;
}

bool IPCUser::Write(DWORD offset, DWORD size, void* src, Error* result) {
  FS6IPC_WRITESTATEDATA_HDR* header =
      (FS6IPC_WRITESTATEDATA_HDR*)this->nextPointer;

  // Check link is open
  if (!this->viewPointer) {
    *result = Error::NOTOPEN;
    return false;
  }

  // Check whether we have enough space for this request (including terminator)
  if (this->nextPointer - this->viewPointer + 4 + size +
          sizeof(F64IPC_READSTATEDATA_HDR) >
      MAX_SIZE) {
    *result = Error::SIZE;
    return false;
  }

  // Initialize header for write request
  header->dwId = FS6IPC_WRITESTATEDATA_ID;
  header->dwOffset = offset;
  header->nBytes = size;

  // Copy in the data to be written
  if (size) {
    CopyMemory(&this->nextPointer[sizeof(FS6IPC_WRITESTATEDATA_HDR)], src,
               size);
  }

  // Update the pointer to be ready fore more data
  this->nextPointer += sizeof(FS6IPC_WRITESTATEDATA_HDR) + size;

  *result = Error::OK;
  return true;
}
}  // namespace FSUIPC
