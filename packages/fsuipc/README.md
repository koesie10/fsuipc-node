# fsuipc
> Node bindings to FSUIPC for Windows x64.

[![NPM Version][npm-image]][npm-url]
[![Downloads Stats][npm-downloads]][npm-url]

## Installation

```sh
npm install --save fsuipc
```

Or

```sh
yarn add fsuipc
```

## Usage example

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

## Release History

This is only provided for historical reasons, for the newest releases see [GitHub releases](https://github.com/koesie10/fsuipc-node/releases).

* 0.4.1:
    * Limit number of published files
* 0.4.0:
    * Add support for requesting a connection to MSFS
    * Add support for writes
* 0.3.0
    * Add support for Node 12.0, drop support for Node < 11.0
* 0.2.0
    * Stability improvements
* 0.1.0
    * Initial release

<!-- Markdown link & img dfn's -->
[npm-image]: https://img.shields.io/npm/v/fsuipc.svg?style=flat-square
[npm-url]: https://npmjs.org/package/fsuipc
[npm-downloads]: https://img.shields.io/npm/dm/fsuipc.svg?style=flat-square
