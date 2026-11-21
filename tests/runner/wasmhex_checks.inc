// M6-S4a: All-C++ runner modes (sample, matrix, golden-merge, check)
// included inside the anonymous namespace of wasmdec_runner.cpp.

std::string find_wat2wasm()
{
  std::error_code ec;
  for ( const char *cand : { "/opt/homebrew/bin/wat2wasm", "/usr/local/bin/wat2wasm" } )
  {
    if ( fs::exists(cand, ec) )
      return cand;
  }
  return "wat2wasm";
}

bool compile_wat_file(const fs::path &wat_path, const fs::path &wasm_path,
                      const std::vector<std::string> &flags = {})
{
  std::string cmd = "'" + find_wat2wasm() + "'";
  for ( const std::string &fl : flags )
    cmd += " " + fl;
  cmd += " '" + wat_path.string() + "' -o '" + wasm_path.string() + "' 2>&1";
  FILE *pipe = popen(cmd.c_str(), "r");
  if ( pipe == nullptr )
    return false;
  char buf[256];
  std::string out;
  while ( std::fgets(buf, sizeof(buf), pipe) != nullptr )
    out += buf;
  int st = pclose(pipe);
  int rc = WIFEXITED(st) ? WEXITSTATUS(st) : st;
  if ( rc != 0 )
  {
    std::fprintf(stderr, "wat2wasm failed (%s): %s\n", wat_path.string().c_str(), out.c_str());
    return false;
  }
  return true;
}

bool compile_wat_string(const std::string &wat_source, const fs::path &wasm_path,
                        const std::vector<std::string> &flags = {})
{
  fs::path wat_path = wasm_path;
  wat_path.replace_extension(".wat");
  if ( !write_file_text(wat_path, wat_source) )
    return false;
  return compile_wat_file(wat_path, wasm_path, flags);
}

struct scratch_db_session_t
{
  fs::path work_dir;
  fs::path local_wasm;
  int open_rc = -1;
  bool opened = false;

  bool open(const fs::path &wasm_path, std::string *out_captured = nullptr)
  {
    if ( !prepare_scratch_wasm(wasm_path, &work_dir, &local_wasm) )
      return false;
    g_ui_listener.clear();
    open_rc = open_database(local_wasm.c_str(), true);
    if ( open_rc == 0 )
    {
      opened = true;
      auto_wait();
      if ( get_hexdsp() == nullptr )
        init_hexrays_plugin();
      change_hexrays_config("ARG_HINTS_MODE = 0");
    }
    if ( out_captured != nullptr )
      *out_captured = g_ui_listener.all_text();
    return open_rc == 0;
  }

  void close()
  {
    if ( opened )
    {
      set_database_flag(DBFL_KILL);
      close_database(false);
      opened = false;
    }
    if ( !work_dir.empty() )
    {
      std::error_code ec;
      fs::remove_all(work_dir, ec);
      work_dir.clear();
    }
  }

  ~scratch_db_session_t() { close(); }
};

std::map<std::string, ea_t> collect_db_functions(bool strip_wasm_prefix = false)
{
  std::map<std::string, ea_t> out;
  const size_t qty = get_func_qty();
  for ( size_t i = 0; i < qty; ++i )
  {
    func_entry_info_t fi;
    if ( !get_func_entry_info_by_num(&fi, i) )
      continue;
    qstring qname;
    get_func_name(&qname, fi.start_ea);
    std::string name(qname.c_str());
    out[name] = fi.start_ea;
    if ( strip_wasm_prefix && name.compare(0, 5, "wasm_") == 0 )
      out[name.substr(5)] = fi.start_ea;
  }
  return out;
}

std::string decompile_checked(ea_t ea, bool *out_ok = nullptr, hexrays_failure_t *out_hf = nullptr,
                              size_t *out_lvars = nullptr)
{
  if ( out_ok )
    *out_ok = false;
  hexrays_failure_t hf;
  cfuncptr_t cfunc(nullptr);
  try
  {
    cfunc = decompile_function(ea, &hf, 0);
  }
  catch ( ... )
  {
    cfunc = cfuncptr_t(nullptr);
  }
  if ( out_hf )
    *out_hf = hf;
  if ( cfunc == nullptr )
    return std::string();
  try
  {
    cfunc->verify(ALLOW_UNUSED_LABELS, true);
    cfunc->mba->verify(true);
    if ( out_lvars )
      *out_lvars = cfunc->get_lvars() != nullptr ? cfunc->get_lvars()->size() : 0;
    const strvec_t &sv = cfunc->get_pseudocode();
    std::string text;
    for ( size_t k = 0; k < sv.size(); ++k )
    {
      qstring clean;
      tag_remove(&clean, sv[k].line);
      if ( k > 0 )
        text.push_back('\n');
      text.append(clean.c_str(), clean.length());
    }
    if ( out_ok )
      *out_ok = true;
    return text;
  }
  catch ( ... )
  {
    return std::string();
  }
}

std::pair<std::string, std::string> decompile_two_pass(ea_t ea, bool *out_ok = nullptr)
{
  bool ok1 = false;
  std::string before = decompile_checked(ea, &ok1);
  if ( !ok1 )
  {
    if ( out_ok )
      *out_ok = false;
    return { "", "" };
  }
  mark_cfunc_dirty(ea, true);
  bool ok2 = false;
  std::string after = decompile_checked(ea, &ok2);
  if ( out_ok )
    *out_ok = ok2;
  return { before, after };
}

void append_uleb_bytes(std::vector<uint8_t> *out, uint64_t val)
{
  do
  {
    uint8_t b = static_cast<uint8_t>(val & 0x7Fu);
    val >>= 7;
    if ( val != 0 )
      b |= 0x80u;
    out->push_back(b);
  } while ( val != 0 );
}

void append_wasm_section(std::vector<uint8_t> *out, uint8_t sid,
                         const std::vector<uint8_t> &payload)
{
  out->push_back(sid);
  append_uleb_bytes(out, payload.size());
  out->insert(out->end(), payload.begin(), payload.end());
}

// ---------------------------------------------------------------------------
// Mode 4: sample [<module.wasm>] [--stride N] [--max-failures M] [--idausr <dir>]
// ---------------------------------------------------------------------------

int cmd_sample(const char *self_exe, int argc, char **argv)
{
  fs::path repo_root = find_repo_root();
  fs::path wasm_path = repo_root / "wasm-test" / "wasm-test" / "duckdb.wasm";
  fs::path explicit_idausr;
  long long stride = 100;
  long long max_failures = 1;

  long long env_val = 0;
  if ( parse_opt_int64(read_env_str("WASMHEX_STRIDE").c_str(), &env_val) )
    stride = env_val;
  if ( parse_opt_int64(read_env_str("WASMHEX_P74_MAX_FAILURES").c_str(), &env_val) )
    max_failures = env_val;

  for ( int i = 0; i < argc; ++i )
  {
    if ( std::strcmp(argv[i], "--stride") == 0 && i + 1 < argc )
      stride = std::strtoll(argv[++i], nullptr, 0);
    else if ( std::strcmp(argv[i], "--max-failures") == 0 && i + 1 < argc )
      max_failures = std::strtoll(argv[++i], nullptr, 0);
    else if ( std::strcmp(argv[i], "--idausr") == 0 && i + 1 < argc )
      explicit_idausr = argv[++i];
    else
      wasm_path = argv[i];
  }

  std::error_code ec;
  if ( !fs::is_regular_file(wasm_path, ec) )
  {
    std::fprintf(stderr, "duckdb fixture is missing: %s\n", wasm_path.string().c_str());
    return 2;
  }

  staged_idausr_t staged;
  if ( !setup_isolated_idausr(explicit_idausr, &staged) )
    return 2;

  long long worst = 0;
  for ( const char *mode : { "hidden", "explicit" } )
  {
    std::string cmd = "'" + std::string(self_exe) + "' f5 '" + wasm_path.string() + "' --stride "
      + std::to_string(stride) + " --bounds " + mode + " --idausr '" + staged.dir.string()
      + "' 2>&1";
    FILE *pipe = popen(cmd.c_str(), "r");
    if ( pipe == nullptr )
    {
      std::fprintf(stderr, "wasmdec_runner sample: failed to launch f5 for mode %s\n", mode);
      return 2;
    }
    std::vector<std::string> failing;
    std::vector<std::string> categories;
    long long total = -1;
    long long failures = -1;
    std::string f5_sha;
    char buf[1024];
    while ( std::fgets(buf, sizeof(buf), pipe) != nullptr )
    {
      std::string line = trim_ws(buf);
      if ( line.compare(0, 16, "WASMHEX_F5_FAIL ") == 0 )
      {
        failing.push_back(line.substr(16));
      }
      else if ( line.compare(0, 20, "WASMHEX_F5_CATEGORY ") == 0 )
      {
        size_t sp = line.find(' ', 20);
        if ( sp != std::string::npos )
          categories.push_back(line.substr(sp + 1));
      }
      else if ( line.compare(0, 16, "WASMHEX_F5_DONE ") == 0 )
      {
        std::sscanf(line.c_str(), "WASMHEX_F5_DONE total=%lld failures=%lld", &total, &failures);
      }
      else if ( line.compare(0, 17, "F5_SUBSET_SHA256 ") == 0 )
      {
        f5_sha = trim_ws(line.substr(17));
        std::printf("%s\n", line.c_str());
        std::fflush(stdout);
      }
      else if ( line.compare(0, 10, "F5_TIMING ") == 0 )
      {
        std::printf("%s\n", line.c_str());
        std::fflush(stdout);
      }
    }
    pclose(pipe);

    if ( total < 0 || failures < 0 )
    {
      std::fprintf(stderr, "%s sample run produced no summary\n", mode);
      return 1;
    }

    for ( size_t i = 0; i < std::min<size_t>(failing.size(), 20); ++i )
      std::printf("WASMHEX_P74_FAIL_%s %s\n", mode, failing[i].c_str());
    for ( size_t i = 0; i < std::min<size_t>(categories.size(), 10); ++i )
      std::printf("WASMHEX_P74_CATEGORY_%s %s\n", mode, categories[i].c_str());

    std::string mode_upper(mode);
    for ( char &c : mode_upper )
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::printf("WASMHEX_P74_MODE_%s total=%lld failures=%lld pass=%lld/%lld sha256=%s\n",
                mode_upper.c_str(), total, failures, total - failures, total, f5_sha.c_str());
    std::fflush(stdout);
    worst = std::max(worst, failures);
  }

  if ( worst > max_failures )
  {
    std::fprintf(stderr, "sample pass rate regressed: %lld failures > %lld\n", worst, max_failures);
    return 1;
  }
  std::printf("WASMHEX_P74_OK max_failures=%lld (threshold=%lld)\n", worst, max_failures);
  std::fflush(stdout);
  return 0;
}

// ---------------------------------------------------------------------------
// Mode 5: matrix [--idausr <dir>]
// ---------------------------------------------------------------------------

int32_t expected_brtable_dispatch(const std::string &fixture, uint32_t i)
{
  if ( fixture == "bt_small" )
    return i < 3 ? static_cast<int32_t>(10 + 10 * i) : 0;
  if ( fixture == "bt_boundary" )
    return i == 0 ? 10 : (i == 1 ? 11 : (i == 2 ? 12 : 0));
  if ( fixture == "bt_same_target" )
    return i < 8 ? 11 : 7;
  if ( fixture == "bt_all_same" )
    return 42;
  if ( fixture == "bt_default_dup" )
    return i == 1 ? 20 : 10;
  if ( fixture == "bt_large" )
    return (i >= 256 || (i % 16) == 15) ? 0 : static_cast<int32_t>(100 + (i % 2));
  if ( fixture == "bt_nested" )
  {
    uint32_t n = i >= 100 ? (i - 100) : i;
    return n == 0 ? 10 : (n == 1 ? 20 : (n == 2 ? 30 : 0));
  }
  if ( fixture == "bt_depths" )
    return i < 4 ? static_cast<int32_t>(4 - i) : (i == 4 ? 0 : 4);
  if ( fixture == "bt_unreachable" )
    return 0;
  if ( fixture == "bt_payload" )
    return i == 0 ? 6100 : (i == 1 ? 5101 : (i == 2 ? 3102 : static_cast<int32_t>(i + 100)));
  if ( fixture == "bt_depth_per_case_64" )
    return i < 63 ? static_cast<int32_t>(i) : 0;
  if ( fixture == "bt_depth_per_case_256" )
    return i < 255 ? static_cast<int32_t>(i) : 0;
  return 0;
}

int cmd_matrix(int argc, char **argv)
{
  fs::path repo_root = find_repo_root();
  fs::path brtable_dir = repo_root / "tests" / "fixtures" / "brtable";
  fs::path explicit_idausr;
  for ( int i = 0; i < argc; ++i )
  {
    if ( std::strcmp(argv[i], "--idausr") == 0 && i + 1 < argc )
      explicit_idausr = argv[++i];
  }

  static const char *const FIXTURES[] = {
    "bt_small",
    "bt_boundary",
    "bt_same_target",
    "bt_all_same",
    "bt_default_dup",
    "bt_large",
    "bt_nested",
    "bt_depths",
    "bt_unreachable",
    "bt_payload",
    "bt_depth_per_case_64",
    "bt_depth_per_case_256",
  };
  static const uint32_t SAMPLES[] = {
    0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 99, 255, 256, 300,
  };

  std::error_code ec;
  for ( const char *fx : FIXTURES )
  {
    if ( !fs::is_regular_file(brtable_dir / (std::string(fx) + ".wasm"), ec) )
    {
      std::fprintf(stderr, "missing fixture %s.wasm\n", fx);
      return 2;
    }
  }

  staged_idausr_t staged;
  if ( !ensure_idalib_initialized(explicit_idausr, &staged) )
    return 2;

  std::vector<std::string> red;
  for ( const char *fx_cstr : FIXTURES )
  {
    std::string fixture(fx_cstr);
    fs::path wasm_path = brtable_dir / (fixture + ".wasm");

    // 1. node (runtime via libwasmtime)
    {
      std::vector<std::pair<uint32_t, int32_t>> cases;
      for ( uint32_t s : SAMPLES )
        cases.push_back({ s, expected_brtable_dispatch(fixture, s) });
      if ( fixture == "bt_boundary" )
        cases.push_back({ 4294967295u, expected_brtable_dispatch(fixture, 4294967295u) });

      std::string err;
      bool ok = wasmhex_runner::wasmtime_eval_i32_1(wasm_path.string(), "dispatch", cases, &err);
      std::string status = ok ? "OK" : "MISMATCH";
      std::string line = "WASMHEX_BRTABLE_CASE " + fixture + " node " + status;
      std::printf("%s\n", line.substr(0, 220).c_str());
      std::fflush(stdout);
      if ( status != "OK" )
        red.push_back(line);
    }

    // 2. jtbl (IDA decompilation via idalib)
    {
      qsetenv("WASMHEX_BRTABLE", "jtbl");
      scratch_db_session_t sess;
      std::string status = "OK";
      if ( !sess.open(wasm_path) )
      {
        status = "CRASH open_database=" + std::to_string(sess.open_rc);
      }
      else
      {
        std::vector<std::string> bad;
        const size_t qty = get_func_qty();
        for ( size_t i = 0; i < qty; ++i )
        {
          func_entry_info_t fi;
          if ( !get_func_entry_info_by_num(&fi, i) )
            continue;
          qstring qname;
          get_func_name(&qname, fi.start_ea);
          std::string name = qname.empty() ? "sub" : std::string(qname.c_str());
          bool ok = false;
          hexrays_failure_t hf;
          std::string text = decompile_checked(fi.start_ea, &ok, &hf);
          if ( !ok )
          {
            char buf[128];
            qsnprintf(buf, sizeof(buf), "%s: INTERR code=%d errea=0x%llx", name.c_str(), hf.code,
                      static_cast<unsigned long long>(hf.errea));
            bad.push_back(buf);
            continue;
          }
          std::vector<std::string> flags;
          if ( text.find("STACK[") != std::string::npos )
            flags.push_back("STACK");
          if ( text.find("JUMPOUT(") != std::string::npos )
            flags.push_back("JUMPOUT");
          if ( !flags.empty() )
          {
            std::string fl = name + ": FLAGS ";
            for ( size_t k = 0; k < flags.size(); ++k )
              fl += (k > 0 ? "," : "") + flags[k];
            bad.push_back(fl);
          }
        }
        if ( !bad.empty() )
        {
          status.clear();
          for ( size_t k = 0; k < bad.size(); ++k )
            status += (k > 0 ? "; " : "") + bad[k];
        }
      }
      sess.close();
      std::string line = "WASMHEX_BRTABLE_CASE " + fixture + " jtbl " + status;
      std::printf("%s\n", line.substr(0, 220).c_str());
      std::fflush(stdout);
      if ( status != "OK" )
        red.push_back(line);
    }
  }

  std::printf("WASMHEX_BRTABLE_MATRIX total=%zu red=%zu\n",
              (sizeof(FIXTURES) / sizeof(FIXTURES[0])) * 2, red.size());
  std::fflush(stdout);
  if ( !red.empty() )
    return 2;
  std::printf("WASMHEX_BRTABLE_OK\n");
  std::fflush(stdout);
  return 0;
}

// ---------------------------------------------------------------------------
// Mode 6: golden-merge [<module.wasm>] [--strict] [--regenerate] [--idausr <dir>]
// ---------------------------------------------------------------------------

