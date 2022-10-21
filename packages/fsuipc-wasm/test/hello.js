const fsuipcWasm = require('..');

const obj = new fsuipcWasm.FSUIPCWASM({
  logLevel: fsuipcWasm.LogLevel.Enable,
});

async function test() {
  await obj.start();

  console.log('Started');

  console.log(obj.lvarValues);

  await obj.close();

  console.log('closed');
}

test().catch(e => console.error(e));
