function stream_file(file, chunk_size, cb) {
  if(!(chunk_size >= 1024))
    chunk_size = 1024;
  let offset = 0;
  let fr = new FileReader();

  function getNextChunk() {
    // getNextChunk(): will fire 'onload' event when specified chunk size is loaded
    let slice = file.slice(offset, offset + chunk_size);
    fr.readAsArrayBuffer(slice);
  }

  fr.onload = function() {
    let view = new Uint8Array(fr.result);
    if(!cb(null, view)) {
      offset += chunk_size;

      if(offset < file.size && view.length)
        getNextChunk();
      else // all done
        cb(null, null);
    }
  };

  fr.onerror = function() {
    cb('read error');
  };

  getNextChunk();
}
