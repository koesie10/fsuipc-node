// Type definitions for fsuipc-wasm

export interface InstanceOptions {
  // If set, will print debug logs to the console
  debug?: boolean;
}

export class FSUIPCWASM {
  constructor(options?: InstanceOptions);

  start(): Promise<FSUIPCWASM>;
  close(): Promise<FSUIPCWASM>;

  get lvarValues(): Record<string, number>;
}

export class FSUIPCWASMError extends Error {
  constructor(message: string);
}
