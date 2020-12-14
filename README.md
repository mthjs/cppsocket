# cppsocket

> Go-esque way of listening for and handling of connections.

## Why?

Go's way of handling connections is pretty nice. It felt like a nice thing to
implement a similar, albeit limited, way of handling sockets in C++11.

## Getting Started

The easiest way to get going is by using the provided docker-container by
running the following commands from the root of this project:

```bash
$ docker build -t cppsocket ./container
$ docker run --rm -it -v `pwd`:/opt cppsocket bash
```

> If you don't have docker, or aren't keen on installing docker, you can also
> do the next steps without (although I haven't tested any other environment
> than the provided docker-container).
> Granted, you'll have to have the following installed:
> * cmake
> * make
> * gcc

Once in an environment that provides us with everything required by this
project, building is as easy as:

```bash
$ mkdir -p build
$ cd build
$ cmake ..
$ make -j6 cppsocket
```

### Running Tests

Before you can run the tests, you'll need to initialize the `git submodules`.
Which is as quite easy. At the root of this project, run the following:

```bash
$ git submodule update --init --recursive
```

After which, we should have the wonderful test-framework [Catch2]
available. Presuming we're still at the root of this project, we'll do the
following to build and run the tests:

```bash
$ mkdir -p build
$ cd build
$ cmake ..
$ make -j6 tests; ./tests/test
```

[Catch2]: https://github.com/catchorg/Catch2