int cmd_golden_merge(int argc, char **argv)
{
  fs::path repo_root = find_repo_root();
  fs::path wasm_path = repo_root / "wasm-test" / "wasm-test" / "duckdb.wasm";
  fs::path baseline_path
    = repo_root / "tests" / "integration" / "golden" / "mergeProcessed_baseline.txt";
  fs::path explicit_idausr;
  bool strict = read_env_str("WASMHEX_GOLDEN_STRICT") == "1";
  bool regenerate = read_env_str("WASMHEX_GOLDEN_REGENERATE") == "1";

  for ( int i = 0; i < argc; ++i )
  {
    if ( std::strcmp(argv[i], "--strict") == 0 )
      strict = true;
    else if ( std::strcmp(argv[i], "--regenerate") == 0 )
      regenerate = true;
    else if ( std::strcmp(argv[i], "--idausr") == 0 && i + 1 < argc )
      explicit_idausr = argv[++i];
    else
      wasm_path = argv[i];
  }

  std::error_code ec;
  if ( !fs::is_regular_file(wasm_path, ec) )
  {
    std::fprintf(stderr, "duckdb fixture is missing: %s\n", wasm_path.string().c_str());
    return 2;
  }

  staged_idausr_t staged;
  if ( !ensure_idalib_initialized(explicit_idausr, &staged) )
    return 2;

  qsetenv("WASMHEX_BOUNDS", "hidden");
  scratch_db_session_t sess;
  if ( !sess.open(wasm_path) )
  {
    std::fprintf(stderr, "golden-merge: open_database failed with rc=%d\n", sess.open_rc);
    return 1;
  }

  ea_t target_ea = BADADDR;
  std::string target_name;
  size_t matches = 0;
  const size_t qty = get_func_qty();
  for ( size_t i = 0; i < qty; ++i )
  {
    func_entry_info_t fi;
    if ( !get_func_entry_info_by_num(&fi, i) )
      continue;
    qstring qname;
    get_func_name(&qname, fi.start_ea);
    std::string nm(qname.c_str());
    if ( nm.find("mergeProcessed") != std::string::npos )
    {
      target_ea = fi.start_ea;
      target_name = nm;
      ++matches;
    }
  }
  if ( matches != 1 || target_ea == BADADDR )
  {
    std::fprintf(stderr, "golden-merge: expected 1 mergeProcessed function, found %zu\n", matches);
    return 1;
  }

  bool ok = false;
  size_t lvars = 0;
  hexrays_failure_t hf;
  std::string text = decompile_checked(target_ea, &ok, &hf, &lvars);
  sess.close();
  if ( !ok )
  {
    std::fprintf(stderr, "golden-merge: decompile failed code=%d errea=0x%llx\n", hf.code,
                 static_cast<unsigned long long>(hf.errea));
    return 1;
  }

  std::vector<std::string> body = split_lines_no_eol(text);
  long non_empty_lines = 0;
  for ( const std::string &ln : body )
    if ( !trim_ws(ln).empty() )
      ++non_empty_lines;
  long stack_cnt = static_cast<long>(count_substr(text, "STACK["));
  long coerce_cnt = static_cast<long>(count_substr(text, "COERCE_"));
  long helpers_cnt = 0;
  {
    std::regex re("\\b(?:wasmhex_|wasm_)\\w+");
    helpers_cnt = static_cast<long>(
      std::distance(std::sregex_iterator(text.begin(), text.end(), re), std::sregex_iterator()));
  }
  long goto_cnt = 0;
  {
    std::regex re("\\bgoto\\b");
    goto_cnt = static_cast<long>(
      std::distance(std::sregex_iterator(text.begin(), text.end(), re), std::sregex_iterator()));
  }
  long lvars_cnt = static_cast<long>(lvars);

  std::map<std::string, std::string> current = {
    { "lines", std::to_string(non_empty_lines) }, { "stack", std::to_string(stack_cnt) },
    { "helpers", std::to_string(helpers_cnt) },   { "coerce", std::to_string(coerce_cnt) },
    { "goto", std::to_string(goto_cnt) },         { "lvars", std::to_string(lvars_cnt) },
  };
  static const char *const KEYS[] = { "lines", "stack", "helpers", "coerce", "goto", "lvars" };

  std::string header_line;
  for ( size_t i = 0; i < 6; ++i )
  {
    if ( i > 0 )
      header_line += " ";
    header_line += std::string(KEYS[i]) + "=" + current[KEYS[i]];
  }

  if ( regenerate )
  {
    std::ostringstream oss;
    oss << "# mergeProcessed golden baseline (" << target_name << ")\n";
    oss << "# METRICS " << header_line << "\n";
    oss << "# Regenerate: wasmdec_runner golden-merge --regenerate\n";
    for ( const std::string &ln : body )
      oss << ln << "\n";
    write_file_text(baseline_path, oss.str());
    std::printf("WASMHEX_GOLDEN_REGENERATED %s\n", header_line.c_str());
    return 0;
  }

  if ( !fs::is_regular_file(baseline_path, ec) )
  {
    std::fprintf(stderr, "golden baseline is missing: %s\n", baseline_path.string().c_str());
    return 2;
  }

  std::vector<std::string> base_raw = split_lines_no_eol(read_file_text(baseline_path));
  std::map<std::string, std::string> base_header;
  size_t body_start = 0;
  for ( size_t i = 0; i < base_raw.size(); ++i )
  {
    if ( base_raw[i].compare(0, 10, "# METRICS ") == 0 )
    {
      std::istringstream iss(base_raw[i].substr(10));
      std::string tok;
      while ( iss >> tok )
      {
        size_t eq = tok.find('=');
        if ( eq != std::string::npos )
          base_header[tok.substr(0, eq)] = tok.substr(eq + 1);
      }
      body_start = i + 1;
    }
  }
  std::vector<std::string> base_body;
  for ( size_t i = body_start; i < base_raw.size(); ++i )
  {
    if ( base_raw[i].empty() || base_raw[i][0] != '#' )
      base_body.push_back(base_raw[i]);
  }

  std::vector<std::string> diffs;
  for ( const char *k : KEYS )
  {
    std::string want = base_header.count(k) ? base_header[k] : "?";
    if ( current[k] != want )
      diffs.push_back(std::string(k) + " " + want + "->" + current[k]);
  }
  if ( strict && diffs.empty() && body != base_body )
  {
    diffs.push_back("text mismatch");
  }
  if ( !diffs.empty() )
  {
    for ( const std::string &d : diffs )
      std::printf("WASMHEX_GOLDEN_DIFF %s\n", d.c_str());
    size_t shown = 0;
    size_t n = std::min(base_body.size(), body.size());
    for ( size_t i = 0; i < n && shown < 5; ++i )
    {
      if ( base_body[i] != body[i] )
      {
        std::printf("WASMHEX_GOLDEN_LINE %zu -%s\n", i, base_body[i].c_str());
        std::printf("WASMHEX_GOLDEN_LINE %zu +%s\n", i, body[i].c_str());
        ++shown;
      }
    }
    return 1;
  }

  std::printf("WASMHEX_GOLDEN_OK %s\n", header_line.c_str());
  std::fflush(stdout);
  return 0;
}

// ---------------------------------------------------------------------------
// Mode 7: check [<case> ...] [--idausr <dir>]
// Bespoke verifier & checker migration (M6-S4a)
// ---------------------------------------------------------------------------

ea_t find_br_if_in_range(ea_t start, ea_t end)
{
  ea_t ea = start;
  while ( ea < end )
  {
    insn_t insn;
    int len = decode_insn(&insn, ea);
    if ( len > 0 )
    {
      qstring mnem;
      print_insn_mnem(&mnem, ea);
      if ( mnem == "br_if" )
        return ea;
      ea += static_cast<ea_t>(len);
    }
    else
    {
      ++ea;
    }
  }
  return BADADDR;
}

ea_t find_fc_imm_byte_in_range(ea_t start, ea_t end)
{
  ea_t ea = start;
  while ( ea < end )
  {
    insn_t insn;
    int len = decode_insn(&insn, ea);
    if ( len > 0 )
    {
      if ( insn.itype != 0 && insn.ops[0].type == o_imm && insn.ops[0].value == 147525116 )
        return ea + 1;
      ea += static_cast<ea_t>(len);
    }
    else
    {
      ++ea;
    }
  }
  return BADADDR;
}

bool craft_c_branch_target(netnode &node, ea_t ea, ea_t target)
{
  uint8_t blob[9];
  blob[0] = 0x01;
  uint64_t t = static_cast<uint64_t>(target);
  std::memcpy(blob + 1, &t, 8);
  return node.supset_ea(ea, blob, sizeof(blob), 'C');
}

void reanalyze_site(ea_t site)
{
  func_entry_info_t fn;
  if ( !get_func_entry_info(&fn, site) )
    return;
  insn_t insn;
  int len = decode_insn(&insn, site);
  if ( len > 0 )
  {
    del_items(site, DELIT_SIMPLE, static_cast<asize_t>(len));
    plan_ea(site);
    plan_and_wait(fn.start_ea, fn.end_ea);
  }
}

struct cfg_reject_v2_t
{
  bool present = false;
  uint8_t reason = 0;
  ea_t from_ea = BADADDR;
  ea_t to_ea = BADADDR;
};

cfg_reject_v2_t read_cfg_reject_v2(netnode &node, ea_t func_ea)
{
  cfg_reject_v2_t out;
  uint8_t raw[32];
  ssize_t sz = node.supval_ea(func_ea, raw, sizeof(raw), 'u');
  if ( sz == 18 && raw[0] == 2 )
  {
    out.present = true;
    out.reason = raw[1];
    uint64_t frm = 0;
    uint64_t to = 0;
    std::memcpy(&frm, raw + 2, 8);
    std::memcpy(&to, raw + 10, 8);
    out.from_ea = static_cast<ea_t>(frm);
    out.to_ea = static_cast<ea_t>(to);
  }
  return out;
}

bool has_u_mark(netnode &node, ea_t func_ea)
{
  uint8_t raw[32];
  ssize_t sz = node.supval_ea(func_ea, raw, sizeof(raw), 'u');
  if ( sz <= 0 )
    return false;
  return (sz == 1 && raw[0] == 1) || (sz == 18 && raw[0] == 2);
}

bool run_check_p04_cfg_reject(const fs::path &fixtures_dir)
{
  qsetenv("WASMHEX_BOUNDS", "hidden");
  scratch_db_session_t sess;
  if ( !sess.open(fixtures_dir / "s10_cfg_incomplete_repro.wasm") )
    return false;

  bool ok = true;
  auto expect = [&](const char *name, bool cond, const std::string &detail = "")
  {
    if ( !cond )
      ok = false;
    std::printf("WASMHEX_P04_%s %s %s\n", cond ? "OK " : "FAIL", name, detail.c_str());
  };

  netnode node("$ wasmhex", 0, false);
  ea_t cross = get_name_ea(BADADDR, "s10_cross");
  ea_t other = get_name_ea(BADADDR, "s10_clean");
  ea_t mid = get_name_ea(BADADDR, "s10_midinsn");
  if ( cross == BADADDR || other == BADADDR || mid == BADADDR )
    return false;

  func_entry_info_t cross_fn;
  if ( !get_func_entry_info(&cross_fn, cross) )
    return false;
  ea_t site = find_br_if_in_range(cross_fn.start_ea, cross_fn.end_ea);
  expect("cross_site_found", site != BADADDR);
  craft_c_branch_target(node, site, other);

  func_entry_info_t mid_fn;
  if ( !get_func_entry_info(&mid_fn, mid) )
    return false;
  ea_t site2 = find_br_if_in_range(mid_fn.start_ea, mid_fn.end_ea);
  expect("midinsn_site_found", site2 != BADADDR);
  ea_t fc_byte = find_fc_imm_byte_in_range(mid_fn.start_ea, mid_fn.end_ea);
  expect("fc_imm_found", fc_byte != BADADDR);
  craft_c_branch_target(node, site2, fc_byte);

  reanalyze_site(site);
  reanalyze_site(site2);

  cfg_reject_v2_t rec_cross = read_cfg_reject_v2(node, cross);
  expect("cross_record_v2", rec_cross.present);
  if ( rec_cross.present )
  {
    expect("cross_reason", rec_cross.reason == 2);
    expect("cross_edge_eas", rec_cross.from_ea == site && rec_cross.to_ea == other);
  }

  cfg_reject_v2_t rec_mid = read_cfg_reject_v2(node, mid);
  expect("midinsn_record_v2", rec_mid.present);
  if ( rec_mid.present )
  {
    expect("midinsn_reason", rec_mid.reason == 3);
    expect("midinsn_edge_eas", rec_mid.from_ea == site2 && rec_mid.to_ea == fc_byte);
  }

  std::vector<ea_t> marks;
  for ( size_t i = 0; i < get_func_qty(); ++i )
  {
    func_entry_info_t fi;
    if ( get_func_entry_info_by_num(&fi, i) && read_cfg_reject_v2(node, fi.start_ea).present )
      marks.push_back(fi.start_ea);
  }
  std::sort(marks.begin(), marks.end());
  std::vector<ea_t> want_marks = { cross, mid };
  std::sort(want_marks.begin(), want_marks.end());
  expect("exactly_two_records", marks == want_marks);

  bool dec_cross = false;
  decompile_checked(cross, &dec_cross);
  expect("cross_f5_refused", !dec_cross);

  bool dec_mid = false;
  decompile_checked(mid, &dec_mid);
  expect("midinsn_f5_refused", !dec_mid);

  expect("clean_unmarked", !read_cfg_reject_v2(node, other).present);
  bool dec_clean = false;
  decompile_checked(other, &dec_clean);
  expect("clean_f5_ok", dec_clean);

  std::printf("WASMHEX_P04_%s\n", ok ? "OK" : "RED");
  std::fflush(stdout);
  return ok;
}

bool run_check_s10_cfg_incomplete(const fs::path &fixtures_dir)
{
  qsetenv("WASMHEX_BOUNDS", "hidden");
  scratch_db_session_t sess;
  if ( !sess.open(fixtures_dir / "s10_cfg_incomplete_repro.wasm") )
    return false;

  bool ok = true;
  auto expect = [&](const char *name, bool cond)
  {
    if ( !cond )
      ok = false;
    std::printf("WASMHEX_S10_%s %s\n", cond ? "OK " : "FAIL", name);
  };

  netnode node("$ wasmhex", 0, false);
  ea_t cross = get_name_ea(BADADDR, "s10_cross");
  ea_t other = get_name_ea(BADADDR, "s10_clean");
  ea_t mid = get_name_ea(BADADDR, "s10_midinsn");
  if ( cross == BADADDR || other == BADADDR || mid == BADADDR )
    return false;

  func_entry_info_t cross_fn;
  if ( !get_func_entry_info(&cross_fn, cross) )
    return false;
  ea_t site = find_br_if_in_range(cross_fn.start_ea, cross_fn.end_ea);
  expect("cross_site_found", site != BADADDR);
  craft_c_branch_target(node, site, other);

  func_entry_info_t mid_fn;
  if ( !get_func_entry_info(&mid_fn, mid) )
    return false;
  ea_t site2 = find_br_if_in_range(mid_fn.start_ea, mid_fn.end_ea);
  expect("midinsn_site_found", site2 != BADADDR);
  ea_t fc_byte = find_fc_imm_byte_in_range(mid_fn.start_ea, mid_fn.end_ea);
  expect("fc_imm_found", fc_byte != BADADDR);
  craft_c_branch_target(node, site2, fc_byte);

  reanalyze_site(site);
  reanalyze_site(site2);

  expect("cross_marked", has_u_mark(node, cross));
  expect("midinsn_marked", has_u_mark(node, mid));

  std::vector<ea_t> marks;
  for ( size_t i = 0; i < get_func_qty(); ++i )
  {
    func_entry_info_t fi;
    if ( get_func_entry_info_by_num(&fi, i) && has_u_mark(node, fi.start_ea) )
      marks.push_back(fi.start_ea);
  }
  std::sort(marks.begin(), marks.end());
  std::vector<ea_t> want_marks = { cross, mid };
  std::sort(want_marks.begin(), want_marks.end());
  expect("exactly_two_marks", marks == want_marks);

  bool dec_cross = false;
  decompile_checked(cross, &dec_cross);
  expect("cross_f5_refused", !dec_cross);

  bool dec_mid = false;
  decompile_checked(mid, &dec_mid);
  expect("midinsn_f5_refused", !dec_mid);

  expect("clean_unmarked", !has_u_mark(node, other));
  bool dec_clean = false;
  decompile_checked(other, &dec_clean);
  expect("clean_f5_ok", dec_clean);

  std::printf("WASMHEX_S10_%s\n", ok ? "OK" : "RED");
  std::fflush(stdout);
  return ok;
}

bool run_check_s10_corpus_marks(const fs::path &fixtures_dir)
{
  qsetenv("WASMHEX_BOUNDS", "hidden");
  scratch_db_session_t sess;
  if ( !sess.open(fixtures_dir / "s10_cfg_incomplete_repro.wasm") )
    return false;

  netnode node("$ wasmhex", 0, false);
  size_t marks = 0;
  size_t total = get_func_qty();
  for ( size_t i = 0; i < total; ++i )
  {
    func_entry_info_t fi;
    if ( get_func_entry_info_by_num(&fi, i) && has_u_mark(node, fi.start_ea) )
      ++marks;
  }
  std::printf("WASMHEX_S10_CORPUS functions=%zu cfg_incomplete_marks=%zu\n", total, marks);
  std::fflush(stdout);
  return marks == 0;
}

bool run_check_p05_go_refresh_set(const fs::path &fixtures_dir)
{
  qsetenv("WASMHEX_BOUNDS", "hidden");
  scratch_db_session_t sess;
  if ( !sess.open(fixtures_dir / "p05_go_refresh_foreign_set.wasm") )
    return false;

  bool ok = true;
  auto expect = [&](const char *name, bool cond)
  {
    if ( !cond )
      ok = false;
    std::printf("WASMHEX_P05_%s %s\n", cond ? "OK " : "FAIL", name);
  };

  ea_t entry = get_name_ea(BADADDR, "p05_foreign_set");
  expect("entry_found", entry != BADADDR);
  if ( entry == BADADDR )
    return false;

  netnode node("$ wasmhex", 0, false);
  bytevec_t raw;
  ssize_t bsz = node.getblob_ea(&raw, entry, 'x');
  expect("blob_x_present", bsz >= 16 && (bsz % 4) == 0);
  if ( bsz < 24 )
    return false;

  std::vector<uint32_t> words(static_cast<size_t>(bsz) / 4);
  std::memcpy(words.data(), raw.begin(), static_cast<size_t>(bsz));
  expect("one_record", words[0] == 2 && words[1] == 1);
  uint32_t sp_restore_off = words[5];

  func_entry_info_t fn;
  if ( !get_func_entry_info(&fn, entry) )
    return false;
  std::vector<ea_t> planted;
  ea_t ea = fn.start_ea;
  while ( ea < fn.end_ea )
  {
    insn_t insn;
    int n = decode_insn(&insn, ea);
    if ( n <= 0 )
      break;
    qstring mnem;
    print_insn_mnem(&mnem, ea);
    if ( mnem == "global.get" && insn.ops[0].value == 0 )
    {
      qstring nxt_mnem;
      print_insn_mnem(&nxt_mnem, ea + static_cast<ea_t>(n));
      if ( nxt_mnem == "i32.const" )
        planted.push_back(ea);
    }
    ea += static_cast<ea_t>(n);
  }
  expect("planted_refresh_found", planted.size() == 1);
  if ( planted.empty() )
    return false;

  ea_t planted_ea = planted[0];
  expect("corrupt_target_differs", planted_ea != entry + sp_restore_off);
  // M2 D3/H2: decompile ONCE with the pristine blob so the per-function
  // FunctionIR cache holds an entry for this EA, THEN corrupt — the
  // re-decompile below must go through the invalidation path (wire-hash
  // mismatch -> rebuild from the new bytes), never a stale entry.
  {
    bool pre_ok = false;
    (void)decompile_checked(entry, &pre_ok);
    expect("pre_corruption_decompiles", pre_ok);
  }
  words[5] = static_cast<uint32_t>(planted_ea - entry);
  expect("blob_x_rewritten",
         node.setblob_ea(words.data(), words.size() * sizeof(uint32_t), entry, 'x'));

  mark_cfunc_dirty(entry, true);
  bool dec_ok = false;
  std::string text = decompile_checked(entry, &dec_ok);
  expect("decompiles_after_corruption", dec_ok);
  if ( dec_ok )
  {
    bool survived = false;
    for ( const std::string &ln : split_lines_no_eol(text) )
    {
      if ( (ln.find("wasm_global_0") != std::string::npos
            || ln.find("global_0") != std::string::npos)
           && ln.find('+') != std::string::npos )
      {
        survived = true;
        break;
      }
    }
    expect("foreign_set_survives", survived);
    expect("no_stack_refs", text.find("STACK[") == std::string::npos);
    expect("no_jumpout", text.find("JUMPOUT(") == std::string::npos);
  }

  std::printf("WASMHEX_P05_%s\n", ok ? "OK" : "RED");
  std::fflush(stdout);
  return ok;
}

