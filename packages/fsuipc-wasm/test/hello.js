const fsuipcWasm = require('..');

const obj = new fsuipcWasm.FSUIPCWASM({
  // logLevel: fsuipcWasm.LogLevel.Enable,
});

async function test() {
  await obj.start();

  console.log('Started');

  console.log(obj.lvarValues);

  obj.flagLvarForUpdate("A32NX_IS_STATIONARY");

  obj.setLvarUpdateCallback((newLvars) => {
    console.log(newLvars);
  });

  // await obj.close();
}

test().catch(e => console.error(e));
