const fsuipc = require('..');

const obj = new fsuipc.FSUIPC();

obj.open()
    .then((obj) => {
      console.log(obj.add('clockHour', 0x238, fsuipc.Type.Byte));
      console.log(obj.add('aircraftType', 0x3D00, fsuipc.Type.String, 256));
      console.log(obj.add('lights', 0x0D0C, fsuipc.Type.BitArray, 2));

      console.log(obj.remove('lights'));

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
