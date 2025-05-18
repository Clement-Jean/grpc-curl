#include <cassert>
#include <cstddef>
#include <cstring>
#include <stdlib.h>
#include <fcntl.h>
#include <getopt.h>
#include <utility>
#include <string>
#include <memory>
#include <queue>
#include <iostream>

#include <curl/curl.h>

#include "proto/reflection.pb.h"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/tokenizer.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/text_format.h>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/compiler/importer.h>
#include <google/protobuf/struct.pb.h>

class ErrorPrinter : public google::protobuf::io::ErrorCollector,
                     public google::protobuf::compiler::MultiFileErrorCollector
{
  void RecordError(int line, int column, absl::string_view message) override {
    std::cerr << line << ":" << column << ": " << message << std::endl;
  }

  void RecordError(absl::string_view filename, int line, int column,
		   absl::string_view message) override {
    std::cerr << filename << ":" << line << ":" << column << ": " << message << std::endl;
  }
};

struct Context {
  int verbose_flag;
  std::vector<std::string> args;
  std::vector<std::string> headers;
  std::vector<std::string> protos;
  std::vector<std::string> paths;
  std::string ca_cert;
  std::string data;
};

class RpcHandler {
private:
  enum RPC_TYPE {
    UNARY,
    SERVER_STREAMING,
    CLIENT_STREAMING,
    BIDI_STREAMING
  };

private:
  CURL* _handle;
  RPC_TYPE _type;
  const google::protobuf::Descriptor *_input_type;
  const google::protobuf::Descriptor *_output_type;
  google::protobuf::DynamicMessageFactory &_factory;
  const google::protobuf::DescriptorPool *_pool;
  std::queue<std::unique_ptr<google::protobuf::Message>> _request_queue;

public:
  RpcHandler(CURL *handle,
             const google::protobuf::ServiceDescriptor *service_descriptor,
             const google::protobuf::MethodDescriptor *method_descriptor,
             google::protobuf::DynamicMessageFactory &dynamic_factory,
             const google::protobuf::DescriptorPool *pool) : _handle(handle), _factory(dynamic_factory), _pool(pool) {
    bool server_streaming = method_descriptor->server_streaming();
    bool client_streaming = method_descriptor->client_streaming();

    if (server_streaming) {
      _type = client_streaming ? BIDI_STREAMING : SERVER_STREAMING;
    } else {
      _type = client_streaming ? CLIENT_STREAMING : UNARY;
    }

    _input_type = method_descriptor->input_type();
    _output_type = method_descriptor->output_type();
  }

