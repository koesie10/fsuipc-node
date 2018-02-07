import fsuipc = require('..');

const obj = new fsuipc.FSUIPC();

obj.open()
    .then((obj) => {
      obj.add('clockHour', 0x238, fsuipc.Type.Byte);
      obj.add('aircraftType', 0x3D00, fsuipc.Type.String, 256);
      obj.add('lights', 0x0D0C, fsuipc.Type.BitArray, 2);

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