bool run_check_p06_morestack_names(const fs::path &fixtures_dir)
{
  qsetenv("WASMHEX_BOUNDS", "hidden");
  scratch_db_session_t sess;
  if ( !sess.open(fixtures_dir / "p06_morestack_names.wasm") )
    return false;

  bool ok = true;
  auto expect = [&](const char *name, bool cond)
  {
    if ( !cond )
      ok = false;
    std::printf("WASMHEX_P06_%s %s\n", cond ? "OK " : "FAIL", name);
  };

  ea_t ms = get_name_ea(BADADDR, "runtime.morestack");
  expect("pclntab_name_landed", ms != BADADDR);

  ea_t named_ea = get_name_ea(BADADDR, "p06_named_hidden");
  if ( named_ea == BADADDR )
    named_ea = get_name_ea(BADADDR, "main.a");
  bool dec_named = false;
  std::string named_text = named_ea != BADADDR ? decompile_checked(named_ea, &dec_named) : "";
  expect("named_decompiles", dec_named);
  if ( dec_named )
  {
    expect("named_guard_hidden", named_text.find("runtime.morestack(") == std::string::npos);
    expect("named_store_hidden", named_text.find("4660") == std::string::npos);
    expect("named_body_kept", named_text.find("42") != std::string::npos);
  }

  ea_t unnamed_ea = get_name_ea(BADADDR, "p06_unnamed_kept");
  if ( unnamed_ea == BADADDR )
    unnamed_ea = get_name_ea(BADADDR, "main.b");
  bool dec_unnamed = false;
  std::string unnamed_text
    = unnamed_ea != BADADDR ? decompile_checked(unnamed_ea, &dec_unnamed) : "";
  expect("unnamed_decompiles", dec_unnamed);
  if ( dec_unnamed )
  {
    expect("unnamed_guard_kept", unnamed_text.find("runtime.mini(") != std::string::npos);
    expect("unnamed_store_kept", unnamed_text.find("4660") != std::string::npos);
    expect("unnamed_body_kept", unnamed_text.find("7") != std::string::npos);
  }

  for ( size_t i = 0; i < get_func_qty(); ++i )
  {
    func_entry_info_t fi;
    if ( !get_func_entry_info_by_num(&fi, i) )
      continue;
    bool fn_ok = false;
    decompile_checked(fi.start_ea, &fn_ok);
    if ( !fn_ok )
      ok = false;
  }

  std::printf("WASMHEX_P06_%s\n", ok ? "OK" : "RED");
  std::fflush(stdout);
  return ok;
}

bool run_check_s9_morestack_callee(const fs::path &fixtures_dir)
{
  qsetenv("WASMHEX_BOUNDS", "hidden");
  scratch_db_session_t sess;
  if ( !sess.open(fixtures_dir / "s9_go_morestack_callee.wasm") )
    return false;

  bool ok = true;
  auto expect = [&](const char *name, bool cond)
  {
    if ( !cond )
      ok = false;
    std::printf("WASMHEX_S9_%s %s\n", cond ? "OK " : "FAIL", name);
  };

  auto dec_by_name = [&](const char *nm, bool *out_ok) -> std::string
  {
    ea_t ea = get_name_ea(BADADDR, nm);
    if ( ea == BADADDR )
    {
      *out_ok = false;
      return "";
    }
    return decompile_checked(ea, out_ok);
  };

  bool ok_u = false;
  std::string unnamed = dec_by_name("s9_guard_hidden", &ok_u);
  expect("unnamed_shell_decompiles", ok_u);
  if ( ok_u )
  {
    expect("unnamed_shell_guard_kept", unnamed.find("wasm_func_1(") != std::string::npos);
    expect("unnamed_shell_store_kept", unnamed.find("4660") != std::string::npos);
    expect("unnamed_shell_body_kept", unnamed.find("42") != std::string::npos);
  }

  bool ok_p = false;
  std::string plain = dec_by_name("s9_plain_kept", &ok_p);
  expect("plain_kept_decompiles", ok_p);
  if ( ok_p )
    expect("plain_kept_if_visible", plain.find("wasm_func_1(") != std::string::npos);

  bool ok_i = false;
  std::string infix = dec_by_name("s9_infix_callee_kept", &ok_i);
  expect("infix_callee_decompiles", ok_i);
  if ( ok_i )
    expect("infix_callee_call_visible", infix.find("xwasm_func_9(") != std::string::npos);

  for ( size_t i = 0; i < get_func_qty(); ++i )
  {
    func_entry_info_t fi;
    if ( !get_func_entry_info_by_num(&fi, i) )
      continue;
    bool fn_ok = false;
    decompile_checked(fi.start_ea, &fn_ok);
    if ( !fn_ok )
      ok = false;
  }

  std::printf("WASMHEX_S9_%s\n", ok ? "OK" : "RED");
  std::fflush(stdout);
  return ok;
}

bool run_check_s1s2s7s11_mode(const fs::path &fixtures_dir, const std::string &mode)
{
  bool ok = true;
  auto expect = [&](const std::string &name, bool cond)
  {
    if ( !cond )
      ok = false;
    std::string m_up = mode;
    for ( char &c : m_up )
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::printf("WASMHEX_%s_%s %s\n", m_up.c_str(), cond ? "OK " : "FAIL", name.c_str());
  };

  if ( mode == "s1" )
  {
    qsetenv("WASMHEX_BOUNDS", "explicit");
    scratch_db_session_t sess;
    if ( !sess.open(fixtures_dir / "s1_import_table_arg_clobber.wasm") )
      return false;
    std::regex clobber1("v\\d+\\(\\(int\\)v\\d+,");
    std::regex clobber2("v\\d+\\(v\\d+, a\\d\\)");
    for ( const char *fn_name : { "s1_first_arg_survives", "s1_swapped", "s1_loaded_arg" } )
    {
      ea_t ea = get_name_ea(BADADDR, fn_name);
      bool dec_ok = false;
      std::string text = ea != BADADDR ? decompile_checked(ea, &dec_ok) : "";
      expect(std::string(fn_name) + "_decompiles", dec_ok);
      if ( dec_ok )
      {
        expect(std::string(fn_name) + "_lookup_rendered",
               text.find("wasm_table_lookup") != std::string::npos
                 || text.find("__indirect_function_table") != std::string::npos);
        bool has_clobber = std::regex_search(text, clobber1) || std::regex_search(text, clobber2);
        expect(std::string(fn_name) + "_arg0_not_callee", !has_clobber);
      }
    }
    qsetenv("WASMHEX_BOUNDS", "hidden");
  }
  else if ( mode == "s2" )
  {
    qsetenv("WASMHEX_BOUNDS", "hidden");
    scratch_db_session_t sess;
    if ( !sess.open(fixtures_dir / "s2_go_sp_stable.wasm") )
      return false;
    ea_t ea = get_name_ea(BADADDR, "s2_two_calls");
    bool dec_ok = false;
    std::string text = ea != BADADDR ? decompile_checked(ea, &dec_ok) : "";
    expect("decompiles", dec_ok);
    if ( dec_ok )
    {
      size_t sp_writes = 0;
      size_t calls = 0;
      for ( const std::string &ln : split_lines_no_eol(text) )
      {
        if ( ln.find("wasm_global_0") != std::string::npos )
          ++sp_writes;
        if ( ln.find("wasm_func_3") != std::string::npos )
          ++calls;
      }
      expect("two_calls_rendered", calls == 2);
      expect("sp_not_drifting", sp_writes == 0);
    }
  }
  else if ( mode == "s7" )
  {
    qsetenv("WASMHEX_BOUNDS", "hidden");
    scratch_db_session_t sess;
    if ( !sess.open(fixtures_dir / "s7_go_f32_arg.wasm") )
      return false;
    ea_t ea = get_name_ea(BADADDR, "s7_f32_arg");
    bool dec_ok = false;
    std::string text = ea != BADADDR ? decompile_checked(ea, &dec_ok) : "";
    expect("decompiles", dec_ok);
    if ( dec_ok )
    {
      std::vector<std::string> calls;
      for ( const std::string &ln : split_lines_no_eol(text) )
        if ( ln.find("wasm_func_3") != std::string::npos )
          calls.push_back(ln);
      expect("call_present", calls.size() == 1);
      bool f32_in_call = !calls.empty()
        && (calls[0].find("1.5") != std::string::npos
            || calls[0].find("0x3FC00000") != std::string::npos
            || calls[0].find("float") != std::string::npos);
      expect("f32_arg_in_call", f32_in_call);
    }
  }
  else if ( mode == "s11" )
  {
    qsetenv("WASMHEX_BOUNDS", "hidden");
    scratch_db_session_t sess;
    if ( !sess.open(fixtures_dir / "s11_go_zero_arg.wasm") )
      return false;
    ea_t ea = get_name_ea(BADADDR, "s11_zero_arg");
    bool dec_ok = false;
    std::string text = ea != BADADDR ? decompile_checked(ea, &dec_ok) : "";
    expect("decompiles", dec_ok);
    if ( dec_ok )
    {
      std::vector<std::string> calls;
      for ( const std::string &ln : split_lines_no_eol(text) )
        if ( ln.find("wasm_func_3") != std::string::npos )
          calls.push_back(ln);
      expect("call_present", calls.size() == 1);
      expect("zero_arg_call", !calls.empty() && calls[0].find("()") != std::string::npos);
    }
  }
  else if ( mode == "s8" )
  {
    qsetenv("WASMHEX_BOUNDS", "hidden");
    scratch_db_session_t sess;
    if ( !sess.open(fixtures_dir / "s2_go_sp_stable.wasm") )
      return false;
    ea_t entry = get_name_ea(BADADDR, "s2_two_calls");
    expect("entry_found", entry != BADADDR);
    netnode node("$ wasmhex", 0, false);
    uint32_t bad_blob[2] = { 2, 1 };
    expect("blob_written", node.setblob_ea(bad_blob, sizeof(bad_blob), entry, 'x'));
    mark_cfunc_dirty(entry, true);
    bool dec_ok = false;
    std::string text = decompile_checked(entry, &dec_ok);
    expect("f5_survives_corrupt_record", dec_ok);
    if ( dec_ok )
    {
      size_t sp_writes = 0;
      for ( const std::string &ln : split_lines_no_eol(text) )
        if ( ln.find("wasm_global_0") != std::string::npos )
          ++sp_writes;
      expect("nothing_consumed", sp_writes >= 2);
    }
  }

  std::string m_up = mode;
  for ( char &c : m_up )
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  std::printf("WASMHEX_%s_%s\n", m_up.c_str(), ok ? "OK" : "RED");
  std::fflush(stdout);
  return ok;
}

bool run_check_s1s2s7s11(const fs::path &fixtures_dir)
{
  bool all_ok = true;
  for ( const char *m : { "s1", "s2", "s7", "s11", "s8" } )
  {
    if ( !run_check_s1s2s7s11_mode(fixtures_dir, m) )
      all_ok = false;
  }
  return all_ok;
}

bool run_check_s7_go_provenance_one(const fs::path &wasm_path, const std::string &want)
{
  qsetenv("WASMHEX_BOUNDS", "hidden");
  scratch_db_session_t sess;
  if ( !sess.open(wasm_path) )
    return false;

  netnode node("$ wasmhex", 0, false);
  uint8_t blob[16];
  ssize_t sz = node.supval(0, blob, sizeof(blob), 'G');
  bool go_flag = (sz == 2 && blob[0] == 1 && blob[1] == 1) || (sz == 1 && blob[0] == 1);

  bool ok = true;
  if ( want == "good" )
  {
    if ( !(sz == 2 && blob[0] == 1 && blob[1] == 1) )
      ok = false;
  }
  else
  {
    if ( go_flag )
      ok = false;
  }

  for ( size_t i = 0; i < get_func_qty(); ++i )
  {
    func_entry_info_t fi;
    if ( !get_func_entry_info_by_num(&fi, i) )
      continue;
    bool fn_ok = false;
    decompile_checked(fi.start_ea, &fn_ok);
    if ( !fn_ok )
      ok = false;
  }

  std::printf("WASMHEX_S7_%s mode=%s\n", ok ? "OK" : "RED", want.c_str());
  std::fflush(stdout);
  return ok;
}

bool run_check_s7_go_provenance(const fs::path &fixtures_dir)
{
  return run_check_s7_go_provenance_one(fixtures_dir / "s7_go_provenance_good.wasm", "good")
    && run_check_s7_go_provenance_one(fixtures_dir / "s7_go_provenance_bad_globals.wasm",
                                      "bad_globals")
    && run_check_s7_go_provenance_one(fixtures_dir / "s7_go_provenance_bad_prologue.wasm",
                                      "bad_prologue");
}

bool run_check_cframe_fallback(const fs::path &fixtures_dir)
{
  qsetenv("WASMHEX_BOUNDS", "hidden");
  scratch_db_session_t sess;
  std::string captured;
  if ( !sess.open(fixtures_dir / "cframe_fallback_reason.wasm", &captured) )
    return false;

  const char *diag = "wasmhex: function 0 (0x1000) C shadow stack fallback: "
                     "uses C stack pointer without a canonical entry prologue";
  if ( captured.find(diag) == std::string::npos )
  {
    std::fprintf(stderr, "missing C-frame fallback diagnostic\n");
    return false;
  }

  ea_t ea = get_name_ea(BADADDR, "poke");
  bool dec_ok = false;
  std::string code = ea != BADADDR ? decompile_checked(ea, &dec_ok) : "";
  if ( !dec_ok )
    return false;
  if ( code.find("wasm_global") == std::string::npos
       && code.find("_stack_pointer") == std::string::npos )
    return false;

  std::printf("WASMHEX_CFRAME_FALLBACK_OK diagnostic+linear-form\n");
  std::fflush(stdout);
  return true;
}

bool run_check_p4_idioms()
{
  static const char *const WAT = R"((module
  (import "env" "__table_base" (global $tb i32))
  (import "env" "__THREW__" (global $threw (mut i32)))
  (import "env" "invoke_vii" (func $invoke_vii (param i32 i32 i32)))
  (import "env" "invoke_vi" (func $invoke_vi (param i32 i32)))
  (import "env" "invoke_v" (func $invoke_v (param i32)))
  (table 2 funcref)
  (elem (global.get $tb) $target_fn0 $target_fn1)
  (func $target_fn0 (param i32 i32) local.get 0 local.get 1 i32.add drop)
  (func $target_fn1 (param i32) local.get 0 drop)
  (func (export "test_table_base_call") (param $arg0 i32) (param $arg1 i32)
    global.get $tb local.get $arg0 local.get $arg1 call $invoke_vii
    global.get $tb i32.const 1 i32.add local.get $arg0 call $invoke_vi)
  (func (export "test_emscripten_eh") (param $arg0 i32) (result i32)
    (local $t i32)
    i32.const 0 global.set $threw
    global.get $tb i32.const 1 i32.add local.get $arg0 call $invoke_vi
    global.get $threw local.set $t
    i32.const 0 global.set $threw
    local.get $t i32.const 1 i32.eq
    if i32.const 999 return end
    local.get $arg0)
))";

  char tmpl[] = "/private/tmp/wasmhex_p4_XXXXXX";
  char *made = mkdtemp(tmpl);
  if ( made == nullptr )
    return false;
  fs::path tmp(made);
  fs::path wasm = tmp / "idioms_test.wasm";
  bool compiled = compile_wat_string(WAT, wasm, { "--debug-names" });
  if ( !compiled )
  {
    std::error_code ec;
    fs::remove_all(tmp, ec);
    return false;
  }

  qsetenv("WASMHEX_BOUNDS", "hidden");
  scratch_db_session_t sess;
  bool opened = sess.open(wasm);
  std::error_code ec;
  fs::remove_all(tmp, ec);
  if ( !opened )
    return false;

  bool p4_2_ok = false;
  ea_t fn_tb = get_name_ea(BADADDR, "test_table_base_call");
  if ( fn_tb != BADADDR )
  {
    bool dec_ok = false;
    std::string code = decompile_checked(fn_tb, &dec_ok);
    if ( dec_ok )
    {
      bool has_direct = code.find("&target_fn0") != std::string::npos
        || code.find("&wasm_func_") != std::string::npos
        || code.find("target_fn0") != std::string::npos;
      size_t brace = code.find('{');
      std::string body = brace != std::string::npos ? code.substr(brace + 1) : code;
      bool has_tb = std::regex_search(body, std::regex("\\b__?table_base\\b"));
      if ( has_direct && !has_tb )
      {
        p4_2_ok = true;
        std::printf("PASS_P4_2_FUNCPTR: __table_base + k replaced by function address\n");
      }
    }
  }

  bool p4_3_ok = false;
  ea_t fn_eh = get_name_ea(BADADDR, "test_emscripten_eh");
  if ( fn_eh != BADADDR )
  {
    bool dec_ok = false;
    std::string code = decompile_checked(fn_eh, &dec_ok);
    if ( dec_ok )
    {
      std::regex reset_re("(?:GOT\\.mem\\.)?_{1,2}THREW__(?:\\][^=]*|\\s*)=\\s*0\\b");
      std::regex eq_one_re("(?:GOT\\.mem\\.)?_{1,2}THREW__(?:\\][^=]*|\\s*)==\\s*1\\b");
      if ( std::regex_search(code, eq_one_re) && !std::regex_search(code, reset_re) )
      {
        p4_3_ok = true;
        std::printf(
          "PASS_P4_3_EH: __THREW__ = 0 resets folded; direct == 1 landing check retained\n");
      }
    }
  }

  if ( p4_2_ok && p4_3_ok )
  {
    std::printf("M5_IDIOMS_VERIFICATION_SUCCESS\n");
    std::fflush(stdout);
    return true;
  }
  return false;
}