  auto handle(const Context& ctx) -> int {
    if (_type == UNARY || _type == SERVER_STREAMING) {
      if (ctx.data.size() == 0) {
	std::cerr << "provide data please" << std::endl;
	return 1;
      }

      auto type = _pool->FindMessageTypeByName(_input_type->full_name());
      std::unique_ptr<google::protobuf::Message> req_message(_factory.GetPrototype(type)->New());
      type = _pool->FindMessageTypeByName(_output_type->full_name());
      std::unique_ptr<google::protobuf::Message> res_message(_factory.GetPrototype(type)->New());

      curl_easy_setopt(_handle, CURLOPT_READDATA, (void *)&_request_queue);
      curl_easy_setopt(_handle, CURLOPT_WRITEDATA, (void *)res_message.get());

      auto json_opts = google::protobuf::util::JsonParseOptions { .ignore_unknown_fields = true };
      auto encode_req = google::protobuf::util::JsonStringToMessage(ctx.data, req_message.get(), json_opts);
      if (encode_req != absl::OkStatus()) {
	std::cerr << "failed to parse data" << std::endl;
	return 1;
      }

      _request_queue.push(std::move(req_message));
      auto res = curl_easy_perform(_handle);

      if (res != CURLE_OK) {
        std::cerr << "error: " << curl_easy_strerror(res) << std::endl;
	return 1;
      }

      // TODO check status + message

    } else if (_type == CLIENT_STREAMING) {
      // TODO if data == @ -> pipe (does this work on windows)
      //      else getline loop
      std::string line = "";

      auto type = _pool->FindMessageTypeByName(_output_type->full_name());
      std::unique_ptr<google::protobuf::Message> res_message(_factory.GetPrototype(type)->New());
      curl_easy_setopt(_handle, CURLOPT_READDATA, (void *)&_request_queue);
      curl_easy_setopt(_handle, CURLOPT_WRITEDATA, (void *)res_message.get());

      type = _pool->FindMessageTypeByName(_input_type->full_name());
      while (std::getline(std::cin, line)) {
	std::unique_ptr<google::protobuf::Message> req_message(_factory.GetPrototype(type)->New());

	auto json_opts = google::protobuf::util::JsonParseOptions { .ignore_unknown_fields = true };
	auto encode_req = google::protobuf::util::JsonStringToMessage(line, req_message.get(), json_opts);
        if (encode_req != absl::OkStatus()) {
          std::cerr << "failed to parse data: " << encode_req << std::endl;
	  return 1;
        }

	_request_queue.push(std::move(req_message));
      }

      auto res = curl_easy_perform(_handle);

      if (res != CURLE_OK) {
        std::cerr << "error: " << curl_easy_strerror(res) << std::endl;
	return 1;
      }

      // TODO check status + message

    } else {
      std::string line = "";

      auto req_type = _pool->FindMessageTypeByName(_input_type->full_name());
      auto res_type = _pool->FindMessageTypeByName(_output_type->full_name());

      curl_easy_setopt(_handle, CURLOPT_READDATA, (void *)&_request_queue);

      while (std::getline(std::cin, line)) {
	std::unique_ptr<google::protobuf::Message> req_message(_factory.GetPrototype(req_type)->New());
        std::unique_ptr<google::protobuf::Message> res_message(_factory.GetPrototype(res_type)->New());

	curl_easy_setopt(_handle, CURLOPT_WRITEDATA, (void *)res_message.get());

	auto json_opts = google::protobuf::util::JsonParseOptions { .ignore_unknown_fields = true };
	auto encode_req = google::protobuf::util::JsonStringToMessage(line, req_message.get(), json_opts);
        if (encode_req != absl::OkStatus()) {
          std::cerr << "failed to parse data: " << encode_req << std::endl;
	  return 1;
        }

        _request_queue.push(std::move(req_message));
	auto res = curl_easy_perform(_handle);

	if (res != CURLE_OK) {
	  std::cerr << "error: " << curl_easy_strerror(res) << std::endl;
	  return 1;
        }

	// TODO check status + message
      }
    }

    return 0;
  }
};

// TODO what happens for big messages
static size_t write_callback(void *contents, size_t size, size_t nmemb,
                             void *userdata) {
  size_t realsize = size * nmemb;
  auto res_message = (google::protobuf::Message *)userdata;

  if (realsize > 5) {
    auto in = google::protobuf::io::ArrayInputStream(contents, realsize);

    in.Skip(5); // skip gRPC frame metadata
    if (!res_message->ParsePartialFromZeroCopyStream(&in)) {
      std::cerr << "failed to parse server streaming response" << std::endl;
      return 0;
    }

    std::string out;
    auto opts = google::protobuf::util::JsonPrintOptions { .add_whitespace = true };
    auto encode_req = google::protobuf::util::MessageToJsonString(*res_message, &out, opts);
    if (encode_req != absl::OkStatus()) {
      std::cerr << "failed to encode to JSON" << std::endl;
      return 0;
    }

    std::cout << out << std::endl;
    //auto out = google::protobuf::io::FileOutputStream(0); // STDOUT
    //google::protobuf::TextFormat::Print(*res_message, &out); // or JSON
  } else {
    return 0;
  }

  return realsize;
}

static size_t reflection_write_callback(void *contents, size_t size, size_t nmemb,
					void *userdata) {
  size_t realsize = size * nmemb;
  auto res_message = (google::protobuf::Message *)userdata;

  if (realsize > 5) {
    auto in = google::protobuf::io::ArrayInputStream(contents, realsize);

    in.Skip(5); // skip gRPC frame metadata
    if (!res_message->ParsePartialFromZeroCopyStream(&in)) {
      std::cerr << "failed to parse server streaming response" << std::endl;
      return 0;
    }
  } else {
    return 0;
  }

  return realsize;
}

