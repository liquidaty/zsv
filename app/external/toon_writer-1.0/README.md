### toonwriter: small, fast and permissively-license TOON writer written in C

[![ci](https://github.com/liquidaty/toonwriter/actions/workflows/ci.yml/badge.svg)](https://github.com/liquidaty/toonwriter/actions/workflows/ci.yml)


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
we want to write our TOON to stdout, stderr, another file, a network pipe, or just
feed that back into some other process in a chain of data processing.

#### How to use

##### create a handle
Write either to a file, or provide your own `fwrite`-like function and target:

```

toonwriter_handle toonwriter_new_file(FILE *f, struct toonwriter_opts *opts);
toonwriter_handle toonwriter_new(
    size_t (*write)(const void *, size_t, size_t, void *),
    void *write_arg,
    struct toonwriter_opts *opts    
);
```

e.g. `toonwriter_handle h = toonwriter_new(fwrite, stdout, NULL)`

##### write your TOON
For example:
```
toonwriter_start_object(h);
toonwriter_object_key(h, "hello");
toonwriter_str(h, "there!");
toonwriter_end(h); // or alternatively, for more strict use, `toonwriter_end_object(h)`
```

or, in a more concise form:
```
toonwriter_start_object(h);
toonwriter_object_str(h, "hello", "there!"); // similar macros can be used for other types
toonwriter_end(h);

```

##### clean up

`toonwriter_delete(h)`

### Enjoy!
