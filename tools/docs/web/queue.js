// network-web-queue: two game files in the upload queue with "Install directly" ticked (nothing is sent).
(() => {
  const mk = (name, mb) => new File([new Uint8Array(1)], name, { type: 'application/octet-stream' });
  const files = [mk('Goose Delivery Service [0100DE0000110000][v0].nsp'), mk('Bread Knight [0100DE0000120000][v0].nsz')];
  addFilesToUploadQueue(files);
  transferQueue[0].size = 2147483648; transferQueue[1].size = 3435973837;
  renderQueue();
  return transferQueue.length;
})()
