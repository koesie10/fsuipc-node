// Type definitions for fsuipc-wasm

export enum LogLevel {
  Disable = 0,
  Info = 1,
  Buffer = 2,
  Debug = 3,
  Trace = 4,
  Enable = 5,
}

export interface InstanceOptions {
  // If set, will print logs to the console with the specified log level
  logLevel?: LogLevel;
}

export class FSUIPCWASM {
  constructor(options?: InstanceOptions);

  start(): Promise<FSUIPCWASM>;
  close(): Promise<FSUIPCWASM>;

  get lvarValues(): Record<string, number>;

  setLvarUpdateCallback(callback: (updatedLvars: Record<string, number>) => void): Promise<void>;
  flagLvarForUpdate(lvarName: string): Promise<void>;
}

export class FSUIPCWASMError extends Error {
  constructor(message: string);
}
