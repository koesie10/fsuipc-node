const fsuipcWasm = require('..');

const obj = new fsuipcWasm.FSUIPCWASM({
  debug: true,
});

const sleep = (ms) => new Promise((resolve) => {
  setTimeout(resolve, ms);
});

async function test() {
  await obj.start();

  console.log('Started');

  await sleep(100);

  console.log(obj.lvarValues);

  await obj.close();

  console.log('closed');
}

test().catch(e => console.error(e));