static size_t read_callback(char *ptr, size_t size, size_t nmemb,
                            void *userdata) {
  auto request_queue = (std::queue<std::unique_ptr<google::protobuf::Message>> *)userdata;

  if (request_queue == nullptr || request_queue->empty()) {
    return 0;
  }

  auto req_message = std::move(request_queue->front());

  request_queue->pop();

  auto data_size = req_message->ByteSizeLong();
  size_t required_size = data_size + 5;
  size_t realsize = size * nmemb;

  if (realsize < required_size) {
    std::cerr << "buffer too small for message with gRPC framing. required: "
              << required_size << ", available: " << realsize << std::endl;
    return CURL_READFUNC_ABORT;
  }

  auto data = new uint8_t[data_size];
  req_message->SerializeToArray(data, data_size);

  ptr[0] = 0; // TODO compression
  // TODO depending on endianess!
  ptr[1] = data_size & 0xFF000000;
  ptr[2] = data_size & 0x00FF0000;
  ptr[3] = data_size & 0x0000FF00;
  ptr[4] = data_size & 0x000000FF;
  memcpy(ptr + 5, data, data_size);
  delete[] data;
  return required_size;
}

// TODO max message: 4194304

auto parse_options(int argc, char **argv, Context *ctx) {
  static struct option long_options[] = {
    {"verbose", no_argument, &ctx->verbose_flag, 'v'},
    {"rpc_header", required_argument, 0, 'h'},
    {"data", required_argument, 0, 'd'},
    {"proto", required_argument, 0, 'p'},
    {"proto_path", required_argument, 0, 'I'},
    {"ca_cert", required_argument, 0, 'c'},
    {0, 0, 0, 0}
  };

  int c = 0;
  int option_index = 0;

  while ((c = getopt_long(argc, argv, "vh:d:p:I:c:", long_options, &option_index)) != -1) {
    switch (c) {
    case 0: { // long option
      if (long_options[option_index].flag != 0) {
        break;
      }

      if (optarg) {
	ctx->args.push_back(optarg);
      }
      break;
    }
    case 'v': {
      ctx->verbose_flag = 1;
      break;
    }
    case 'h': {
      ctx->headers.push_back(optarg);
      break;
    }
    case 'd': {
      ctx->data = optarg;
      break;
    }
    case 'p': {
      ctx->protos.push_back(optarg);
      break;
    }
    case 'I': {
      ctx->paths.push_back(optarg);
      break;
    }
    case 'c': {
      ctx->ca_cert = optarg;
      break;
    }
    case '?': {
      std::cerr << "got unknown option." << std::endl;
      break;
    }
    default: {
      std::cerr << "got unknown parse returns: " << c << std::endl;
      break;
    }
    }
  }

  for (int i = optind; i < argc; i++) {
    ctx->args.push_back(argv[i]);
  }
}

auto check_options(const Context &ctx) -> bool {
  if (ctx.args.size() != 2) {
    std::cerr << "usage: try './curl [url] [IP]' to make a get request" << std::endl;
    return false;
  }

  if (!ctx.args[0].contains("/")) {
    std::cerr << "the url should have format: (package.)service/rpc" << std::endl;
    return false;
  }

  return true;
}

auto get_rpc_info(const google::protobuf::DescriptorPool *pool,
                  const std::string &rpc)
    -> std::pair<const google::protobuf::ServiceDescriptor *,
                 const google::protobuf::MethodDescriptor *> {
  auto it = std::find(rpc.begin(), rpc.end(), '/');
  assert(it != rpc.end());
  auto sep_pos = std::distance(rpc.begin(), it);
  auto svc = rpc.substr(0, sep_pos);
  auto method = rpc.substr(sep_pos + 1, std::distance(it, rpc.end()));
  auto svc_desc = pool->FindServiceByName(svc);
  auto rpc_desc = svc_desc->FindMethodByName(method);

  return std::make_pair(svc_desc, rpc_desc);
}

bool is_installed_proto_path(const std::string& path) {
  std::string file_path = path + "/google/protobuf/descriptor.proto";
  return std::filesystem::exists(file_path);
}

