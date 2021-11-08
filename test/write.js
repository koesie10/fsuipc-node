const fsuipc = require('..');

const obj = new fsuipc.FSUIPC();

obj.open()
    .then((obj) => {
      obj.write(0x0BC8, fsuipc.Type.UInt32, 32767);

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
