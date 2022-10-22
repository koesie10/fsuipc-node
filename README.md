# fsuipc-node
> Node bindings to FSUIPC for Windows x64.

This repository contains two packages:
- [`fsuipc`](#fsuipc): Node bindings to FSUIPC for Windows x64.
- [`fsuipc-wasm`](#fsuipc-wasm): Node bindings to the FSUIPC WASM interface for Windows x64.

## [`fsuipc`](./packages/fsuipc)
> The `fsuipc` package can be used to read and write data from and to FSUIPC.

[![NPM Version][fsuipc-npm-image]][fsuipc-npm-url]
[![Downloads Stats][fsuipc-npm-downloads]][fsuipc-npm-url]

### Installation

```sh
npm install --save fsuipc
```

Or

```sh
yarn add fsuipc
```

### Usage example

```js
const fsuipc = require('fsuipc');

const obj = new fsuipc.FSUIPC();

obj.open()
    .then((obj) => {
      console.log(obj.add('clockHour', 0x238, fsuipc.Type.Byte));
      console.log(obj.add('aircraftType', 0x3D00, fsuipc.Type.String, 256));
      console.log(obj.add('lights', 0x0D0C, fsuipc.Type.BitArray, 2));

      return obj.process();
    })
    .then((result) => {
      console.log(JSON.stringify(result));

      return obj.close();
    })
    .catch((err) => {
      console.error(err);
      
      return obj.close();
    });

```

## [`fsuipc-wasm`](./packages/fsuipc-wasm)
> The `fsuipc-wasm` package can be used to read and write Lvars from and to FSUIPC using the [WASM interface](https://forum.simflight.com/topic/92031-wasm-module-client-api-for-msfs-fsuipc7-now-available/). Only for Microsoft Flight Simulator.

[![NPM Version][fsuipc-wasm-npm-image]][fsuipc-wasm-npm-url]
[![Downloads Stats][fsuipc-wasm-npm-downloads]][fsuipc-wasm-npm-url]

### Installation

```sh
npm install --save fsuipc-wasm
```

Or

```sh
yarn add fsuipc-wasm
```

### Usage example

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

## Meta

Distributed under the MIT license. See ``LICENSE`` for more information.

[https://github.com/koesie10/fsuipc-node](https://github.com/koesie10/fsuipc-node)

## Contributing

1. Fork it (<https://github.com/koesie10/fsuipc-node/fork>)
2. Create your feature branch (`git checkout -b feature/fooBar`)
3. Commit your changes (`git commit -am 'Add some fooBar'`)
4. Push to the branch (`git push origin feature/fooBar`)
5. Create a new Pull Request

## Publishing a new version

1. Run `yarn lerna version` to bump the version of all packages
2. Create a release on GitHub with the new version number
3. GitHub Actions will automatically publish the new version to NPM

<!-- Markdown link & img dfn's -->
[fsuipc-npm-image]: https://img.shields.io/npm/v/fsuipc.svg?style=flat-square
[fsuipc-npm-url]: https://npmjs.org/package/fsuipc
[fsuipc-npm-downloads]: https://img.shields.io/npm/dm/fsuipc.svg?style=flat-square
[fsuipc-wasm-npm-image]: https://img.shields.io/npm/v/fsuipc-wasm.svg?style=flat-square
[fsuipc-wasm-npm-url]: https://npmjs.org/package/fsuipc-wasm
[fsuipc-wasm-npm-downloads]: https://img.shields.io/npm/dm/fsuipc-wasm.svg?style=flat-square