// TODO what happens when you have multiple protocs?
auto add_default_proto_paths(google::protobuf::compiler::DiskSourceTree& source_tree) {
  const char *path_env = std::getenv("PATH");

  if (path_env) {
    std::string dir;
    std::istringstream tokenStream(path_env);
    char delimiter =
#ifdef _WIN32
                                                       ';'
#else
                                                       ':'
#endif
    ;

    while (std::getline(tokenStream, dir, delimiter)) {
      std::filesystem::path fs_path = std::filesystem::path(dir) /
#ifdef _WIN32
                          "protoc.exe"
#else
                          "protoc"
#endif
          ;

      if (!std::filesystem::exists(fs_path)) {
        continue;
      }

      std::string path = fs_path.string();
      std::size_t pos = path.find_last_of("/\\");
      if (pos == path.npos || pos == 0) {
	continue;
      }

      path = path.substr(0, pos);

      if (is_installed_proto_path(path)) {
        source_tree.MapPath("", path.c_str());
	continue;
      }

      auto include_path = path + "/include";
      if (is_installed_proto_path(include_path)) {
        source_tree.MapPath("", include_path.c_str());
	continue;
      }

      pos = path.find_last_of("/\\");
      if (pos == std::string::npos || pos == 0) {
	return;
      }

      path = path.substr(0, pos);
      include_path = path + "/include";
      if (is_installed_proto_path(include_path)) {
        source_tree.MapPath("", include_path.c_str());
	continue;
      }
    }
  }
}

auto is_reflection_enabled(CURL *curl_handle, const Context &ctx,
                           const std::filesystem::path &url,
                           const std::string &rpc)
    -> std::unique_ptr<google::protobuf::DescriptorPool> {
  assert(curl_handle != nullptr);

  size_t svc_end = rpc.find_last_of('/');
  if (svc_end == std::string::npos || svc_end == 0) {
    std::cerr << "invalid rpc provided to is_reflection_enabled" << std::endl;
    return nullptr;
  }

  auto req_message = std::make_unique<grpc::reflection::v1::ServerReflectionRequest>();
  req_message->set_file_containing_symbol(rpc.substr(0, svc_end));

  std::queue<std::unique_ptr<grpc::reflection::v1::ServerReflectionRequest>> req_queue;
  grpc::reflection::v1::ServerReflectionResponse res_message;
  auto full_url = url / "grpc.reflection.v1.ServerReflection/ServerReflectionInfo";

  curl_easy_setopt(curl_handle, CURLOPT_URL, full_url.c_str());
  curl_easy_setopt(curl_handle, CURLOPT_READDATA, (void *)&req_queue);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, reflection_write_callback);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&res_message);

  req_queue.push(std::move(req_message));
  auto res = curl_easy_perform(curl_handle);
  struct curl_header *type;
  CURLHcode h = curl_easy_header(curl_handle, "grpc-status", 0, CURLH_HEADER, -1, &type);

  if (res != CURLE_OK) {
    return nullptr;
  }

  if (h != CURLHE_MISSING) {
    if (type != nullptr && type->amount != 0 && type->value != nullptr && std::string(type->value) != "0") {
      h = curl_easy_header(curl_handle, "grpc-message", 0, CURLH_HEADER,
			   -1, &type);
      return nullptr;
    }
  }

  auto pool = std::make_unique<google::protobuf::DescriptorPool>();
  auto fdr = res_message.file_descriptor_response();

  // FIX based on current (naive) tests the file descriptors are in reverse
  //     order. Meaning that if a.proto depends on b.proto, this will be
  //     [a.proto, b.proto] and not [b.proto, a.proto]
  for (auto i = fdr.file_descriptor_proto_size() - 1; i >= 0; i--) {
    auto fd_bytes = fdr.file_descriptor_proto(i);
    google::protobuf::io::ArrayInputStream raw_input(fd_bytes.data(), fd_bytes.size());
    google::protobuf::io::CodedInputStream coded_input(&raw_input);

    google::protobuf::FileDescriptorProto fd;
    if (fd.ParseFromCodedStream(&coded_input)) {
      pool->BuildFile(fd);
    }
  }

  return pool;
}

