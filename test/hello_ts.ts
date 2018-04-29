import fsuipc = require('..');

const obj = new fsuipc.FSUIPC();

let intervalId: any;

setTimeout(() => null, 10);
setTimeout(stopRecording, 5000);
setTimeout(startRecording, 6000);
setTimeout(stopRecording, 10000);
setTimeout(startRecording, 12000);

startRecording();

function startRecording() {
  console.log('Starting recording...');
  obj.open()
    .then(() => {
      console.log('Link is open');

      obj.add('clockHour', 0x238, fsuipc.Type.Byte);
      obj.add('aircraftType', 0x3D00, fsuipc.Type.String, 256);
      obj.add('lights', 0x0D0C, fsuipc.Type.BitArray, 2);
      intervalId = setInterval(() => {
        obj.process()
          .then(value => {
            console.log('Got result: ', JSON.stringify(value));
          })
          .catch(err => {
            console.error(err);

            clearInterval(intervalId);
            return obj.close();
          });
      }, 1000);

      console.log('Recording has started');
    })
    .catch(err => {
      console.error(err);

      console.log('Recording has stopped');

      return obj.close();
    });
}

function stopRecording() {
  console.log('Stopping recording...');
  clearInterval(intervalId);
  obj.close().catch(err => console.error(err));
}
