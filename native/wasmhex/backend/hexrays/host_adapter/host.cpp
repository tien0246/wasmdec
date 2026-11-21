#include "host.hpp"
#include "host_support.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <hexrays.hpp>
#include <diskio.hpp>
#include <loader.hpp>
#if defined(__APPLE__) && (defined(__arm64__) || defined(__aarch64__))
#include <CommonCrypto/CommonDigest.h>
#include <cstdio>
#include <cstdlib>
#endif

namespace wasmhex
{
static plugin_t *hexrays_host_plugin = nullptr;
static std::string hexrays_host_patch_directory;

#if defined(__APPLE__) && (defined(__arm64__) || defined(__aarch64__))

static std::string hexrays_host_sha256_text(const void *data, size_t size)
{
  std::array<uchar, CC_SHA256_DIGEST_LENGTH> digest;
  if ( CC_SHA256(data, CC_LONG(size), digest.data()) == nullptr )
    return std::string();
  char text[CC_SHA256_DIGEST_LENGTH * 2 + 1];
  for ( size_t i = 0; i < CC_SHA256_DIGEST_LENGTH; ++i )
    qsnprintf(text + i * 2, 3, "%02x", digest[i]);
  return std::string(text);
}

// Print a multi-line host_support_message() through msg(), one "wasmhex: "
// prefixed line at a time (DECISIONS #15: every refusal states its reason —
// never silent, never a crash).
static void hexrays_host_report(const std::string &message)
{
  // Split on '\n' without pulling in <sstream> just for this.
  size_t start = 0;
  while ( start <= message.size() )
  {
    const size_t end = message.find('\n', start);
    const std::string line
      = message.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if ( !line.empty() )
      msg("wasmhex: %s\n", line.c_str());
    if ( end == std::string::npos )
      break;
    start = end + 1;
  }
}

// The official support table lives in host_support.{hpp,cpp}; the
// runtime hexx64 SHA-256 is the load-bearing key (the plugin may be compiled
// against a different SDK line than the IDA it runs in — see host_support.hpp).
// Everything outside {platform x hexx64 SHA-256} gets the guided message from
// host_support_message() and a plain `false` — disassembly keeps working, F5
// is unavailable. A MATCHED entry also carries the byte-exact patch set for
// that build (host_support.hpp), applied below with the same fail-closed
// expected-bytes guard.
static const host_support_entry_t *hexrays_host_match_supported(const std::vector<uchar> &image)
{
  static const char *const k_platform = "macOS arm64";
  const std::string actual = hexrays_host_sha256_text(image.data(), image.size());
  if ( actual.empty() )
  {
    msg("wasmhex: could not hash hexx64.dylib\n");
    return nullptr;
  }
  const host_support_entry_t *const entry = match_host_support(k_platform, actual.c_str());
  if ( entry != nullptr )
    return entry;
  const host_support_status status = classify_host_support(k_platform, actual.c_str());
  hexrays_host_report(host_support_message(status, k_platform, actual.c_str()));
  return nullptr;
}
#endif

bool load_wasmhex_hexrays_host()
{
#if defined(__APPLE__) && (defined(__arm64__) || defined(__aarch64__))
  if ( hexrays_host_plugin != nullptr )
    return true;
  bool private_host_loaded = false;
  bool original_host_loaded = false;
  for ( plugin_info_t *info = get_plugins(); info != nullptr; info = info->next )
  {
    if ( info->entry == nullptr )
      continue;
    const std::string filename = info->path == nullptr
      ? std::string()
      : std::filesystem::path(info->path).filename().string();
    private_host_loaded |= filename == "hexx64_wasm_private.dylib";
    original_host_loaded |= filename == "hexx64.dylib";
  }
  if ( private_host_loaded )
    return true;
  if ( original_host_loaded )
  {
    msg("wasmhex: Hex-Rays was initialized before its private WASM backend\n");
    return false;
  }

  char original_path[QMAXFILE];
  if ( getsysfile(original_path, sizeof(original_path), "hexx64.dylib", PLG_SUBDIR) == nullptr )
  {
    msg("wasmhex: could not locate hexx64.dylib\n");
    return false;
  }
  std::ifstream input(original_path, std::ios::binary);
  if ( !input )
  {
    msg("wasmhex: could not read hexx64.dylib\n");
    return false;
  }
  std::vector<uchar> image((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
  if ( image.empty() || image.size() > UINT32_MAX )
  {
    msg("wasmhex: invalid hexx64.dylib size\n");
    return false;
  }

  // Support-table gate (see host_support.hpp) — the matched entry pins
  // BOTH the hexx64 build (SHA-256) and the byte-exact patch set for it. A
  // clear message with guidance is printed for every host outside the
  // table; nothing is patched, nothing crashes.
  const host_support_entry_t *const supported_entry = hexrays_host_match_supported(image);
  if ( supported_entry == nullptr )
    return false;

  // The patch set travels with the table entry: IDA patch levels move the
  // offsets, so each supported build carries its own proven five sites
  // (semantics documented in host_support.cpp). Fail-closed: every site must
  // show its expected bytes before anything is written.
  for ( std::size_t i = 0; i < supported_entry->patch_count; ++i )
  {
    const host_patch_t &patch = supported_entry->patches[i];
    if ( patch.offset > image.size() || image.size() - patch.offset < sizeof(patch.expected)
         || !std::equal(patch.expected, patch.expected + sizeof(patch.expected),
                        image.begin() + std::ptrdiff_t(patch.offset)) )
    {
      msg("wasmhex: hexx64 patch guard failed at file offset %a\n", ea_t(patch.offset));
      return false;
    }
    std::copy(patch.replacement, patch.replacement + sizeof(patch.replacement),
              image.begin() + std::ptrdiff_t(patch.offset));
  }

  char temp_template[] = "/tmp/ida_wasm_hexx64_XXXXXX";
  char *temp_dir = mkdtemp(temp_template);
  if ( temp_dir == nullptr )
  {
    msg("wasmhex: could not create a temporary Hex-Rays backend directory\n");
    return false;
  }
  const std::filesystem::path patched_path
    = std::filesystem::path(temp_dir) / "hexx64_wasm_private.dylib";
  {
    std::ofstream output(patched_path, std::ios::binary);
    if ( !output
         || !output.write(reinterpret_cast<const char *>(image.data()),
                          std::streamsize(image.size())) )
    {
      std::filesystem::remove_all(temp_dir);
      msg("wasmhex: could not write the patched Hex-Rays backend\n");
      return false;
    }
  }

  const std::string path = patched_path.string();
  const std::string sign_command = "/usr/bin/codesign --force --sign - --timestamp=none " + path;
  const std::string verify_command = "/usr/bin/codesign --verify --strict " + path;
  if ( std::system(sign_command.c_str()) != 0 || std::system(verify_command.c_str()) != 0 )
  {
    std::filesystem::remove_all(temp_dir);
    msg("wasmhex: could not ad-hoc sign the patched Hex-Rays backend\n");
    return false;
  }
  hexrays_host_plugin = load_plugin(path.c_str());
  if ( hexrays_host_plugin == nullptr )
  {
    std::filesystem::remove_all(temp_dir);
    msg("wasmhex: IDA could not load the patched Hex-Rays backend\n");
    return false;
  }
  if ( !change_hexrays_config("MAX_FUNCSIZE = 4096") )
  {
    msg("wasmhex: could not raise the Hex-Rays function-size limit\n");
    return false;
  }
  hexrays_host_patch_directory = temp_dir;
  return true;
#else
  msg("wasmhex: the built-in Hex-Rays dispatcher patch is available only on IDA 9.5 arm64 macOS\n");
  return false;
#endif
}

}
