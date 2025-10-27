### jsonwriter: small, fast and permissively-license JSON writer written in C


#### Why do we need this?

This library was designed to meet the following requirements:

* small
* fast
* memory-efficient
* supports full range of UTF8
* written in C, portable, and permissively licensed
* thread safe
* supports custom write functions and write targets (e.g. write to network or pipe
or to custom buffer using some custom write function)

The last item was the most difficult to find, but was indispensible for versatile
reusability (for example, maybe we will not know, until dynamically at runtime, whether
we want to write our JSON to stdout, stderr, another file, a network pipe, or just
feed that back into some other process in a chain of data processing.

#### How to use

##### create a handle
Write either to a file, or provide your own `fwrite`-like function and target:

```
jsonwriter_handle jsonwriter_new_file(FILE *f);
jsonwriter_handle jsonwriter_new(
    size_t (*write)(const void *, size_t, size_t, void *),
		void *write_arg
);
```

e.g. `jsonwriter_handle h = jsonwriter_new(fwrite, stdout)`

##### write your JSON
For example:
```
jsonwriter_start_object(h);
jsonwriter_object_key(h, "hello");
jsonwriter_str(h, "there!");
jsonwriter_end(h);
```

##### clean up

`jsonwriter_delete(h)`

### Enjoy!
