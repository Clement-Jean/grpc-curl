# grpc-curl

MVP using libcurl to test gRPC endpoints.

## Build

With `ninja` as generator:

```
mkdir build && cd build
cmake -DPROTOBUF_FETCHCONTENT=true -GNinja ..
ninja
```

## Usage

With a server running on `localhost` and port 50051, and defining a unary RPC called `GreetOnce` in package `greet` and service `GreetService`:

`./grpc-curl "http://localhost:50051/" greet.GreetService/GreetOnce`

Then, we need to use some options depending on the type of RPC:

- `--data`/`-d` `json` to set the request data (unary/server streaming)

Finally, some options are needed depending on the features enabled/disabled in the server:

- `--proto_path`/`-I` `path` to set the path in which the imported proto files can be found in
- `--proto`/`-p` `path` to set the proto file(s) in which the request/response/service are defined (when reflection is not enabled)
- `--rpc_header`/`-h` `key:val` to set headers
- `--ca_cert`/`-c` `path` to set the ca cert used in TLS communication

### Client Streaming / Bidirectional streaming

When running in client streaming or bidi streaming, the CLI will ask for input on STDIN. Each line is the equivalent of the input of `--data`. And the the input can be stopped with CTRL+d. For example, with a client streaming RPC called `GreetJointly`:

```
./grpc-curl "http://localhost:50051/" greet.GreetService/GreetJointly`
{"name": "clement", "separator": " and "}
{"name": "daniel"}
[CTRL+D]
greeting: "hello clement and daniel"
```

## Missing Features

- processing of big messages
- `-d @` to read the data on STDIN (with heredocs or pipes)
- reflection
- data compression
