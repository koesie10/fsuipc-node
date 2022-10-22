# `fsuipc-wasm`
> Node bindings to the FSUIPC WASM interface for Windows x64.

[![NPM Version][npm-image]][npm-url]
[![Downloads Stats][npm-downloads]][npm-url]

To use this package, you must be using Microsoft Flight Simulator and [the FSUIPC WASM module](https://forum.simflight.com/topic/92031-wasm-module-client-api-for-msfs-fsuipc7-now-available/) must be installed.

## Installation

```sh
npm install --save fsuipc-wasm
```

Or

```sh
yarn add fsuipc-wasm
```

## Usage example

```js
const fsuipcWasm = require('fsuipc-wasm');

const obj = new fsuipcWasm.FSUIPCWASM();

await obj.start();

console.log(obj.lvarValues);

obj.flagLvarForUpdate("A32NX_IS_STATIONARY");

obj.setLvarUpdateCallback((newLvars) => {
  console.log(newLvars);
});
```

<!-- Markdown link & img dfn's -->
[npm-image]: https://img.shields.io/npm/v/fsuipc-wasm.svg?style=flat-square
[npm-url]: https://npmjs.org/package/fsuipc-wasm
[npm-downloads]: https://img.shields.io/npm/dm/fsuipc-wasm.svg?style=flat-square
