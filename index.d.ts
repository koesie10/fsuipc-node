// Type definitions for fsuipc

export const enum Type {
  Byte = 0,
  SByte = 1,
  Int16 = 2,
  Int32 = 3,
  Int64 = 4,
  UInt16 = 5,
  UInt32 = 6,
  UInt64 = 7,
  Double = 8,
  Single = 9,
  ByteArray = 10,
  String = 11,
  BitArray = 12,
}

interface Offset {
  name: string;
  offset: number;
  type: Type;
  length: number;
  test: Type.Byte;
}

export enum Simulator {
  ANY,
  FS98,
  FS2K,
  CFS2,
  CFS1,
  FLY,
  FS2K2,
  FS2K4,
  FSX,
  ESP,
  P3D,
  FSX64,
  P3D64,
  MSFS,
}

export class FSUIPC {
  constructor();

  open(requestedSimulator?: Simulator): Promise<FSUIPC>;
  close(): Promise<FSUIPC>;
  process(): Promise<object>;

  add(name: string, offset: number,
      type: Type.Byte|Type.SByte|Type.Int16|Type.Int32|Type.Int64|
      Type.UInt16|Type.UInt32|Type.UInt64|Type.Double|Type.Single): Offset;
  add(name: string, offset: number,
      type: Type.ByteArray|Type.String|Type.BitArray, length: number): Offset;

  remove(name: string): Offset;
}

export enum ErrorCode {
  OK,
  // Attempt to Open when already Open
  OPEN,
  // Cannot link to FSUIPC or WideClient
  NOFS,
  // Failed to Register common message with Windows
  REGMSG,
  // Failed to create Atom for mapping filename
  ATOM,
  // Failed to create a file mapping object
  MAP,
  // Failed to open a view to the file map
  VIEW,
  // Incorrect version of FSUIPC, or not FSUIPC
  VERSION,
  // Sim is not version requested
  WRONGFS,
  // Call cannot execute, link not Open
  NOTOPEN,
  // Call cannot execute: no requests accumulated
  NODATA,
  // IPC timed out all retries
  TIMEOUT,
  // IPC sendmessage failed all retries
  SENDMSG,
  // IPC request contains bad data
  DATA,
  // Maybe running on WideClient, but FS not running on Server, or wrong FSUIPC
  RUNNING,
  // Read or Write request cannot be added, memory for Process is full
  SIZE
}

export class FSUIPCError extends Error {
  constructor(message: string, code: ErrorCode);

  code: ErrorCode;
}