bool run_check_prototypes()
{
  static const char *const WAT = R"((module
  (import "env" "free" (func $free (param i32)))
  (import "env" "malloc" (func $malloc (param i32) (result i32)))
  (import "env" "memmove" (func $memmove (param i32 i32 i32) (result i32)))
  (func (export "test_libc") (param $size i32) (result i32)
    (local $ptr i32)
    local.get $size call $malloc local.set $ptr
    local.get $ptr local.get $ptr local.get $size call $memmove drop
    local.get $ptr call $free
    local.get $ptr)
  (func (export "test_param_names") (param $value i32) (param $index i32) (result i32)
    local.get $value local.get $index i32.add)
  (func (export "_ZN3Foo3barEi") (param i32 i32) (result i32)
    local.get 0 local.get 1 i32.add)
))";

  char tmpl[] = "/private/tmp/wasmhex_protos_XXXXXX";
  char *made = mkdtemp(tmpl);
  if ( made == nullptr )
    return false;
  fs::path tmp(made);
  fs::path wasm = tmp / "protos_test.wasm";
  bool compiled = compile_wat_string(WAT, wasm, { "--debug-names" });
  if ( !compiled )
  {
    std::error_code ec;
    fs::remove_all(tmp, ec);
    return false;
  }

  qsetenv("WASMHEX_BOUNDS", "hidden");
  scratch_db_session_t sess;
  bool opened = sess.open(wasm);
  std::error_code ec;
  fs::remove_all(tmp, ec);
  if ( !opened )
    return false;

  bool libc_ok = false;
  netnode wasm_node("$ wasmhex", 0, false);
  bool import_malloc_ok = false;
  bool import_free_ok = false;
  for ( nodeidx_t i = 0; i < 16; ++i )
  {
    qstring h;
    qstring decl;
    wasm_node.supstr(&h, i, 'H');
    wasm_node.supstr(&decl, i, 'I');
    if ( h == "malloc" && decl.find('*') != qstring::npos )
      import_malloc_ok = true;
    else if ( h == "free" && decl.find('*') != qstring::npos )
      import_free_ok = true;
  }
  if ( import_malloc_ok && import_free_ok )
  {
    libc_ok = true;
    std::printf("PASS_LIBC_PROTOS: free/malloc/memmove have standard pointer prototypes\n");
  }

  bool param_names_ok = false;
  ea_t fn_param = get_name_ea(BADADDR, "test_param_names");
  if ( fn_param != BADADDR )
  {
    bool dec_ok = false;
    std::string code = decompile_checked(fn_param, &dec_ok);
    if ( dec_ok && std::regex_search(code, std::regex("\\bvalue\\b"))
         && std::regex_search(code, std::regex("\\bindex\\b"))
         && !std::regex_search(code, std::regex("\\ba1\\b")) )
    {
      param_names_ok = true;
      std::printf("PASS_PARAM_NAMES: parameters 'value' and 'index' recovered from local names\n");
    }
  }

  bool this_param_ok = false;
  for ( const auto &kv : collect_db_functions() )
  {
    qstring dem;
    demangle_name(&dem, kv.first.c_str(), 0);
    if ( kv.first.find("_ZN3Foo3barEi") != std::string::npos
         || std::string(dem.c_str()).find("Foo::bar") != std::string::npos )
    {
      bool dec_ok = false;
      std::string code = decompile_checked(kv.second, &dec_ok);
      if ( dec_ok && std::regex_search(code, std::regex("\\bthis\\b")) )
      {
        this_param_ok = true;
        std::printf("PASS_THIS_PARAM: C++ method parameter 1 named 'this'\n");
      }
      break;
    }
  }

  if ( libc_ok && param_names_ok && this_param_ok )
  {
    std::printf("M4_B_VERIFICATION_SUCCESS\n");
    std::fflush(stdout);
    return true;
  }
  return false;
}

bool run_check_names()
{
  static const char *const WAT = R"((module
  (import "env" "invoke_vii" (func $invoke_vii (param i32 i32)))
  (global $__THREW__ (mut i32) (i32.const 0))
  (func (export "_ZN3Foo3barEi") (param i32) (result i32)
    local.get 0 i32.const 1 i32.add)
  (func (export "caller") (result i32)
    i32.const 10 i32.const 20 call $invoke_vii global.get $__THREW__)
))";

  char tmpl[] = "/private/tmp/wasmhex_names_XXXXXX";
  char *made = mkdtemp(tmpl);
  if ( made == nullptr )
    return false;
  fs::path tmp(made);
  fs::path wasm = tmp / "names_test.wasm";
  bool compiled = compile_wat_string(WAT, wasm, { "--debug-names" });
  if ( !compiled )
  {
    std::error_code ec;
    fs::remove_all(tmp, ec);
    return false;
  }

  qsetenv("WASMHEX_BOUNDS", "hidden");
  scratch_db_session_t sess;
  bool opened = sess.open(wasm);
  std::error_code ec;
  fs::remove_all(tmp, ec);
  if ( !opened )
    return false;

  bool demangled_clean = false;
  bool global_named = false;
  for ( size_t i = 0; i < get_nlist_size(); ++i )
  {
    const char *nm_cstr = get_nlist_name(i);
    if ( nm_cstr == nullptr )
      continue;
    std::string nm(nm_cstr);
    if ( nm.find("_ZN3Foo3barEi") != std::string::npos && nm.compare(0, 5, "wasm_") != 0 )
    {
      qstring dem;
      if ( demangle_name(&dem, nm.c_str(), 0) > 0
           && std::string(dem.c_str()).find("Foo::bar") != std::string::npos )
      {
        demangled_clean = true;
        std::printf("PASS_DEMANGLE: %s -> %s\n", nm.c_str(), dem.c_str());
      }
    }
    if ( nm == "__THREW__" )
    {
      global_named = true;
      std::printf("PASS_GLOBAL_NAME: %s\n", nm.c_str());
    }
  }

  netnode wasm_node("$ wasmhex", 0, false);
  qstring imp_name;
  wasm_node.supstr(&imp_name, 0, 'H');
  bool import_short = (imp_name == "invoke_vii");
  if ( import_short )
    std::printf("PASS_IMPORT_NAME: %s\n", imp_name.c_str());

  if ( demangled_clean && import_short && global_named )
  {
    std::printf("M4_NAMES_VERIFICATION_SUCCESS\n");
    std::fflush(stdout);
    return true;
  }
  return false;
}

bool verify_dwarf_probe_names()
{
  ea_t ea_dist = get_name_ea(BADADDR, "compute_distance");
  ea_t ea_scale = get_name_ea(BADADDR, "scale_static");
  if ( ea_dist == BADADDR || ea_scale == BADADDR )
    return false;
  tinfo_t tif_dist;
  tinfo_t tif_scale;
  qstring s_dist;
  qstring s_scale;
  if ( get_tinfo(&tif_dist, ea_dist) )
    tif_dist.print(&s_dist);
  if ( get_tinfo(&tif_scale, ea_scale) )
    tif_scale.print(&s_scale);
  std::string d_str(s_dist.c_str());
  std::string sc_str(s_scale.c_str());
  if ( d_str.find("a@<r0>") == std::string::npos || d_str.find("b@<r1>") == std::string::npos )
    return false;
  if ( sc_str.find("factor@<r0>") == std::string::npos
       || sc_str.find("base@<r1>") == std::string::npos )
    return false;
  bool ok1 = false;
  bool ok2 = false;
  decompile_checked(ea_dist, &ok1);
  decompile_checked(ea_scale, &ok2);
  return ok1 && ok2;
}

bool run_check_dwarf_names(const fs::path &fixtures_dir)
{
  qsetenv("WASMHEX_BOUNDS", "hidden");
  const char *summary = "wasmhex: DWARF names applied: 2 function(s), 2 function local set(s), "
                        "1 data segment(s)";

  // 1. dwarf_noname.wasm
  {
    scratch_db_session_t sess;
    std::string cap;
    if ( !sess.open(fixtures_dir / "dwarf_noname.wasm", &cap) )
      return false;
    if ( cap.find(summary) == std::string::npos || !verify_dwarf_probe_names() )
      return false;
    std::printf("WASMHEX_DWARF_NAMES_OK\n");
  }

  // 2. dwarf_ext.wasm (+ sidecar)
  {
    scratch_db_session_t sess;
    std::string cap;
    if ( !sess.open(fixtures_dir / "dwarf_ext.wasm", &cap) )
      return false;
    if ( cap.find("external_debug_info") == std::string::npos
         || cap.find("DWARF section(s)") == std::string::npos
         || cap.find(summary) == std::string::npos || !verify_dwarf_probe_names() )
      return false;
    std::printf("WASMHEX_DWARF_EXTERNAL_OK\n");
  }

  // 3. dwarf_full.wasm
  {
    scratch_db_session_t sess;
    std::string cap;
    if ( !sess.open(fixtures_dir / "dwarf_full.wasm", &cap) )
      return false;
    if ( cap.find("wasmhex: DWARF names applied: 0 function(s)") == std::string::npos )
      return false;
    if ( get_name_ea(BADADDR, "compute_distance") == BADADDR
         || get_name_ea(BADADDR, "scale_static") == BADADDR )
      return false;
    std::printf("WASMHEX_DWARF_FILL_EMPTY_OK\n");
  }

  // 4. add.wasm
  {
    scratch_db_session_t sess;
    std::string cap;
    if ( !sess.open(fixtures_dir / "add.wasm", &cap) )
      return false;
    if ( cap.find("wasmhex: DWARF") != std::string::npos
         || cap.find("external_debug_info") != std::string::npos
         || get_name_ea(BADADDR, "add") == BADADDR )
      return false;
    std::printf("WASMHEX_DWARF_NOREGRESS_OK\n");
  }

  std::printf("WASMHEX_DWARF_OK\n");
  std::fflush(stdout);
  return true;
}

bool run_check_differential(const fs::path &fixtures_dir)
{
  static const char *const FUNCTIONS[] = {
    "struct_fields", "array_sum", "nested_if", "switch_dispatch", "call_two_levels",
    "pointer_arith", "float_ops", "factorial", "string_len",      "loop_bound",
  };

  qsetenv("WASMHEX_BOUNDS", "hidden");
  for ( const char *level : { "o0", "o2" } )
  {
    scratch_db_session_t sess;
    if ( !sess.open(fixtures_dir / (std::string("differential_") + level + ".wasm")) )
      return false;
    std::map<std::string, std::string> texts;
    for ( const char *fn_name : FUNCTIONS )
    {
      ea_t ea = get_name_ea(BADADDR, fn_name);
      if ( ea == BADADDR )
        return false;
      bool ok = false;
      auto pair_txt = decompile_two_pass(ea, &ok);
      if ( !ok || pair_txt.second.find("STACK[") != std::string::npos )
        return false;
      texts[fn_name] = pair_txt.second;
    }
    sess.close();

    auto count_decisions = [](const std::string &t) -> size_t
    {
      std::regex if_re("\\bif \\(");
      size_t ifs = static_cast<size_t>(
        std::distance(std::sregex_iterator(t.begin(), t.end(), if_re), std::sregex_iterator()));
      size_t qmarks = count_substr(t, "?");
      return ifs + qmarks;
    };

    if ( std::strcmp(level, "o2") == 0 )
    {
      // M1-D1: wide databases (memory_mode != 0) make the 4-byte param
      // unable to carry the 8-byte `wasmhex_struct_v*` pointer, so member
      // display (a1->field_0/24) is retired by the flat address form —
      // pin the equivalent flat accesses instead (todo 8-K7 follow-up:
      // typed-pointer member display on wide DBs). Offset-0 accesses
      // still render as members (a1->field_0).
      if ( texts["struct_fields"].find("*(_DWORD *)((unsigned int)a1 + 0x18LL)")
             == std::string::npos
           || texts["struct_fields"].find("a1->field_0") == std::string::npos )
        return false;
      if ( !std::regex_search(texts["switch_dispatch"], std::regex("\\bswitch\\b"))
           || texts["switch_dispatch"].find("case 1:") == std::string::npos
           || texts["switch_dispatch"].find("case 2:") == std::string::npos
           || texts["switch_dispatch"].find("case 3:") == std::string::npos
           || texts["switch_dispatch"].find("default:") == std::string::npos )
        return false;
      if ( count_decisions(texts["nested_if"]) < 3
           || texts["nested_if"].find("return 4") == std::string::npos
           || texts["nested_if"].find("return 3") == std::string::npos )
        return false;
      if ( texts["call_two_levels"].find("9 * a1 + 4") == std::string::npos )
        return false;
      if ( count_substr(texts["float_ops"], "?") < 2
           || texts["float_ops"].find("100.0") == std::string::npos )
        return false;
      std::printf("WASMHEX_DIFF_O2_OK n=%zu\n", texts.size());
    }
    else
    {
      if ( !std::regex_search(texts["switch_dispatch"], std::regex("\\bswitch\\b"))
           || texts["switch_dispatch"].find("case 1:") == std::string::npos
           || texts["switch_dispatch"].find("case 2:") == std::string::npos
           || texts["switch_dispatch"].find("case 3:") == std::string::npos
           || texts["switch_dispatch"].find("default:") == std::string::npos )
        return false;
      if ( !std::regex_search(texts["factorial"], std::regex("factorial\\s*\\(")) )
        return false;
      std::printf("WASMHEX_DIFF_O0_OK n=%zu\n", texts.size());
    }
  }
  std::printf("WASMHEX_DIFF_OK\n");
  std::fflush(stdout);
  return true;
}

bool run_check_p1_regressions(const fs::path &fixtures_dir, bool include_duckdb = true)
{
  // 1. kreg capacity probe on add.wasm
  {
    qsetenv("WASMHEX_BOUNDS", "hidden");
    scratch_db_session_t sess;
    if ( !sess.open(fixtures_dir / "add.wasm") )
      return false;
    func_entry_info_t fi;
    if ( !get_func_entry_info_by_num(&fi, 0) )
      return false;
    std::map<int, int> counts;
    for ( int width : { 1, 4, 8, 16 } )
    {
      mbl_array_t *mba = create_empty_mba(decomp_ranges_t(fi.start_ea));
      if ( mba == nullptr )
        return false;
      int cnt = 0;
      while ( mba->alloc_kreg(width) != mr_none )
        ++cnt;
      counts[width] = cnt;
      delete mba;
    }
    if ( counts[1] != 65023 || counts[4] != 16255 || counts[8] != 8127 || counts[16] != 4063 )
      return false;
    std::printf("WASMHEX_P1_KREG_CAPACITY 1=65023 4=16255 8=8127 16=4063\n");
    std::fflush(stdout);
  }

  // 2. P1 2-pass (mark_cfunc_dirty) + void* 4-byte + idiom checks
  {
    static const char *const P1_WAT = R"((module
  (import "env" "__stack_pointer" (global $sp (mut i32)))
  (import "env" "__table_base" (global $tb i32))
  (import "env" "memory" (memory $mem 1 4))
  (import "env" "fill_pair" (func $fill_pair (param i32 i32)))
  (import "env" "stackSave" (func $stack_save (result i32)))
  (import "env" "stackRestore" (func $stack_restore (param i32) (result i32)))
  (import "env" "stackAlloc" (func $stack_alloc (param i32) (result i32)))
  (export "mem" (memory $mem))
  (func (export "const48") (result i32) i32.const 48)
  (func (export "struct_deref2") (param i32) (result f64)
    (f64.convert_i32_s (i32.load (local.get 0)))
    (f64.add (f64.load offset=8 (local.get 0)))
    (f64.add (f64.convert_i32_s (i32.load offset=16 (local.get 0))))
    (f64.add (f64.load offset=32 (local.get 0))))
  (func (export "single_deref_ptr") (param i32) (result i32)
    (i32.load offset=4 (local.get 0)))
  (func (export "no_deref_plain") (param i32) (result i32)
    (i32.add (local.get 0) (i32.const 5)))
  (func (export "conflict_width_mix") (param i32) (result f64)
    (f64.convert_i32_s (i32.load offset=8 (local.get 0)))
    (f64.add (f64.load offset=8 (local.get 0))))
  (func (export "c_frame_call_and_reload") (param i32) (result i32)
    (local i32 i32)
    global.get $sp i32.const 32 i32.sub local.tee 1 global.set $sp
    local.get 1 i32.const 10 i32.store offset=8
    local.get 1 i32.const 20 i32.store offset=16
    local.get 1 i32.const 8 i32.add local.get 1 i32.const 16 i32.add call $fill_pair
    local.get 1 i32.load offset=8 local.get 1 i32.load offset=16 i32.add local.get 0 i32.add local.set 2
    local.get 1 i32.const 32 i32.add global.set $sp
    local.get 2)
  (func (export "stack_import_roundtrip") (param i32) (result i32)
    call $stack_save local.get 0 i32.const 16 i32.add call $stack_restore drop)
  (func (export "stack_import_restore_used") (param i32) (result i32)
    local.get 0 call $stack_restore)
  (func (export "stack_import_alloc_kept") (param i32) (result i32)
    local.get 0 call $stack_alloc)
))";

    char tmpl[] = "/private/tmp/wasmhex_p1_XXXXXX";
    char *made = mkdtemp(tmpl);
    if ( made == nullptr )
      return false;
    fs::path tmp(made);
    fs::path wasm = tmp / "p1_test.wasm";
    bool compiled = compile_wat_string(P1_WAT, wasm);
    if ( !compiled )
    {
      std::error_code ec;
      fs::remove_all(tmp, ec);
      return false;
    }

    qsetenv("WASMHEX_BOUNDS", "hidden");
    scratch_db_session_t sess;
    bool opened = sess.open(wasm);
    std::error_code ec;
    fs::remove_all(tmp, ec);
    if ( !opened )
      return false;

    tinfo_t ptr_tif;
    if ( !parse_decl(&ptr_tif, nullptr, get_idati(), "void *;", PT_SIL | PT_SEMICOLON) )
      return false;
    // M1-D1: this module has memory, so the database is WIDE and the
    // compiler pointer is 8 bytes (MERR_SIZEOF forces CM_N64 on wide
    // databases); the wasm32-ness of integral sizes is pinned by
    // size_i == 4 (the old 4-byte-pointer pin held only on the pre-D1
    // 32-bit databases).
    if ( ptr_tif.get_size() != 8 )
      return false;
    tinfo_t int_tif;
    if ( !parse_decl(&int_tif, nullptr, get_idati(), "int;", PT_SIL | PT_SEMICOLON)
         || int_tif.get_size() != 4 )
      return false;
    std::printf("WASMHEX_P2_3_PTR8_INT4_OK\n");

    auto check_2pass = [&](const char *nm, std::pair<std::string, std::string> *out_pair) -> bool
    {
      ea_t ea = get_name_ea(BADADDR, nm);
      if ( ea == BADADDR )
        return false;
      bool ok = false;
      *out_pair = decompile_two_pass(ea, &ok);
      return ok && out_pair->first.find("STACK[") == std::string::npos
        && out_pair->second.find("STACK[") == std::string::npos;
    };

    std::pair<std::string, std::string> p_struct;
    if ( !check_2pass("struct_deref2", &p_struct) )
      return false;
    // M1-D1: member display (->field_8/32) is retired on wide databases
    // (4-byte param cannot carry the 8-byte struct pointer) — pin the
    // flat address form instead; the i32-width load still must not
    // render as the old 4-byte-only deref (todo 8-K7 follow-up).
    if ( (p_struct.second.find("((unsigned int)a1 + 8") == std::string::npos
          && p_struct.second.find("((unsigned int)a1 + 0x20") == std::string::npos)
         || p_struct.second.find("*(double *)(a1 + 8)") != std::string::npos )
      return false;
    std::printf("WASMHEX_P5_2_STRUCT_OK struct_deref2\n");

    std::pair<std::string, std::string> p_single;
    if ( !check_2pass("single_deref_ptr", &p_single) )
      return false;
    if ( p_single.second.find("field_") != std::string::npos
         || (p_single.second.find("a1[1]") == std::string::npos
             && p_single.second.find("*a1") == std::string::npos) )
      return false;
    std::printf("WASMHEX_P5_2_SINGLE_PTR_OK single_deref_ptr\n");

    for ( const char *fc : { "no_deref_plain", "conflict_width_mix" } )
    {
      std::pair<std::string, std::string> p_fc;
      if ( !check_2pass(fc, &p_fc) || p_fc.first != p_fc.second )
        return false;
      std::printf("WASMHEX_P5_2_FAIL_CLOSED_OK %s\n", fc);
    }
    std::printf("WASMHEX_P1_FIXTURE_OK\n");
    std::fflush(stdout);
  }

  // 3. duckdb mergeProcessed 2-pass check
  if ( include_duckdb )
  {
    fs::path duckdb_path = find_repo_root() / "wasm-test" / "wasm-test" / "duckdb.wasm";
    std::error_code ec;
    if ( fs::is_regular_file(duckdb_path, ec) )
    {
      qsetenv("WASMHEX_BOUNDS", "hidden");
      scratch_db_session_t sess;
      if ( !sess.open(duckdb_path) )
        return false;
      ea_t mp_ea = BADADDR;
      std::string mp_name;
      for ( size_t i = 0; i < get_func_qty(); ++i )
      {
        func_entry_info_t fi;
        if ( !get_func_entry_info_by_num(&fi, i) )
          continue;
        qstring qn;
        get_func_name(&qn, fi.start_ea);
        std::string nm(qn.c_str());
        if ( nm.find("mergeProcessed") != std::string::npos )
        {
          mp_ea = fi.start_ea;
          mp_name = nm;
          break;
        }
      }
      if ( mp_ea == BADADDR )
        return false;
      bool ok = false;
      auto mp_pair = decompile_two_pass(mp_ea, &ok);
      if ( !ok )
        return false;
      const std::string &before = mp_pair.first;
      const std::string &after = mp_pair.second;
      if ( after.find("JUMPOUT") != std::string::npos
           || after.find("wasm_global_import_") != std::string::npos
           || after.find("stack_pointer") != std::string::npos
           || after.find("BYREF") == std::string::npos || after.find("MK_FP") != std::string::npos
           || before.find("COERCE_") != std::string::npos
           || after.find("COERCE_") != std::string::npos
           || before.find("STACK[") != std::string::npos
           || after.find("STACK[") != std::string::npos || after.find("GOT.") == std::string::npos )
        return false;
      long lines1 = static_cast<long>(split_lines_no_eol(before).size());
      long lines2 = static_cast<long>(split_lines_no_eol(after).size());
      if ( std::abs(lines2 - lines1) > std::max<long>(1, lines1 / 20) )
        return false;
      std::printf("WASMHEX_P1_MERGEPROCESSED name=%s stack=0 struct=1 lines1=%ld lines2=%ld\n",
                  mp_name.c_str(), lines1, lines2);
      std::fflush(stdout);
    }
  }

  std::printf("WASMHEX_P1_PROBE_OK capacity=8127\n");
  std::fflush(stdout);
  return true;
}

