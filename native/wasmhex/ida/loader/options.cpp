#ifndef WASMHEX_PURE_STRING_TEST
#include "options.hpp"
#endif

#include <cstddef>
#include <cstring>
#include <set>

namespace wasmhex::options
{

namespace detail
{

template <typename QStr, typename Set>
inline void parse_debug_categories(const QStr &value, Set *out)
{
  if ( out == nullptr || value.empty() )
    return;
  for ( std::size_t pos = 0; pos <= value.size(); )
  {
    const std::size_t comma = value.find(',', pos);
    const QStr token = value.substr(pos, comma);
    if ( !token.empty() )
      out->insert(token);
    if ( comma == QStr::npos )
      break;
    pos = comma + 1;
  }
}

} // namespace detail

#ifndef WASMHEX_PURE_STRING_TEST

extern "C" __attribute__((used)) const char wasmhex_m1_options_substr_build_marker[]
  = "WASMHEX_BUILD_MARKER_OPTIONS_SUBSTR_END_INDEX";

bool debug(const char *category)
{
  // WASMHEX_DEBUG is read exactly once per process — the getenv rule
  // keeps every other translation unit free of environment access.
  static const std::set<qstring> enabled = []()
  {
    std::set<qstring> out;
    qstring value;
    if ( qgetenv("WASMHEX_DEBUG", &value) && !value.empty() )
      detail::parse_debug_categories(value, &out);
    return out;
  }();
  return enabled.find(qstring(category)) != enabled.end();
}

bool env_string(const char *name, qstring *out_value)
{
  return out_value != nullptr && qgetenv(name, out_value);
}

#endif // WASMHEX_PURE_STRING_TEST

} // namespace wasmhex::options

#ifndef WASMHEX_PURE_STRING_TEST

namespace wasmhex::loader
{

void read_wasmhex_option(netnode &wasm_node, uchar tag, const char *env_name, const char *fallback,
                         qstring *out_value)
{
  qstring value;
  if ( !qgetenv(env_name, &value) || value.empty() )
  {
    // No environment override: a previously stored choice (same database
    // reloaded, or an option changed after load) wins over the fallback.
    qstring stored;
    if ( wasm_node.supstr(&stored, 0, tag) > 0 && !stored.empty() )
    {
      *out_value = stored;
      return;
    }
    *out_value = fallback;
    return;
  }
  *out_value = value;
  wasm_node.supset(0, value.c_str(), value.length() + 1, tag);
}

bool read_wasmhex_option_flag(netnode &wasm_node, uchar tag, const char *env_name, bool fallback)
{
  qstring value;
  read_wasmhex_option(wasm_node, tag, env_name, fallback ? "1" : "0", &value);
  return value == "1";
}

} // namespace wasmhex::loader

#endif // WASMHEX_PURE_STRING_TEST
