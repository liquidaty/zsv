  var _loaded = false;

  Module.onRuntimeInitialized = function() {
    _loaded = true;
    run_if_loaded();
  };

  var do_on_load = [];
  function run_if_loaded() {
    if(_loaded)
      while(do_on_load.length)
        do_on_load.pop()();
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

  return {
    new: function(row_handler, ctx) {
      let h = _zsv_new(null);
      if(h) {
        let z = {
          zsv: h,
          row_handler: null,
          buff: null,
          buffsize: 0,
          cellbuff: null,
          cellbuffsize: 0
        };

        let this_row_handler = function(_) {
          row_handler(ctx);
        };
        // row_handlerp: void (*row_handler)(void *ctx);
        if(!(z.row_handler = addFunction(this_row_handler, 'vi')))
          console.log('Error! Unable to allocate row handler');
        else
          _zsv_set_row_handler(h, z.row_handler);
        return z;
      }
    },
    parseBytes: function(z, byte_array) {
      let len = byte_array.length;
      if(len) {
        // copy bytes into a chunk of memory that our library can access
        if(!(z.buffsize >= len)) {
          if(z.buff)
            _free(z.buff);
          z.buff = _malloc(len);
          z.buffsize = len;
        }
        // copy to memory that wasm can access, then parse
        writeArrayToMemory(byte_array, z.buff);
        return _zsv_parse_bytes(z.zsv, z.buff, len);
      }
    },
    cellCount: function(z) {
      return _zsv_cell_count(z.zsv);
    },
    getCell: function(z, i) {
      let len = _zsv_get_cell_len(z.zsv, i);
      if(len > 0) {
        if(!(z.cellbuffsize >= len + 1)) {
          if(z.cellbuff)
            _free(z.cellbuff);
          z.cellbuff = _malloc(len + 1);
          z.cellbuffsize = len;
        }
        _zsv_copy_cell_str(z.zsv, i, z.cellbuff);
        return UTF8ToString(z.cellbuff);
      }
      return '';
    },
    abort: function(z) {
      return _zsv_abort(z.zsv);
    },
    finish: function(z) {
      return _zsv_finish(z.zsv);
    },
    delete: function(z) {
      if(z.row_handler)
        removeFunction(z.row_handler);
      if(z.buff)
        _free(z.buff);
      if(z.cellbuff)
        _free(z.cellbuff);
      return _zsv_delete(z.zsv);
    },
    runOnLoad: run_on_load
  };
})();