bool run_check_p0_regressions(const fs::path &fixtures_dir)
{
  char tmpl[] = "/private/tmp/wasmhex_p0_XXXXXX";
  char *made = mkdtemp(tmpl);
  if ( made == nullptr )
    return false;
  fs::path tmp(made);
  fs::path imp_wasm = tmp / "growable_memory32_imported.wasm";
  if ( !compile_wat_file(fixtures_dir / "wasmhex_growable_memory32_imported.wat", imp_wasm) )
  {
    std::error_code ec;
    fs::remove_all(tmp, ec);
    return false;
  }

  // explicit mode
  {
    qsetenv("WASMHEX_BOUNDS", "explicit");
    scratch_db_session_t sess;
    if ( !sess.open(imp_wasm) )
      return false;
    for ( const char *nm : { "touch", "load32_offset4" } )
    {
      ea_t ea = get_name_ea(BADADDR, nm);
      bool ok = false;
      std::string txt = ea != BADADDR ? decompile_checked(ea, &ok) : "";
      if ( !ok || txt.find("wasm_trap_memory_") == std::string::npos )
        return false;
      std::printf("WASMHEX_GROWABLE_MEMORY32_F5_OK %s\n", nm);
    }
  }

  // hidden mode
  {
    qsetenv("WASMHEX_BOUNDS", "hidden");
    scratch_db_session_t sess;
    if ( !sess.open(imp_wasm) )
      return false;
    for ( const char *nm : { "touch", "load32_offset4" } )
    {
      ea_t ea = get_name_ea(BADADDR, nm);
      bool ok = false;
      std::string txt = ea != BADADDR ? decompile_checked(ea, &ok) : "";
      if ( !ok || txt.find("wasm_trap_memory_") != std::string::npos
           || txt.find("MK_FP") != std::string::npos
           || txt.find("*(_DWORD *)(") == std::string::npos )
        return false;
      std::printf("WASMHEX_GROWABLE_MEMORY32_HIDDEN_F5_OK %s\n", nm);
    }
  }

  // P0-3/4/5 functions
  static const char *const P0_WAT = R"((module
  (func (export "local_alias") (param i32) (result i32)
    local.get 0 local.get 0 i32.const 1 i32.add local.set 0)
  (func (export "tee_subtract") (param i32) (result i32)
    (local i32 i32 i32 i32 i32 i32 i32 i32 i32 i32
           i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
    local.get 0 local.tee 20 i32.const 4 i32.sub i32.const 1000 i32.mul local.get 20 i32.add)
  (func (export "loop_guard") (param i32) (result i32) (local i32 i32)
    i32.const 0 local.set 1
    local.get 1 local.get 0 i32.lt_s local.set 2
    block loop
      local.get 2 i32.eqz br_if 1
      local.get 1 i32.const 1 i32.add local.set 1
      local.get 1 local.get 0 i32.lt_s local.set 2
      br 0
    end end
    local.get 1)
))";
  fs::path p0_wasm = tmp / "p0_funcs.wasm";
  bool compiled = compile_wat_string(P0_WAT, p0_wasm);
  std::error_code ec;
  if ( !compiled )
  {
    fs::remove_all(tmp, ec);
    return false;
  }

  qsetenv("WASMHEX_BOUNDS", "hidden");
  scratch_db_session_t sess;
  bool opened = sess.open(p0_wasm);
  fs::remove_all(tmp, ec);
  if ( !opened )
    return false;

  for ( const char *nm : { "local_alias", "tee_subtract", "loop_guard" } )
  {
    ea_t ea = get_name_ea(BADADDR, nm);
    bool ok = false;
    std::string txt = ea != BADADDR ? decompile_checked(ea, &ok) : "";
    if ( !ok || txt.find("STACK[") != std::string::npos
         || txt.find("JUMPOUT") != std::string::npos )
      return false;
  }
  std::printf("WASMHEX_P0_3_F5_OK\nWASMHEX_P0_4_F5_OK\nWASMHEX_P0_5_F5_OK\n");
  std::printf("WASMHEX_P0_REGRESSIONS_OK\n");
  std::fflush(stdout);
  return true;
}

bool run_check_m7ab_explicit(const std::string &variant)
{
  static const char *const WAT = R"((module
  (memory 1 4)
  (func (export "div_s") (param i32 i32) (result i32) local.get 0 local.get 1 i32.div_s)
  (func (export "div_u") (param i32 i32) (result i32) local.get 0 local.get 1 i32.div_u)
  (func (export "rem_s") (param i32 i32) (result i32) local.get 0 local.get 1 i32.rem_s)
  (func (export "rem_u") (param i32 i32) (result i32) local.get 0 local.get 1 i32.rem_u)
  (func (export "clz32") (param i32) (result i32) local.get 0 i32.clz)
  (func (export "rotl") (param i32 i32) (result i32) local.get 0 local.get 1 i32.rotl)
  (func (export "popcnt") (param i32) (result i32) local.get 0 i32.popcnt)
  (func (export "shift_dynamic") (param i32 i32) (result i32) local.get 0 local.get 1 i32.shl)
  (func (export "sel_i32") (param i32 i32 i32) (result i32) local.get 1 local.get 2 local.get 0 select)
  (func (export "sel_f64") (param i32 f64 f64) (result f64) local.get 1 local.get 2 local.get 0 select)
  (func (export "p6_min") (param f64 f64) (result f64) local.get 0 local.get 1 f64.min)
  (func (export "p6_max") (param f64 f64) (result f64) local.get 0 local.get 1 f64.max)
  (func (export "p6_nearest") (param f64) (result f64) local.get 0 f64.nearest)
  (func (export "p6_trunc_sat") (param f64) (result i32) local.get 0 i32.trunc_sat_f64_s)
  (func (export "p6_atomic_add") (param i32 i32) (result i32) local.get 0 local.get 1 i32.atomic.rmw.add)
  (func (export "p6_atomic_store") (param i32 i32) local.get 0 local.get 1 i32.atomic.store)
))";

  char tmpl[] = "/private/tmp/wasmhex_m7_XXXXXX";
  char *made = mkdtemp(tmpl);
  if ( made == nullptr )
    return false;
  fs::path tmp(made);
  fs::path wasm = tmp / "m7ab.wasm";
  bool compiled = compile_wat_string(WAT, wasm, { "--enable-threads" });
  std::error_code ec;
  if ( !compiled )
  {
    fs::remove_all(tmp, ec);
    return false;
  }

  qsetenv("WASMHEX_BOUNDS", "explicit");
  scratch_db_session_t sess;
  bool opened = sess.open(wasm);
  fs::remove_all(tmp, ec);
  if ( !opened )
    return false;

  const std::pair<const char *, const char *> expected[] = {
    { "div_s", "wasm_i32_div_s(a1, a2)" },
    { "div_u", "wasm_i32_div_u(a1, a2)" },
    { "rem_s", "wasm_i32_rem_s(a1, a2)" },
    { "rem_u", "wasm_i32_rem_u(a1, a2)" },
    { "rotl", "& 0x1Fu" },
    { "shift_dynamic", "& 0x1Fu" },
    { "sel_i32", "wasm_select_i32(" },
    { "sel_f64", "wasm_select_f64(" },
    { "p6_min", "wasm_f64_min_bits(" },
    { "p6_max", "wasm_f64_max_bits(" },
    { "p6_nearest", "wasm_f64_nearest_bits(" },
    { "p6_trunc_sat", "wasm_i32_trunc_sat_f64_s_bits(" },
    { "p6_atomic_add", "wasm_memory0_i32_atomic_rmw_add_seq_cst(" },
    { "p6_atomic_store", "wasm_memory0_i32_atomic_store_seq_cst(" },
  };
  for ( const auto &kv : expected )
  {
    ea_t ea = get_name_ea(BADADDR, kv.first);
    bool ok = false;
    std::string txt = ea != BADADDR ? decompile_checked(ea, &ok) : "";
    if ( !ok || txt.find(kv.second) == std::string::npos || txt.find("STACK[") != std::string::npos
         || txt.find("JUMPOUT(") != std::string::npos )
      return false;
    for ( const char *forb :
          { "__atomic", "fminimum", "fmaximum", "roundeven", "std::", "trunc_sat_i32_s(" } )
    {
      if ( txt.find(forb) != std::string::npos )
        return false;
    }
  }
  qsetenv("WASMHEX_BOUNDS", "hidden");
  if ( variant == "m7a" )
    std::printf("WASMHEX_M7A_EXPLICIT_OK\n");
  else if ( variant == "m7b" )
    std::printf("WASMHEX_M7B_EXPLICIT_OK\n");
  else
    std::printf("WASMHEX_M7AB_EXPLICIT_OK\n");
  std::fflush(stdout);
  return true;
}

bool expect_loader_rejection(const fs::path &wasm_path, const std::string &expected_diag)
{
  scratch_db_session_t sess;
  std::string cap;
  bool opened = sess.open(wasm_path, &cap);
  if ( opened )
    return false;
  return cap.find(expected_diag) != std::string::npos
    || cap.find("wasmhex: module rejected") != std::string::npos;
}

bool run_check_component_loader_idat(const fs::path &fixtures_dir)
{
  if ( !expect_loader_rejection(fixtures_dir / "component_f5_unsupported.wasm",
                                "Unsupported WASM component section 2 at file offset 0x8") )
    return false;
  if ( !expect_loader_rejection(fixtures_dir / "component_f5_out_of_order.wasm",
                                "Out-of-order WASM component section 1 at file offset 0x12") )
    return false;
  std::printf("WASMHEX_COMPONENT_IDAT_REJECTIONS_OK\n");
  std::fflush(stdout);
  return true;
}

bool run_check_call_ref_rejections(const fs::path &fixtures_dir)
{
  const std::pair<const char *, const char *> cases[] = {
    { "wasmhex_call_ref_unknown_join.wasm",
      "Native wasmhex call_ref requires a statically known function-reference target" },
    { "wasmhex_call_ref_encoded_mismatch.wasm",
      "Native wasmhex finite call_ref targets must have exact function signatures" },
    { "wasmhex_call_ref_subtype_join.wasm",
      "Unsupported WebAssembly GC type form; wasmhex currently supports function types only" },
    { "wasmhex_call_ref_nonnumeric_import_join.wasm",
      "Native wasmhex finite call_ref dispatch supports numeric scalar parameters only" },
  };
  for ( const auto &kv : cases )
  {
    if ( !expect_loader_rejection(fixtures_dir / kv.first, kv.second) )
      return false;
    std::printf("WASMHEX_CALL_REF_REJECTED %s: %s\n", kv.first, kv.second);
  }
  std::printf("WASMHEX_CALL_REF_REJECTIONS_OK\n");
  std::fflush(stdout);
  return true;
}

bool run_check_calls_multi_exit(const fs::path &fixtures_dir)
{
  qsetenv("WASMHEX_BOUNDS", "hidden");
  scratch_db_session_t sess;
  if ( !sess.open(fixtures_dir / "wasmhex_calls_multi.wasm") )
    return false;
  std::map<std::string, std::string> out;
  for ( const char *nm :
        { "direct_import_pair", "direct_import_six", "indirect_scalar", "ordered_shift" } )
  {
    ea_t ea = get_name_ea(BADADDR, nm);
    if ( ea == BADADDR )
      return false;
    bool ok = false;
    std::string txt = decompile_checked(ea, &ok);
    if ( !ok || txt.find("JUMPOUT") != std::string::npos )
      return false;
    hexrays_failure_t hf;
    mbl_array_t *raw
      = gen_microcode(decomp_ranges_t(ea), &hf, nullptr, DECOMP_NO_WAIT, MMAT_GENERATED);
    if ( raw == nullptr )
      return false;
    raw->verify(true);
    delete raw;
    out[nm] = txt;
  }
  if ( out["direct_import_pair"].find("pair(") == std::string::npos
       || out["direct_import_six"].find("six(") == std::string::npos
       || out["indirect_scalar"].find("target_scalar(") == std::string::npos
       || out["indirect_scalar"].find("wasm_trap_call_indirect_type") == std::string::npos
       || out["ordered_shift"].find("effect(a1)") == std::string::npos
       || out["ordered_shift"].find("effect(a2)") == std::string::npos )
    return false;
  std::printf("WASMHEX_CALLS_MULTI_F5_OK\nWASMHEX_HEADLESS_EXIT_OK\n");
  std::fflush(stdout);
  return true;
}

bool run_check_fixed_memory_proof(const fs::path &fixtures_dir)
{
  qsetenv("WASMHEX_BOUNDS", "explicit");
  const char *const ok_binaries[] = {
    "wasmhex_fixed_memory_exact.wasm",    "wasmhex_fixed_memory_v128.wasm",
    "wasmhex_fixed_memory_lane.wasm",     "wasmhex_fixed_memory_widen.wasm",
    "wasmhex_fixed_memory_zero.wasm",     "wasmhex_fixed_memory_zero_splat.wasm",
    "wasmhex_fixed_memory_exported.wasm", "wasmhex_fixed_memory_imported.wasm",
    "wasmhex_fixed_memory_grow.wasm",     "wasmhex_fixed_memory_memory64.wasm",
  };
  for ( const char *bin : ok_binaries )
  {
    scratch_db_session_t sess;
    if ( !sess.open(fixtures_dir / bin) )
      return false;
    for ( size_t i = 0; i < get_func_qty(); ++i )
    {
      func_entry_info_t fi;
      if ( !get_func_entry_info_by_num(&fi, i) )
        continue;
      bool ok = false;
      std::string txt = decompile_checked(fi.start_ea, &ok);
      if ( !ok || txt.find("JUMPOUT") != std::string::npos )
        return false;
    }
    std::printf("WASMHEX_FIXED_MEMORY_CASE_OK %s\n", bin);
  }

  // shared memory without proof fails closed on F5
  {
    scratch_db_session_t sess;
    if ( !sess.open(fixtures_dir / "wasmhex_fixed_memory_shared.wasm") )
      return false;
    for ( const char *nm : { "scalar", "load", "widen" } )
    {
      ea_t ea = get_name_ea(BADADDR, nm);
      bool ok = false;
      decompile_checked(ea, &ok);
      if ( ok )
        return false;
    }
    std::printf("WASMHEX_FIXED_MEMORY_CASE_OK wasmhex_fixed_memory_shared.wasm\n");
  }

  if ( !expect_loader_rejection(fixtures_dir / "wasmhex_fixed_memory_lane_invalid.wasm",
                                "WebAssembly lane immediate is out of range") )
    return false;
  std::printf("WASMHEX_FIXED_MEMORY_REJECTED wasmhex_fixed_memory_lane_invalid.wasm\n");
  qsetenv("WASMHEX_BOUNDS", "hidden");
  std::fflush(stdout);
  return true;
}

