  var _loaded = false;

  Module.onRuntimeInitialized = function() {
    _loaded = true;
    run_if_loaded();
  };

  var do_on_load = [];
  function run_if_loaded() {
    if(_loaded)
      while(do_on_load.length)
        do_on_load.shift()();
  }

  function run_on_load(f) {
    do_on_load.push(f);
    run_if_loaded();
  }

  function str2C(s) {
    var size = lengthBytesUTF8(s) + 1;
    var ret = _malloc(size);
    stringToUTF8Array(s, HEAP8, ret, size);
    return ret;
  }

  /**
   * To support multiple concurrent parsers without increasing the number of
   * allocated function pointers required, we will create a global list of all
   * active parsers which will be reset when the last active parser has completed.
   *
   * All parsers will share a constant pool of 2 row handlers (see below), each
   * of which will then call the relevant instance-specific row handler
   */
  let activeParsers = [];
  let activeParser_count = 0;

  /**
   * Register two global row handlers, one that will aggregate the entire row
   * data before calling the row handler, and another that will leave it to the
   * row handler to determine which cells to fetch
   */
  function globalRowHandlerNoData(ix) {
    let z = activeParsers[ix];
    z.rowHandler(null, z.ctx, z);
  }

  function globalRowHandlerWithData(ix) {
    let z = activeParsers[ix];
    let count = z.cellCount();
    let row = [];
    for(let i = 0; i < count; i++)
      row.push(z.getCell(i));
    z.rowHandler(row, z.ctx, z);
  }

  function globalReadFunc(buff, n, m, ix) {
    let z = activeParsers[ix];
    let sz = n * m;
    let jsbuff = new Uint8Array(buff, 0, sz);
    let bytes = fs.readSync(z.fd, jsbuff, 0, sz);
    z.bytesRead += bytes;
    if(bytes)
      writeArrayToMemory(jsbuff, buff);
    return bytes;
  }

  let globalReadFuncp;
  let globalRowHandlerNoDatap, globalRowHandlerWithDatap;
  run_on_load(function() {
    globalReadFuncp = addFunction(globalReadFunc, 'iiiii');
    globalRowHandlerNoDatap = addFunction(globalRowHandlerNoData, 'vi');
    globalRowHandlerWithDatap = addFunction(globalRowHandlerWithData, 'vi');
  });

  function setBuff(z, zsv, buffsize) {
    if(!buffsize)
      buffsize = 4096*16;
    if(!(buffsize >= 512))
      throw new Error('Invalid buffsize: ' + str(buffsize));
    if(z.heap)
      throw new Error('Buffer already allocated');
    z.heap = _malloc(buffsize);
    if(!z.heap)
      throw new Error('xOut of memory!');
    z.heapSize = buffsize;
    if(_zsv_set_buff(zsv, z.heap, buffsize))
      throw new Error('Unknown error!');
    return z.heap;
  }

  return {
    /**
     * create a new parser
     *
     * @param rowHandler callback with signature (row, ctx, parser)
     * @param ctx        a caller-defined value that will be passed to the row handler
     * @param options    if provided and options.rowData === false, row data will not be passed to the row handler
     */
    new: function(rowHandler, ctx, options) {
      let zsv = _zsv_new(null);
      options = options || {};
      if(zsv) {
        function cellCount() {
          return _zsv_cell_count(zsv);
        };

        function getCell(i) {
          let s = _zsv_get_cell_str(zsv, i);
          if(s)
            return UTF8ToString(s);
          /*
          let len = _zsv_get_cell_len(zsv, i);
          if(len > 0) {
            if(!(z.cellbuffsize >= len + 1)) {
              if(z.cellbuff)
                _free(z.cellbuff);
                z.cellbuff = _malloc(len + 1);
              z.cellbuffsize = len;
            }
            _zsv_copy_cell_str(zsv, i, z.cellbuff);
            return UTF8ToString(z.cellbuff);
          }*/
          return '';
        };

        let z = {
          rowHandler: rowHandler,
          cellCount: cellCount,
          getCell: getCell,
          buff: null,
          buffsize: 0,
          cellbuff: null,
          cellbuffsize: 0,
          ix: activeParsers.length,
          ctx: ctx,
          fd: 0,
          bytesRead: 0
        };

        let o = {
          getBytesRead: function() {
            return z.bytesRead;
          },
          setInputStream: function(fHandle) {
//            let buff = _zsv_get_buff(zsv);
//            let buffsize = _zsv_get_buffsize(zsv);
//            Uint8Array(Module.HEAP8.buffer, z.heap, size);
            z.fd = fHandle.fd;
            _zsv_set_read(zsv, globalReadFuncp);
            _zsv_set_input(zsv, z.ix);
          },
          parseMore: function() {
            return _zsv_parse_more(zsv);
          },
          finish: function() {
            return _zsv_finish(zsv);
          },
          abort: function() {
            return _zsv_abort(zsv);
          },
          delete: function() {
            if(z.buff)
              _free(z.buff);
            if(z.cellbuff)
              _free(z.cellbuff);
            activeParsers[z.ix] = null;
            activeParser_count--;
            if(activeParser_count == 0)
              activeParsers = [];
            return _zsv_delete(zsv);
          },
          cellCount: cellCount,
          getCell: getCell
        };

        activeParsers.push(z);
        activeParser_count++;
        _zsv_set_row_handler(zsv, options.rowData === false ? globalRowHandlerNoDatap : globalRowHandlerWithDatap);
        _zsv_set_context(zsv, z.ix);
        return o;
      }
    },
    runOnLoad: run_on_load
  };
})();