// we return the importer because it returns a reference to its private field pool_...
auto get_pool(CURL *curl_handle, const Context &ctx, const std::filesystem::path& url, const std::string& rpc)
    -> std::pair<std::unique_ptr<google::protobuf::DescriptorPool>,
                 std::unique_ptr<google::protobuf::compiler::Importer>> {

  // TODO maybe we should avoid checking reflection if the --proto is passed?
  std::unique_ptr<google::protobuf::DescriptorPool> reflection_pool = is_reflection_enabled(curl_handle, ctx, url, rpc);

  if (reflection_pool == nullptr) {
    if (ctx.protos.size() == 0) { // we need a proto file for the definitions
	std::cerr << "provide proto files please" << std::endl;
	return std::make_pair(nullptr, nullptr);
      }

      ErrorPrinter ec;
      google::protobuf::compiler::DiskSourceTree source_tree;

      source_tree.MapPath("", ".");
      add_default_proto_paths(source_tree);
      for (const auto& path : ctx.paths) {
	source_tree.MapPath("", path);
      }

      auto importer = std::make_unique<google::protobuf::compiler::Importer>(&source_tree, &ec);
      for (const auto& proto : ctx.protos) {
	const google::protobuf::FileDescriptor* desc = importer->Import(proto);

	if (desc == nullptr) {
	  std::cerr << "failed to import proto file: " << proto << std::endl;
	  return std::make_pair(nullptr, nullptr);
	}
      }

      return std::make_pair(std::make_unique<google::protobuf::DescriptorPool>(importer->pool()), std::move(importer));
  } else {
    return std::make_pair(std::move(reflection_pool), nullptr);
  }
}

auto main(int argc, char **argv) -> int {
  auto ctx = Context { .verbose_flag = 0 };
  ctx.args.reserve(2);

  parse_options(argc, argv, &ctx);
  if (!check_options(ctx)) {
    return 1;
  }

  if (ctx.verbose_flag) {
    printf("Version: %s\n", curl_version());
  }

  CURLcode res;
  struct curl_slist *list = NULL;
  int ret_val = 0;

  auto curl_handle = std::unique_ptr<CURL, void(*)(CURL*)>(curl_easy_init(), (void (*)(CURL *))curl_easy_cleanup);
  if (curl_handle) {
    std::filesystem::path url = ctx.args[0];
    std::filesystem::path rpc = ctx.args[1];
    curl_version_info_data *curl_info = curl_version_info(CURLVERSION_NOW);
    std::string user_agent = std::string("libcurl/") + curl_info->version;

    curl_easy_setopt(curl_handle.get(), CURLOPT_VERBOSE, ctx.verbose_flag);
    curl_easy_setopt(curl_handle.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE);
    curl_easy_setopt(curl_handle.get(), CURLOPT_USERAGENT, user_agent.c_str());
    curl_easy_setopt(curl_handle.get(), CURLOPT_POST, 1L);
    curl_easy_setopt(curl_handle.get(), CURLOPT_READFUNCTION, read_callback);

    if (ctx.ca_cert.size() != 0) {
      curl_easy_setopt(curl_handle.get(), CURLOPT_SSLVERSION, CURL_SSLVERSION_MAX_DEFAULT);
      curl_easy_setopt(curl_handle.get(), CURLOPT_SSL_VERIFYPEER, 1L);
      curl_easy_setopt(curl_handle.get(), CURLOPT_SSL_VERIFYHOST, 2L);
      curl_easy_setopt(curl_handle.get(), CURLOPT_CAINFO, ctx.ca_cert.c_str());
    }

    list = curl_slist_append(list, "Content-Type: application/grpc+proto");
    for (const auto& header : ctx.headers) {
      list = curl_slist_append(list, header.c_str());
    }
    curl_easy_setopt(curl_handle.get(), CURLOPT_HTTPHEADER, list);

    auto [pool, _] = get_pool(curl_handle.get(), ctx, url, rpc);
    if (pool == nullptr) {
      return 1;
    }

    auto [svc_desc, method_desc] = get_rpc_info(pool.get(), rpc);
    google::protobuf::DynamicMessageFactory dynamic_factory(pool.get());
    std::filesystem::path full_url = url / rpc;

    curl_easy_setopt(curl_handle.get(), CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl_handle.get(), CURLOPT_URL, full_url.c_str());

    RpcHandler rpc_handler(curl_handle.get(), svc_desc, method_desc, dynamic_factory, pool.get());
    ret_val = rpc_handler.handle(ctx);

    curl_slist_free_all(list);
  }
  return ret_val;
}