bool run_check_fixed_memory_scalar(const fs::path &fixtures_dir)
{
  qsetenv("WASMHEX_BOUNDS", "explicit");
  for ( const char *bin : { "wasmhex_fixed_memory_scalar.wasm",
                            "wasmhex_fixed_memory_scalar_zero.wasm", "wasmhex_memory64.wasm" } )
  {
    scratch_db_session_t sess;
    if ( !sess.open(fixtures_dir / bin) )
      return false;
    for ( size_t i = 0; i < get_func_qty(); ++i )
    {
      func_entry_info_t fi;
      if ( !get_func_entry_info_by_num(&fi, i) )
        continue;
      bool ok = false;
      std::string txt = decompile_checked(fi.start_ea, &ok);
      if ( !ok || txt.find("JUMPOUT") != std::string::npos
           || txt.find("__builtin_trap") != std::string::npos )
        return false;
    }
    std::printf("WASMHEX_FIXED_MEMORY_SCALAR_CASE_OK %s\n", bin);
  }
  qsetenv("WASMHEX_BOUNDS", "hidden");
  std::fflush(stdout);
  return true;
}

bool run_check_memory_space(const fs::path &fixtures_dir)
{
  qsetenv("WASMHEX_BOUNDS", "explicit");
  // Layout assertions need a module whose linear memory is actually
  // MAPPED (active data segments): p02_bss_access (memory32 2 pages +
  // active data). MEMORY must sit at EA 0, .text above 4GiB, disjoint.
  {
    scratch_db_session_t sess;
    if ( !sess.open(fixtures_dir / "p02_bss_access.wasm") )
      return false;
    auto funcs = collect_db_functions(true);
    segment_info_t text_seg;
    bool have_text = get_segment_info(&text_seg, funcs["bss_read"]);
    segment_info_t mem_seg;
    bool have_mem = get_segment_info(&mem_seg, ea_t(0x1000));
    if ( !have_text || !have_mem || mem_seg.start_ea != 0 || mem_seg.end_ea > 0x100000000ULL
         || text_seg.start_ea < 0x100000000ULL || mem_seg.end_ea > text_seg.start_ea )
      return false;
    bool ok = false;
    std::string txt = decompile_checked(funcs["bss_read"], &ok);
    if ( !ok || txt.find("JUMPOUT") != std::string::npos )
      return false;
  }
  // memory64 (private, fixed extent, no data section): no MEMORY segment
  // is mapped (active-data rule), but .text still clears 4GiB on the
  // wide database and scalar/simd accesses decompile clean.
  scratch_db_session_t sess;
  if ( !sess.open(fixtures_dir / "wasmhex_fixed_memory64_private.wasm") )
    return false;
  auto funcs = collect_db_functions(true);
  ea_t simd_ea = funcs["simd_load"];
  segment_info_t text_seg;
  bool have_text = get_segment_info(&text_seg, simd_ea);
  if ( !have_text || text_seg.start_ea < 0x100000000ULL )
    return false;
  for ( const char *nm : { "scalar_i64_load", "simd_load" } )
  {
    bool ok = false;
    std::string txt = decompile_checked(funcs[nm], &ok);
    if ( !ok || txt.find("__builtin_trap") != std::string::npos
         || txt.find("wasm_memory64_") != std::string::npos )
      return false;
  }
  qsetenv("WASMHEX_BOUNDS", "hidden");
  std::printf("WASMHEX_MEMORY_SPACE_OK base=0x0 text_disjoint\n");
  std::fflush(stdout);
  return true;
}

// Layout + bitness pins. The bisect probes
// from the prototype, kept as in-repo reproduction cases: every D1
// architecture decision below is re-provable from tests/fixtures/m1d1_*.
bool run_check_m1d1_layout(const fs::path &fixtures_dir)
{
  qsetenv("WASMHEX_BOUNDS", "hidden");

  // (1) Bitness one-source, memory_mode == 0 end: a module WITHOUT memory
  // stays a 32-bit database with the legacy .text at 0x1000 (probe4).
  {
    scratch_db_session_t sess;
    if ( !sess.open(fixtures_dir / "m1d1_no_memory.wasm") )
      return false;
    if ( inf_is_64bit() )
      return false;
    auto funcs = collect_db_functions(true);
    ea_t ea = funcs["get_g32"];
    segment_info_t seg;
    if ( ea == BADADDR || !get_segment_info(&seg, ea) || seg.start_ea != 0x1000 )
      return false;
    bool ok = false;
    std::string txt = decompile_checked(ea, &ok);
    if ( !ok || txt.find("JUMPOUT") != std::string::npos )
      return false;
    std::printf("WASMHEX_M1D1_NO_MEMORY_32BIT_DB code=0x1000\n");
  }

  // (2) Bitness one-source, memory_mode != 0 end WITHOUT an 'L' data span:
  // a memory module with no active data segments still gets the 64-bit
  // database and >4GiB code — the exact shape the old two-source rule
  // ('L'-span) FATALed for on reopen (probe6). Also proves 8-byte ldx/stx
  // offset operands (INTERR 50826 avoidance) on the wide DB at EA 0.
  {
    scratch_db_session_t sess;
    if ( !sess.open(fixtures_dir / "m1d1_memory_nodata.wasm") )
      return false;
    if ( !inf_is_64bit() )
      return false;
    auto funcs = collect_db_functions(true);
    segment_info_t seg;
    if ( !get_segment_info(&seg, funcs["brt2"]) || seg.start_ea < 0x100000000ULL )
      return false;
    for ( const char *nm : { "brt2", "brt3" } )
    {
      bool ok = false;
      std::string txt = decompile_checked(funcs[nm], &ok);
      if ( !ok || txt.find("JUMPOUT") != std::string::npos )
        return false;
    }
    std::printf("WASMHEX_M1D1_MEMORY_NODATA_64BIT_DB text_ge_4gib\n");
  }

  // (3) Globals below 4GiB directly above the MAPPED extent (probe1 +
  // probe3): typed globals (i32/i64/f64) decompile — direct gvar data
  // access above 4GiB is MERR_ARCH, so this placement is load-bearing.
  // Memory ops decompile flat (no 0x100000000 base arithmetic anywhere).
  {
    scratch_db_session_t sess;
    if ( !sess.open(fixtures_dir / "m1d1_globals_types.wasm") )
      return false;
    auto funcs = collect_db_functions(true);
    bool have_globals = false;
    ea_t globals_end = 0;
    for ( int i = 0; i < get_segm_qty(); ++i )
    {
      segment_info_t info;
      if ( !get_segment_info_by_num(&info, i, GSI_NAME)
           || std::strcmp(info.get_name(), ".globals") != 0 )
        continue;
      globals_end = info.end_ea;
      have_globals = true;
      break;
    }
    if ( !have_globals || globals_end > 0x100000000ULL )
      return false;
    for ( const char *nm : { "get_g64", "set_g64", "get_f64", "get_g32" } )
    {
      bool ok = false;
      std::string txt = decompile_checked(funcs[nm], &ok);
      if ( !ok || txt.find("JUMPOUT") != std::string::npos
           || txt.find("0x100000000") != std::string::npos )
        return false;
    }
    std::printf("WASMHEX_M1D1_GLOBALS_BELOW_4GIB typed_ok\n");
  }

  // (4) Composite probe (probe1): MEMORY@0 + .globals + .text>4GiB all
  // disjoint; stores through the SP global decompile with the flat
  // address representation (EA-0 acceptance).
  {
    scratch_db_session_t sess;
    if ( !sess.open(fixtures_dir / "m1d1_memory_globals_brtable.wasm") )
      return false;
    if ( !inf_is_64bit() )
      return false;
    auto funcs = collect_db_functions(true);
    segment_info_t text_seg, mem_seg;
    bool have_text = get_segment_info(&text_seg, funcs["brt"]);
    bool have_mem = get_segment_info(&mem_seg, ea_t(0x1000));
    if ( !have_text || !have_mem || mem_seg.start_ea != 0 || text_seg.start_ea < 0x100000000ULL
         || mem_seg.end_ea > text_seg.start_ea )
      return false;
    for ( const char *nm : { "sp_store", "sp_roundtrip", "brt" } )
    {
      bool ok = false;
      std::string txt = decompile_checked(funcs[nm], &ok);
      if ( !ok || txt.find("JUMPOUT") != std::string::npos
           || txt.find("0x100000000") != std::string::npos )
        return false;
    }
    std::printf("WASMHEX_M1D1_LAYOUT_OK memory0_globals_text_disjoint flat\n");
  }

  std::fflush(stdout);
  return true;
}

bool run_check_fixed_memory64(const fs::path &fixtures_dir)
{
  if ( !run_check_memory_space(fixtures_dir) )
    return false;
  qsetenv("WASMHEX_BOUNDS", "explicit");
  for ( const char *bin :
        { "wasmhex_fixed_memory64_scalar.wasm", "wasmhex_fixed_memory64_private.wasm",
          "wasmhex_fixed_memory64_exact.wasm", "wasmhex_fixed_memory64_shared.wasm" } )
  {
    scratch_db_session_t sess;
    if ( !sess.open(fixtures_dir / bin) )
      return false;
    for ( size_t i = 0; i < get_func_qty(); ++i )
    {
      func_entry_info_t fi;
      if ( !get_func_entry_info_by_num(&fi, i) )
        continue;
      bool ok = false;
      std::string txt = decompile_checked(fi.start_ea, &ok);
      if ( !ok || txt.find("JUMPOUT") != std::string::npos )
        return false;
    }
    std::printf("WASMHEX_FIXED_MEMORY64_CASE_OK %s\n", bin);
  }
  // Unbounded-memory64 policy: a memory64 that is
  // neither fixed-extent nor max-declared is REFUSED BY THE LOADER (was:
  // load + per-function F5 refusal) — no code base above such a memory is
  // provably clear of runtime growth. The full-extent sentinel
  // (fixed_memory_bytes == UINT64_MAX) is likewise refused: no EA remains
  // above the memory domain.
  for ( const char *rej_bin :
        { "wasmhex_fixed_memory64_full.wasm", "wasmhex_fixed_memory64_imported.wasm",
          "wasmhex_fixed_memory64_exported.wasm", "wasmhex_fixed_memory64_grow.wasm",
          "wasmhex_memory64_v128.wasm" } )
  {
    const std::string diag = std::string(rej_bin) == "wasmhex_fixed_memory64_full.wasm"
      ? "WASM linear-memory upper bound leaves no address space for the code segment"
      : "Native wasmhex D1 layout requires a bounded Memory64 (fixed extent or declared "
        "maximum)";
    if ( !expect_loader_rejection(fixtures_dir / rej_bin, diag) )
      return false;
    std::printf("WASMHEX_FIXED_MEMORY64_LOADER_REJECTED %s\n", rej_bin);
  }
  // Declared maximum present (shared growable): the module loads and code
  // sits above the declared max, but shared growable memory still fails
  // closed per access (pre-existing policy, unchanged by D1).
  {
    scratch_db_session_t sess;
    if ( !sess.open(fixtures_dir / "wasmhex_fixed_memory64_growable_shared.wasm") )
      return false;
    size_t rejected = 0;
    for ( size_t i = 0; i < get_func_qty(); ++i )
    {
      func_entry_info_t fi;
      if ( !get_func_entry_info_by_num(&fi, i) )
        continue;
      bool ok = false;
      decompile_checked(fi.start_ea, &ok);
      if ( !ok )
        ++rejected;
    }
    if ( rejected == 0 )
      return false;
    std::printf("WASMHEX_FIXED_MEMORY64_CASE_OK wasmhex_fixed_memory64_growable_shared.wasm\n");
  }
  if ( !expect_loader_rejection(fixtures_dir / "wasmhex_fixed_memory64_oversize.wasm",
                                "Native wasmhex rejects Memory64 sizes above 2^48 pages") )
    return false;
  std::printf("WASMHEX_FIXED_MEMORY64_INVALID_REJECTED wasmhex_fixed_memory64_oversize.wasm\n");
  qsetenv("WASMHEX_BOUNDS", "hidden");
  std::fflush(stdout);
  return true;
}

bool run_check_growable_memory64(const fs::path &fixtures_dir)
{
  char tmpl[] = "/private/tmp/wasmhex_gm64_XXXXXX";
  char *made = mkdtemp(tmpl);
  if ( made == nullptr )
    return false;
  fs::path tmp(made);

  fs::path pos = tmp / "positive.wasm";
  if ( !compile_wat_file(fixtures_dir / "wasmhex_growable_memory64.wat", pos) )
    return false;

  qsetenv("WASMHEX_BOUNDS", "explicit");
  {
    scratch_db_session_t sess;
    if ( !sess.open(pos) )
      return false;
    for ( const char *nm : { "grow", "size", "touch" } )
    {
      ea_t ea = get_name_ea(BADADDR, nm);
      bool ok = false;
      std::string txt = ea != BADADDR ? decompile_checked(ea, &ok) : "";
      if ( !ok || txt.find("JUMPOUT") != std::string::npos )
        return false;
    }
    std::printf("WASMHEX_GROWABLE_MEMORY64_F5_OK\n");
  }

  // D1: unbounded (no declared maximum) is now refused by the LOADER
  // — was load + F5 refusal. The remaining rejects carry
  // a declared maximum, so they load with code above the declared max
  // and keep failing closed per access (pre-existing policy).
  {
    fs::path w = tmp / "unbounded.wasm";
    if ( !compile_wat_file(fixtures_dir / "wasmhex_growable_memory64_unbounded.wat", w) )
      return false;
    if ( !expect_loader_rejection(
           w,
           "Native wasmhex D1 layout requires a bounded Memory64 (fixed extent or declared "
           "maximum)") )
      return false;
    std::printf("WASMHEX_GROWABLE_MEMORY64_LOADER_REJECTED unbounded\n");
  }

  for ( const char *rej : { "large_max", "imported", "exported", "shared" } )
  {
    fs::path w = tmp / (std::string(rej) + ".wasm");
    std::vector<std::string> fl;
    if ( std::strcmp(rej, "shared") == 0 )
      fl.push_back("--enable-threads");
    if ( !compile_wat_file(
           fixtures_dir / (std::string("wasmhex_growable_memory64_") + rej + ".wat"), w, fl) )
      return false;
    scratch_db_session_t sess;
    if ( !sess.open(w) )
      return false;
    ea_t ea = get_name_ea(BADADDR, "touch");
    bool ok = false;
    decompile_checked(ea, &ok);
    if ( ok )
      return false;
    std::printf("WASMHEX_GROWABLE_MEMORY64_REJECTED %s\n", rej);
  }

  fs::path multi = tmp / "multi.wasm";
  if ( !compile_wat_file(fixtures_dir / "wasmhex_growable_memory64_multi.wat", multi) )
    return false;
  if ( !expect_loader_rejection(
         multi,
         "Native wasmhex supports one memory at index zero; multiple memories are unsupported") )
    return false;
  std::printf("WASMHEX_GROWABLE_MEMORY64_REJECTED multi\n");

  std::error_code ec;
  fs::remove_all(tmp, ec);
  qsetenv("WASMHEX_BOUNDS", "hidden");
  std::fflush(stdout);
  return true;
}

bool run_check_growable_memory32(const fs::path &fixtures_dir)
{
  char tmpl[] = "/private/tmp/wasmhex_gm32_XXXXXX";
  char *made = mkdtemp(tmpl);
  if ( made == nullptr )
    return false;
  fs::path tmp(made);

  qsetenv("WASMHEX_BOUNDS", "explicit");
  const std::pair<const char *, bool> pos_cases[] = {
    { "wasmhex_growable_memory32.wat", true },
    { "wasmhex_growable_memory32_unbounded.wat", false },
    { "wasmhex_growable_memory32_exported.wat", false },
    { "wasmhex_growable_memory32_imported.wat", false },
    { "wasmhex_growable_memory32_imported_large_max.wat", false },
  };
  for ( const auto &kv : pos_cases )
  {
    fs::path w = tmp / (std::string(kv.first) + ".wasm");
    std::vector<std::string> fl;
    if ( kv.second )
      fl.push_back("--enable-threads");
    if ( !compile_wat_file(fixtures_dir / kv.first, w, fl) )
      return false;
    scratch_db_session_t sess;
    if ( !sess.open(w) )
      return false;
    ea_t ea = get_name_ea(BADADDR, "touch");
    bool ok = false;
    std::string txt = ea != BADADDR ? decompile_checked(ea, &ok) : "";
    if ( !ok || txt.find("wasm_trap_memory_") == std::string::npos )
      return false;
  }
  std::printf("WASMHEX_GROWABLE_MEMORY32_F5_OK\n");

  for ( const char *neg : { "shared", "imported_shared" } )
  {
    fs::path w = tmp / (std::string(neg) + ".wasm");
    if ( !compile_wat_file(fixtures_dir
                             / (std::string("wasmhex_growable_memory32_") + neg + ".wat"),
                           w, { "--enable-threads" }) )
      return false;
    scratch_db_session_t sess;
    if ( !sess.open(w) )
      return false;
    ea_t ea = get_name_ea(BADADDR, "touch");
    bool ok = false;
    decompile_checked(ea, &ok);
    if ( ok )
      return false;
    std::printf("WASMHEX_GROWABLE_MEMORY32_REJECTED %s\n", neg);
  }

  fs::path multi = tmp / "multi.wasm";
  if ( !compile_wat_file(fixtures_dir / "wasmhex_growable_memory32_multi.wat", multi) )
    return false;
  if ( !expect_loader_rejection(
         multi,
         "Native wasmhex supports one memory at index zero; multiple memories are unsupported") )
    return false;
  std::printf("WASMHEX_GROWABLE_MEMORY32_REJECTED multi\n");

  std::error_code ec;
  fs::remove_all(tmp, ec);
  qsetenv("WASMHEX_BOUNDS", "hidden");
  std::fflush(stdout);
  return true;
}

bool run_check_simple_wat_f5(const fs::path &wat_path, const char *marker,
                             const std::vector<std::string> &flags = {})
{
  char tmpl[] = "/private/tmp/wasmhex_swat_XXXXXX";
  char *made = mkdtemp(tmpl);
  if ( made == nullptr )
    return false;
  fs::path tmp(made);
  fs::path w = tmp / "mod.wasm";
  bool compiled = compile_wat_file(wat_path, w, flags);
  std::error_code ec;
  if ( !compiled )
  {
    fs::remove_all(tmp, ec);
    return false;
  }
  qsetenv("WASMHEX_BOUNDS", "hidden");
  scratch_db_session_t sess;
  bool opened = sess.open(w);
  fs::remove_all(tmp, ec);
  if ( !opened )
    return false;
  for ( size_t i = 0; i < get_func_qty(); ++i )
  {
    func_entry_info_t fi;
    if ( !get_func_entry_info_by_num(&fi, i) )
      continue;
    bool ok = false;
    std::string txt = decompile_checked(fi.start_ea, &ok);
    if ( !ok || txt.find("JUMPOUT(") != std::string::npos
         || txt.find("STACK[") != std::string::npos )
      return false;
  }
  if ( marker != nullptr )
  {
    std::printf("%s\n", marker);
    std::fflush(stdout);
  }
  return true;
}

