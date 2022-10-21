const fsuipcWasm = require('..');

const obj = new fsuipcWasm.FSUIPCWASM({
  debug: true,
});

obj.start()
    .then((obj) => {
      console.log('started');
      return obj;
    })
    .then((obj) => {
      return obj.close();
    })
    .catch((err) => {
      console.error(err);

      return obj.close();
    })
    .then((obj) => {
      console.log('closed');
    });