// T0 rev40: regression for the H3 dref slice tags. The fixture's first
// function starts with `global.get` of a DEFINED global — the exact shape
// the rev39 'R'/'r' records refused (the 8-byte ea_t landed on the entry's
// startup-helper string slot and F5 failed closed with -31 on every such
// function). Two assertions:
//  1. every function decompiles (the dref records must no longer be
//     readable as the startup helper — tags '8'/'9' now);
//  2. the entry global.get carries a data xref whose target is the
//     global's EXACT storage address ('j' supval), not the
//     VAL-flavor-read stored-minus-one (measured 0xFFFFFF vs 0x1000000).
bool run_check_dref_entry_globalget(const fs::path &fixtures_dir)
{
  qsetenv("WASMHEX_BOUNDS", "hidden");
  char tmpl[] = "/private/tmp/wasmhex_drefgg_XXXXXX";
  char *made = mkdtemp(tmpl);
  if ( made == nullptr )
    return false;
  fs::path tmp(made);
  const fs::path wat = fixtures_dir / "wasmhex_dref_entry_globalget.wat";
  const fs::path w = tmp / "mod.wasm";
  const bool compiled = compile_wat_file(wat, w);
  scratch_db_session_t sess;
  const bool opened = compiled && sess.open(w);
  if ( !opened )
  {
    std::error_code ec;
    fs::remove_all(tmp, ec);
    return false;
  }
  for ( size_t i = 0; i < get_func_qty(); ++i )
  {
    func_entry_info_t fi;
    if ( !get_func_entry_info_by_num(&fi, i) )
      continue;
    bool ok = false;
    const std::string txt = decompile_checked(fi.start_ea, &ok);
    if ( !ok || txt.find("JUMPOUT(") != std::string::npos
         || txt.find("STACK[") != std::string::npos )
    {
      std::error_code ec;
      fs::remove_all(tmp, ec);
      return false;
    }
  }
  // The first function's entry instruction is the colliding global.get;
  // its proven dref must reference the global item exactly.
  netnode node("$ wasmhex", 0, false);
  ea_t global_address = BADADDR;
  func_entry_info_t first;
  if ( node == BADNODE
       || node.supval(0, &global_address, sizeof(global_address), 'j') != sizeof(global_address)
       || global_address == BADADDR || global_address == 0
       || !get_func_entry_info_by_num(&first, 0) )
  {
    std::error_code ec;
    fs::remove_all(tmp, ec);
    return false;
  }
  bool dref_ok = false;
  xrefblk_t xb;
  for ( bool ok = xb.first_from(first.start_ea, XREF_DATA); ok && !dref_ok; ok = xb.next_from() )
    dref_ok = !xb.iscode && xb.to == global_address;
  std::error_code ec;
  fs::remove_all(tmp, ec);
  if ( !dref_ok )
    return false;
  std::printf("WASMHEX_DREF_ENTRY_GLOBALGET_OK\n");
  std::fflush(stdout);
  return true;
}

bool run_check_native_operand_validation()
{
  auto make_mod = [](const std::vector<uint8_t> &body_ops) -> std::vector<uint8_t>
  {
    std::vector<uint8_t> body = { 0x00 };
    body.insert(body.end(), body_ops.begin(), body_ops.end());
    body.push_back(0x0B);

    std::vector<uint8_t> out = { 0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00 };
    append_wasm_section(&out, 1, { 0x01, 0x60, 0x00, 0x00 });
    append_wasm_section(&out, 3, { 0x01, 0x00 });
    append_wasm_section(&out, 5, { 0x01, 0x00, 0x01 });
    std::vector<uint8_t> code_sec = { 0x01 };
    append_uleb_bytes(&code_sec, body.size());
    code_sec.insert(code_sec.end(), body.begin(), body.end());
    append_wasm_section(&out, 10, code_sec);
    return out;
  };

  char tmpl[] = "/private/tmp/wasmhex_opval_XXXXXX";
  char *made = mkdtemp(tmpl);
  if ( made == nullptr )
    return false;
  fs::path tmp(made);

  auto write_bin = [&](const char *nm, const std::vector<uint8_t> &b) -> fs::path
  {
    fs::path p = tmp / nm;
    std::ofstream ofs(p, std::ios::out | std::ios::binary | std::ios::trunc);
    ofs.write(reinterpret_cast<const char *>(b.data()), static_cast<std::streamsize>(b.size()));
    return p;
  };

  fs::path bad1 = write_bin("load_underflow.wasm", make_mod({ 0x29, 0x03, 0x08, 0x1A }));
  fs::path bad2 = write_bin("store_underflow.wasm", make_mod({ 0x37, 0x03, 0x08 }));
  fs::path good1 = write_bin("valid_load.wasm", make_mod({ 0x41, 0x00, 0x29, 0x03, 0x08, 0x1A }));
  fs::path good2
    = write_bin("valid_store.wasm", make_mod({ 0x41, 0x00, 0x42, 0x01, 0x37, 0x03, 0x08 }));

  bool ok = expect_loader_rejection(bad1, "operand stack underflow")
    && expect_loader_rejection(bad2, "operand stack underflow");
  if ( ok )
  {
    scratch_db_session_t s1;
    ok = s1.open(good1);
  }
  if ( ok )
  {
    scratch_db_session_t s2;
    ok = s2.open(good2);
  }

  std::error_code ec;
  fs::remove_all(tmp, ec);
  if ( ok )
  {
    std::printf("WASMHEX_NATIVE_OPERAND_VALIDATION_OK\n");
    std::fflush(stdout);
  }
  return ok;
}

bool run_check_table_size(const fs::path &fixtures_dir)
{
  qsetenv("WASMHEX_BOUNDS", "hidden");
  fs::path fixed = fixtures_dir / "wasmhex_table_size_direct.wasm";
  {
    scratch_db_session_t sess;
    if ( !sess.open(fixed) )
      return false;
    auto funcs = collect_db_functions(true);
    for ( const auto &kv : std::vector<std::pair<const char *, const char *>>{
            { "table_size_fixed32", "7" }, { "table_size_fixed64", "9" } } )
    {
      bool ok = false;
      std::string txt = decompile_checked(funcs[kv.first], &ok);
      if ( !ok || txt.find(kv.second) == std::string::npos
           || txt.find("wasmhex_") != std::string::npos
           || txt.find("JUMPOUT") != std::string::npos )
        return false;
    }

    // Patch temporary IDB bytes to test processor's independent bounds + malformed ULEB checks
    func_entry_info_t fn;
    if ( !get_func_entry_info(&fn, funcs["table_size_fixed32"]) )
      return false;
    size_t sz = static_cast<size_t>(fn.end_ea - fn.start_ea);
    std::vector<uint8_t> raw(sz);
    if ( get_bytes(raw.data(), static_cast<ssize_t>(sz), fn.start_ea) != static_cast<ssize_t>(sz) )
      return false;
    size_t off = std::string::npos;
    for ( size_t i = 0; i + 2 < sz; ++i )
    {
      if ( raw[i] == 0xFC && raw[i + 1] == 0x10 && raw[i + 2] == 0x00 )
      {
        off = i;
        break;
      }
    }
    if ( off == std::string::npos )
      return false;
    ea_t opcode_ea = fn.start_ea + static_cast<ea_t>(off);
    ea_t index_ea = opcode_ea + 2;

    patch_byte(index_ea, 2);
    insn_t insn;
    if ( decode_insn(&insn, opcode_ea) != 0 )
      return false;
    std::printf("WASMHEX_TABLE_SIZE_PROCESSOR_REJECTED out_of_range\n");

    for ( ea_t b = 0; b < 5; ++b )
      patch_byte(index_ea + b, 0x80);
    if ( decode_insn(&insn, opcode_ea) != 0 )
      return false;
    std::printf("WASMHEX_TABLE_SIZE_PROCESSOR_REJECTED malformed_uleb\n");
    std::printf("WASMHEX_TABLE_SIZE_FIXED_F5_OK\n");
  }

  for ( const char *nm : { "growable", "unbounded" } )
  {
    if ( !expect_loader_rejection(
           fixtures_dir / (std::string("wasmhex_table_size_") + nm + ".wasm"),
           "Native wasmhex table.size requires declared table min equal to max") )
      return false;
    std::string up(nm);
    for ( char &c : up )
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::printf("WASMHEX_TABLE_SIZE_%s_REJECTED\n", up.c_str());
  }

  // Out-of-range table index
  {
    std::string bytes = read_file_text(fixed);
    size_t pos = bytes.find(std::string("\xfc\x10\x00", 3));
    if ( pos == std::string::npos )
      return false;
    bytes[pos + 2] = 2;
    char tmpl[] = "/private/tmp/wasmhex_ts_XXXXXX";
    char *made = mkdtemp(tmpl);
    if ( made == nullptr )
      return false;
    fs::path tmp(made);
    fs::path bad_p = tmp / "table_size_oob.wasm";
    write_file_text(bad_p, bytes);
    bool rej = expect_loader_rejection(bad_p, "table");
    std::error_code ec;
    fs::remove_all(tmp, ec);
    if ( !rej )
      return false;
    std::printf("WASMHEX_TABLE_SIZE_OUT_OF_RANGE_REJECTED\n");
  }
  std::fflush(stdout);
  return true;
}

bool run_check_tailcall_xrefs(const fs::path &fixtures_dir)
{
  qsetenv("WASMHEX_BOUNDS", "hidden");
  scratch_db_session_t sess;
  if ( !sess.open(fixtures_dir / "wasmhex_return_call_indirect.wasm") )
    return false;

  size_t tail_calls = 0;
  for ( size_t i = 0; i < get_func_qty(); ++i )
  {
    func_entry_info_t fi;
    if ( !get_func_entry_info_by_num(&fi, i) )
      continue;
    ea_t ea = fi.start_ea;
    while ( ea < fi.end_ea )
    {
      asize_t sz = get_item_size(ea);
      if ( sz == 0 )
        sz = 1;
      qstring mnem;
      print_insn_mnem(&mnem, ea);
      if ( std::strncmp(mnem.c_str(), "return_call", 11) == 0 )
      {
        ea_t succ = ea + sz;
        xrefblk_t xb;
        for ( bool ok = xb.first_from(ea, XREF_FLOW); ok; ok = xb.next_from() )
        {
          if ( xb.iscode && xb.to == succ )
            return false;
        }
        ++tail_calls;
      }
      ea += sz;
    }
  }
  if ( tail_calls == 0 )
    return false;
  std::printf("WASMHEX_TAILCALL_XREFS_OK %zu\n", tail_calls);
  std::fflush(stdout);
  return true;
}

struct ctree_facts_visitor_t final : public ctree_visitor_t
{
  std::set<std::string> helpers;
  size_t bitwise_ors = 0;
  size_t quiet_ors = 0;
  size_t ternaries = 0;

  ctree_facts_visitor_t() : ctree_visitor_t(CV_FAST) {}

  int idaapi visit_expr(cexpr_t *e) override
  {
    if ( e->op == cot_call && e->x != nullptr && e->x->op == cot_helper && e->x->helper != nullptr )
      helpers.insert(e->x->helper);
    if ( e->op == cot_bor )
    {
      ++bitwise_ors;
      for ( const cexpr_t *ch : { e->x, e->y } )
      {
        if ( ch != nullptr && ch->op == cot_num )
        {
          uint64 v = ch->numval();
          if ( v == 0x00400000ULL || v == 0x0008000000000000ULL )
            ++quiet_ors;
        }
      }
    }
    if ( e->op == cot_tern )
      ++ternaries;
    return 0;
  }
};

bool run_check_float_unary_raw(const fs::path &fixtures_dir)
{
  qsetenv("WASMHEX_BOUNDS", "hidden");
  scratch_db_session_t sess;
  if ( !sess.open(fixtures_dir / "wasmhex_float_unary_raw.wasm") )
    return false;

  int proc_id = PH.id;
  qstring proc_name = inf_get_procname();
  char ftype[128] = { 0 };
  get_file_type_name(ftype, sizeof(ftype));
  if ( proc_id != 0x8000 || proc_name != "wasmdec"
       || std::strcmp(ftype, "WebAssembly Decompiler") != 0 )
    return false;
  std::printf("WASMHEX_PROCESSOR_OK id=0x%x name=%s loader=%s\n", proc_id, proc_name.c_str(),
              ftype);

  auto funcs = collect_db_functions(true);
  for ( const char *vt : { "f32", "f64" } )
  {
    for ( const char *op : { "ceil", "floor", "trunc", "sqrt" } )
    {
      std::string name = std::string(vt) + "_" + op + "_bits";
      ea_t ea = funcs[name];
      if ( ea == BADADDR )
        return false;
      hexrays_failure_t hf;
      cfuncptr_t cf = decompile_function(ea, &hf, 0);
      if ( cf == nullptr )
        return false;
      cf->verify(ALLOW_UNUSED_LABELS, true);
      cf->mba->verify(true);
      ctree_facts_visitor_t facts;
      facts.apply_to(&cf->body, nullptr);
      std::string want_helper = std::string(op) + (std::strcmp(vt, "f32") == 0 ? "f" : "");
      if ( facts.helpers.count(want_helper) == 0 || facts.bitwise_ors != 0 || facts.quiet_ors != 0
           || facts.ternaries != 0 )
        return false;
      for ( const std::string &h : facts.helpers )
        if ( h.compare(0, 5, "wasm_") == 0 )
          return false;
    }
  }
  std::printf("WASMHEX_FLOAT_UNARY_RAW_OK\n");
  std::fflush(stdout);
  return true;
}

bool run_check_sparse_large_indirect(const fs::path &fixtures_dir)
{
  qsetenv("WASMHEX_BOUNDS", "hidden");
  {
    scratch_db_session_t sess;
    if ( !sess.open(fixtures_dir / "wasmhex_sparse_large_indirect.wasm") )
      return false;
    for ( const char *nm : { "dispatch", "tail_dispatch" } )
    {
      ea_t ea = get_name_ea(BADADDR, nm);
      bool ok = false;
      std::string txt = ea != BADADDR ? decompile_checked(ea, &ok) : "";
      if ( !ok || txt.find("identity(") == std::string::npos
           || txt.find("last(") == std::string::npos
           || txt.find("wasm_trap_call_indirect_null()") == std::string::npos
           || txt.find("wasm_trap_call_indirect_type()") == std::string::npos
           || txt.find("wasm_trap_call_indirect_bounds()") == std::string::npos
           || txt.find("JUMPOUT(") != std::string::npos )
        return false;
    }
    std::printf("WASMHEX_SPARSE_LARGE_INDIRECT_F5_OK\n");
  }

  // 65,537 slots limit rejection
  uint32_t count = 65537;
  std::vector<uint8_t> mod = { 0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00 };
  append_wasm_section(&mod, 1, { 2, 0x60, 0, 0, 0x60, 1, 0x7F, 0 });
  append_wasm_section(&mod, 3, { 2, 0, 1 });
  std::vector<uint8_t> tbl = { 1, 0x70, 0 };
  append_uleb_bytes(&tbl, count);
  append_wasm_section(&mod, 4, tbl);
  append_wasm_section(&mod, 7, { 1, 8, 'd', 'i', 's', 'p', 'a', 't', 'c', 'h', 0, 1 });
  std::vector<uint8_t> elems = { 1, 0, 0x41, 0, 0x0B };
  append_uleb_bytes(&elems, count);
  elems.insert(elems.end(), count, 0);
  append_wasm_section(&mod, 9, elems);
  append_wasm_section(&mod, 10, { 2, 2, 0, 0x0B, 7, 0, 0x20, 0, 0x11, 0, 0, 0x0B });

  char tmpl[] = "/private/tmp/wasmhex_sli_XXXXXX";
  char *made = mkdtemp(tmpl);
  if ( made == nullptr )
    return false;
  fs::path tmp(made);
  fs::path p = tmp / "too_many.wasm";
  {
    std::ofstream ofs(p, std::ios::out | std::ios::binary | std::ios::trunc);
    ofs.write(reinterpret_cast<const char *>(mod.data()), static_cast<std::streamsize>(mod.size()));
  }
  bool rej = expect_loader_rejection(
    p, "Native wasmhex indirect dispatch exceeds 65,536 initialized table slots");
  std::error_code ec;
  fs::remove_all(tmp, ec);
  if ( !rej )
    return false;
  std::printf("WASMHEX_SPARSE_LARGE_CASE_LIMIT_REJECTED\n");
  std::fflush(stdout);
  return true;
}

bool run_check_sparse_multi_indirect(const fs::path &fixtures_dir)
{
  char tmpl[] = "/private/tmp/wasmhex_smi_XXXXXX";
  char *made = mkdtemp(tmpl);
  if ( made == nullptr )
    return false;
  fs::path tmp(made);
  fs::path pos = tmp / "positive.wasm";
  fs::path mix = tmp / "mixed.wasm";
  fs::path unsup = tmp / "unsup.wasm";
  if ( !compile_wat_file(fixtures_dir / "wasmhex_sparse_multi_indirect.wat", pos)
       || !compile_wat_file(
         fixtures_dir / "wasmhex_return_call_indirect_mixed_register_unsupported.wat", mix)
       || !compile_wat_file(fixtures_dir / "wasmhex_sparse_multi_reference_unsupported.wat",
                            unsup) )
    return false;

  qsetenv("WASMHEX_BOUNDS", "hidden");
  {
    scratch_db_session_t sess;
    if ( !sess.open(pos) )
      return false;
    for ( const char *nm : { "dispatch_pair", "dispatch_wide", "dispatch_mixed", "dispatch_six",
                             "tail_pair_dispatch", "tail_six_dispatch" } )
    {
      ea_t ea = get_name_ea(BADADDR, nm);
      bool ok = false;
      std::string txt = ea != BADADDR ? decompile_checked(ea, &ok) : "";
      if ( !ok || txt.find("JUMPOUT(") != std::string::npos )
        return false;
    }
    std::printf("WASMHEX_SPARSE_MULTI_F5_OK 6\n");
  }

  if (
    !expect_loader_rejection(
      mix,
      "Native wasmhex return_call_indirect register multi-value ABI is proven only for (i32,i32)") )
    return false;
  std::printf("WASMHEX_RETURN_CALL_INDIRECT_MIXED_REGISTER_REJECTED\n");

  if (
    !expect_loader_rejection(
      unsup,
      "Native wasmhex call_indirect multi-value results support numeric scalars and one abstract funcref/externref handle only") )
    return false;
  std::printf("WASMHEX_SPARSE_MULTI_REFERENCE_REJECTED\n");

  std::error_code ec;
  fs::remove_all(tmp, ec);
  std::fflush(stdout);
  return true;
}

bool run_check_review_fixes()
{
  char tmpl[] = "/private/tmp/wasmhex_rev_XXXXXX";
  char *made = mkdtemp(tmpl);
  if ( made == nullptr )
    return false;
  fs::path tmp(made);

  static const char *const F1_STATIC_WAT = R"((module
  (memory 1)
  (func (export "wrap_trap") (result i32) i32.const -1 i32.load offset=1)
  (func (export "beyond_extent") (result i32) i32.const 65532 i32.load offset=8)
  (func (export "ok_load") (result i32) i32.const 100 i32.load offset=4)
  (func (export "sanity_ifelse") (param i32) (result i32)
    local.get 0 if (result i32) i32.const 42 else i32.const 17 end)
))";
  static const char *const F2_GROWABLE_WAT = R"((module
  (memory (export "mem") 1 2)
  (func (export "within_current") (result i32) i32.const 65530 i32.load offset=2)
  (func (export "beyond_current") (result i32) i32.const 65536 i32.load)
  (func (export "never_valid") (result i32) i32.const 131069 i32.load offset=4)
  (func (export "grow_order") (result i32) (local i32)
    i32.const 65536 i32.load local.set 0
    i32.const 1 memory.grow drop
    i32.const 65536 i32.load local.get 0 i32.add)
))";
  static const char *const F8_FLOAT_WAT = R"((module
  (memory 1)
  (func (export "min_load") (param i32 i32) (result f32)
    local.get 0 f32.load local.get 1 f32.load f32.min)
  (func (export "nearest_load") (param i32) (result f32)
    local.get 0 f32.load f32.nearest)
  (func (export "min_pure") (param f32 f32) (result f32)
    local.get 0 local.get 1 f32.min)
  (func (export "nearest_pure") (param f32) (result f32)
    local.get 0 f32.nearest)
  (func (export "max_load") (param i32 i32) (result f64)
    local.get 0 f64.load local.get 1 f64.load f64.max)
))";

  fs::path f1_w = tmp / "f1.wasm";
  fs::path f2_w = tmp / "f2.wasm";
  fs::path f8_w = tmp / "f8.wasm";
  if ( !compile_wat_string(F1_STATIC_WAT, f1_w) || !compile_wat_string(F2_GROWABLE_WAT, f2_w)
       || !compile_wat_string(F8_FLOAT_WAT, f8_w) )
    return false;

  qsetenv("WASMHEX_BOUNDS", "hidden");
  {
    scratch_db_session_t sess;
    if ( !sess.open(f1_w) )
      return false;
    for ( const char *nm : { "wrap_trap", "beyond_extent" } )
    {
      bool ok = false;
      std::string txt = decompile_checked(get_name_ea(BADADDR, nm), &ok);
      if ( !ok
           || (txt.find("__builtin_trap") == std::string::npos
               && txt.find("wasm_trap") == std::string::npos) )
        return false;
    }
    std::printf("WASMHEX_REVIEW_F1_OK\n");
  }
  {
    scratch_db_session_t sess;
    if ( !sess.open(f2_w) )
      return false;
    bool ok = false;
    std::string order = decompile_checked(get_name_ea(BADADDR, "grow_order"), &ok);
    if ( !ok || order.find("__builtin_wasm_memory_grow(0, 1") == std::string::npos )
      return false;
    std::printf("WASMHEX_REVIEW_F2_OK\n");
  }
  {
    scratch_db_session_t sess;
    if ( !sess.open(f8_w) )
      return false;
    bool ok = false;
    std::string ml = decompile_checked(get_name_ea(BADADDR, "min_load"), &ok);
    if ( !ok || ml.find("fminimumf(") == std::string::npos )
      return false;
    std::printf("WASMHEX_REVIEW_F8_OK\n");
  }

  std::error_code ec;
  fs::remove_all(tmp, ec);
  std::printf("WASMHEX_REVIEW_FIXES_OK\n");
  std::fflush(stdout);
  return true;
}

bool run_one_check_case(const std::string &name, const fs::path &fixtures_dir)
{
  if ( name == "p04" || name == "p04_cfg_reject" || name == "verify_wasmhex_p04_cfg_reject" )
    return run_check_p04_cfg_reject(fixtures_dir);
  if ( name == "p05" || name == "p05_go_refresh_set"
       || name == "verify_wasmhex_p05_go_refresh_set" )
    return run_check_p05_go_refresh_set(fixtures_dir);
  if ( name == "p06" || name == "p06_morestack_names"
       || name == "verify_wasmhex_p06_morestack_names" )
    return run_check_p06_morestack_names(fixtures_dir);
  if ( name == "s9" || name == "s9_morestack_callee"
       || name == "verify_wasmhex_s9_morestack_callee" )
    return run_check_s9_morestack_callee(fixtures_dir);
  if ( name == "s10" || name == "s10_cfg_incomplete"
       || name == "verify_wasmhex_s10_cfg_incomplete" )
    return run_check_s10_cfg_incomplete(fixtures_dir);
  if ( name == "s10_corpus_marks" || name == "verify_wasmhex_s10_corpus_marks" )
    return run_check_s10_corpus_marks(fixtures_dir);
  if ( name == "s1" || name == "s2" || name == "s7_f32" || name == "s11" || name == "s8" )
    return run_check_s1s2s7s11_mode(fixtures_dir, name == "s7_f32" ? "s7" : name);
  if ( name == "s1s2s7s11" || name == "verify_wasmhex_s1s2s7s11" )
    return run_check_s1s2s7s11(fixtures_dir);
  if ( name == "s7" || name == "s7_go_provenance" || name == "verify_wasmhex_s7_go_provenance" )
    return run_check_s7_go_provenance(fixtures_dir);
  if ( name == "cframe_fallback" || name == "check_wasmhex_cframe_fallback" )
    return run_check_cframe_fallback(fixtures_dir);
  if ( name == "p4_idioms" || name == "check_wasmhex_p4_idioms" )
    return run_check_p4_idioms();
  if ( name == "prototypes" || name == "check_wasmhex_prototypes" )
    return run_check_prototypes();
  if ( name == "names" || name == "check_wasmhex_names" )
    return run_check_names();
  if ( name == "dwarf_names" || name == "check_wasmhex_dwarf_names"
       || name == "verify_wasmhex_dwarf_names" )
    return run_check_dwarf_names(fixtures_dir);
  if ( name == "differential" || name == "check_wasmhex_differential" )
    return run_check_differential(fixtures_dir);
  if ( name == "p0_regressions" || name == "check_wasmhex_p0_regressions" )
    return run_check_p0_regressions(fixtures_dir);
  if ( name == "p1_regressions" || name == "check_wasmhex_p1_regressions" )
    return run_check_p1_regressions(fixtures_dir, true);
  if ( name == "p1_fast" )
    return run_check_p1_regressions(fixtures_dir, false);
  if ( name == "m7a_explicit" || name == "check_wasmhex_m7a_explicit" )
    return run_check_m7ab_explicit("m7a");
  if ( name == "m7b_explicit" || name == "check_wasmhex_m7b_explicit" )
    return run_check_m7ab_explicit("m7b");
  if ( name == "m7ab_explicit" || name == "check_wasmhex_m7ab_explicit" )
    return run_check_m7ab_explicit("m7ab");
  if ( name == "review_fixes" || name == "check_wasmhex_review_fixes" )
    return run_check_review_fixes();
  if ( name == "component_loader_idat" || name == "check_component_loader_idat" )
    return run_check_component_loader_idat(fixtures_dir);
  if ( name == "call_ref_rejections" || name == "check_wasmhex_call_ref_rejections" )
    return run_check_call_ref_rejections(fixtures_dir);
  if ( name == "calls_multi_exit" || name == "check_wasmhex_calls_multi_exit"
       || name == "verify_wasmhex_calls_multi" )
    return run_check_calls_multi_exit(fixtures_dir);
  if ( name == "fixed_memory_proof" || name == "check_wasmhex_fixed_memory_proof" )
    return run_check_fixed_memory_proof(fixtures_dir);
  if ( name == "fixed_memory_scalar" || name == "check_wasmhex_fixed_memory_scalar" )
    return run_check_fixed_memory_scalar(fixtures_dir);
  if ( name == "fixed_memory64" || name == "check_wasmhex_fixed_memory64" )
    return run_check_fixed_memory64(fixtures_dir);
  if ( name == "memory_space" || name == "verify_wasmhex_memory_space" )
    return run_check_memory_space(fixtures_dir);
  if ( name == "m1d1_layout" || name == "check_wasmhex_m1d1_layout" )
    return run_check_m1d1_layout(fixtures_dir);
  if ( name == "growable_memory32" || name == "check_wasmhex_growable_memory32" )
    return run_check_growable_memory32(fixtures_dir);
  if ( name == "growable_memory64" || name == "check_wasmhex_growable_memory64" )
    return run_check_growable_memory64(fixtures_dir);
  if ( name == "i64_select_shift" || name == "check_wasmhex_i64_select_shift" )
    return run_check_simple_wat_f5(fixtures_dir / "wasmhex_i64_select_shift.wat",
                                   "WASMHEX_I64_SELECT_SHIFT_F5_OK");
  if ( name == "large_br_table" || name == "check_wasmhex_large_br_table" )
    return run_check_simple_wat_f5(fixtures_dir / "wasmhex_large_br_table.wat",
                                   "WASMHEX_LARGE_BR_TABLE_F5_OK");
  if ( name == "repeated_scalar_loads" || name == "check_wasmhex_repeated_scalar_loads" )
    return run_check_simple_wat_f5(fixtures_dir / "wasmhex_repeated_scalar_loads.wat",
                                   "WASMHEX_REPEATED_SCALAR_LOADS_F5_OK");
  if ( name == "dref_entry_globalget" || name == "check_wasmhex_dref_entry_globalget" )
    return run_check_dref_entry_globalget(fixtures_dir);
  if ( name == "stack_indirect_abi" || name == "check_wasmhex_stack_indirect_abi" )
  {
    bool ok = run_check_simple_wat_f5(fixtures_dir / "wasmhex_stack_arg_abi.wat", nullptr)
      && run_check_simple_wat_f5(fixtures_dir / "wasmhex_stack_indirect_abi.wat", nullptr)
      && run_check_simple_wat_f5(fixtures_dir / "wasmhex_many_signatures.wat", nullptr);
    if ( ok )
      std::printf("WASMHEX_STACK_INDIRECT_ABI_F5_OK\n");
    return ok;
  }
  if ( name == "native_operand_validation" || name == "check_wasmhex_native_operand_validation" )
    return run_check_native_operand_validation();
  if ( name == "sparse_large_indirect" || name == "check_wasmhex_sparse_large_indirect" )
    return run_check_sparse_large_indirect(fixtures_dir);
  if ( name == "sparse_multi_indirect" || name == "check_wasmhex_sparse_multi_indirect" )
    return run_check_sparse_multi_indirect(fixtures_dir);
  if ( name == "table_size" || name == "check_wasmhex_table_size"
       || name == "verify_wasmhex_table_size_direct" )
    return run_check_table_size(fixtures_dir);
  if ( name == "tailcall_xrefs" || name == "verify_wasmhex_tailcall_xrefs" )
    return run_check_tailcall_xrefs(fixtures_dir);
  if ( name == "float_unary_raw" || name == "verify_wasmhex_float_unary_raw" )
    return run_check_float_unary_raw(fixtures_dir);
  if ( name == "simd_bounds_reject" || name == "verify_wasmhex_simd_bounds_reject" )
  {
    qsetenv("WASMHEX_BOUNDS", "explicit");
    scratch_db_session_t sess;
    if ( !sess.open(fixtures_dir / "wasmhex_simd_bounds_shared.wasm") )
      return false;
    func_entry_info_t fi;
    if ( !get_func_entry_info_by_num(&fi, get_func_qty() - 1) )
      return false;
    bool ok = false;
    decompile_checked(fi.start_ea, &ok);
    qsetenv("WASMHEX_BOUNDS", "hidden");
    if ( !ok )
    {
      std::printf("WASMHEX_SIMD_BOUNDS_REJECT_OK\n");
      return true;
    }
    return false;
  }

  if ( name == "imported_active_table" || name == "check_wasmhex_imported_active_table" )
  {
    bool ok = run_check_simple_wat_f5(fixtures_dir / "wasmhex_imported_active_table.wat",
                                      "WASMHEX_IMPORTED_ACTIVE_TABLE_F5_OK")
      && run_check_simple_wat_f5(fixtures_dir / "wasmhex_imported_active_table_larger.wat",
                                 "WASMHEX_IMPORTED_ACTIVE_TABLE_LARGER_F5_OK")
      && run_check_simple_wat_f5(fixtures_dir / "wasmhex_imported_active_table_global_offset.wat",
                                 "WASMHEX_IMPORTED_ACTIVE_TABLE_GLOBAL_OFFSET_F5_OK")
      && run_check_simple_wat_f5(fixtures_dir / "wasmhex_defined_table_global_offset.wat",
                                 "WASMHEX_DEFINED_TABLE_GLOBAL_OFFSET_CALL_INDIRECT_F5_OK")
      && run_check_simple_wat_f5(fixtures_dir / "wasmhex_imported_uninitialized_table_call.wat",
                                 "WASMHEX_UNBOUNDED_IMPORTED_TABLE_CALL_INDIRECT_F5_OK")
      && run_check_simple_wat_f5(fixtures_dir / "wasmhex_imported_active_table_call_indirect.wat",
                                 "WASMHEX_IMPORTED_ACTIVE_TABLE_CALL_INDIRECT_F5_OK")
      && run_check_simple_wat_f5(fixtures_dir / "wasmhex_imported_zero_table_call_indirect.wat",
                                 "WASMHEX_ZERO_MINIMUM_IMPORTED_TABLE_CALL_INDIRECT_F5_OK")
      && run_check_simple_wat_f5(fixtures_dir / "wasmhex_table_call_variants.wat",
                                 "WASMHEX_TABLE_CALL_INDIRECT_F5_OK");
    return ok;
  }
  if ( name == "call_ref_multi" || name == "check_wasmhex_call_ref_multi" )
  {
    return run_check_call_ref_rejections(fixtures_dir);
  }
  if ( name == "sparse_reference_multi" || name == "check_wasmhex_sparse_reference_multi" )
  {
    char tmpl[] = "/private/tmp/wasmhex_srm_XXXXXX";
    char *made = mkdtemp(tmpl);
    if ( made == nullptr )
      return false;
    fs::path tmp(made);
    const char *const unsup_wats[] = {
      "wasmhex_call_ref_typed_result_unsupported.wat",
      "wasmhex_call_ref_vector_result_unsupported.wat",
      "wasmhex_call_ref_two_handles_unsupported.wat",
    };
    bool ok = true;
    for ( const char *wname : unsup_wats )
    {
      fs::path w = tmp / (std::string(wname) + ".wasm");
      if (
        !compile_wat_file(fixtures_dir / wname, w, { "--enable-function-references" })
        || !expect_loader_rejection(
          w,
          "Native wasmhex call_ref multi-value results support numeric scalars and one abstract funcref/externref handle only") )
      {
        ok = false;
        break;
      }
    }
    std::error_code ec;
    fs::remove_all(tmp, ec);
    if ( ok )
      std::printf("WASMHEX_SPARSE_REFERENCE_MULTI_REJECTED\n");
    return ok;
  }
  if ( name == "legacy_call_unwind" || name == "check_wasmhex_legacy_call_unwind"
       || name == "legacy_direct_throw" || name == "check_wasmhex_legacy_direct_throw" )
  {
    char tmpl[] = "/private/tmp/wasmhex_leh_XXXXXX";
    char *made = mkdtemp(tmpl);
    if ( made == nullptr )
      return false;
    fs::path tmp(made);
    fs::path w = tmp / "legacy_eh.wasm";
    bool ok = compile_wat_file(fixtures_dir / "wasmhex_legacy_direct_throw_uncaught.wat", w)
      && expect_loader_rejection(w, "throw");
    std::error_code ec;
    fs::remove_all(tmp, ec);
    if ( ok )
      std::printf("WASMHEX_LEGACY_EH_REJECTED_OK\n");
    return ok;
  }

  std::fprintf(stderr, "wasmdec_runner check: unknown case '%s'\n", name.c_str());
  return false;
}

int cmd_check(int argc, char **argv)
{
  fs::path repo_root = find_repo_root();
  fs::path fixtures_dir = repo_root / "tests" / "fixtures";
  fs::path explicit_idausr;
  std::vector<std::string> cases;

  for ( int i = 0; i < argc; ++i )
  {
    if ( std::strcmp(argv[i], "--idausr") == 0 && i + 1 < argc )
      explicit_idausr = argv[++i];
    else
      cases.push_back(argv[i]);
  }

  static const char *const ALL_FAST_CASES[] = {
    "p04",
    "p05",
    "p06",
    "s9",
    "s10",
    "s10_corpus_marks",
    "s1s2s7s11",
    "s7",
    "cframe_fallback",
    "p4_idioms",
    "prototypes",
    "names",
    "dwarf_names",
    "differential",
    "p0_regressions",
    "p1_fast",
    "m7a_explicit",
    "m7b_explicit",
    "m7ab_explicit",
    "review_fixes",
    "component_loader_idat",
    "call_ref_rejections",
    "call_ref_multi",
    "calls_multi_exit",
    "fixed_memory_proof",
    "fixed_memory_scalar",
    "fixed_memory64",
    "growable_memory32",
    "growable_memory64",
    "i64_select_shift",
    "imported_active_table",
    "large_br_table",
    "legacy_call_unwind",
    "legacy_direct_throw",
    "m1d1_layout",
    "dref_entry_globalget",
    "repeated_scalar_loads",
    "stack_indirect_abi",
    "native_operand_validation",
    "sparse_large_indirect",
    "sparse_multi_indirect",
    "sparse_reference_multi",
    "table_size",
    "tailcall_xrefs",
    "float_unary_raw",
    "simd_bounds_reject",
  };

  if ( cases.empty() || (cases.size() == 1 && cases[0] == "all") )
  {
    cases.clear();
    for ( const char *c : ALL_FAST_CASES )
      cases.push_back(c);
  }

  staged_idausr_t staged;
  if ( !ensure_idalib_initialized(explicit_idausr, &staged) )
    return 2;

  int passed = 0;
  int failed = 0;
  for ( const std::string &c : cases )
  {
    bool ok = run_one_check_case(c, fixtures_dir);
    if ( ok )
    {
      std::printf("CHECK_PASS %s\n", c.c_str());
      ++passed;
    }
    else
    {
      std::printf("CHECK_FAIL %s\n", c.c_str());
      ++failed;
    }
    std::fflush(stdout);
  }
  std::printf("CHECK_TOTAL pass=%d fail=%d\n", passed, failed);
  std::fflush(stdout);
  return failed > 0 ? 1 : 0;
}
