// C++ fixture generator concept — br_table matrix; generates WAT files
// and calls wat2wasm via system().
// Usage: gen_fixtures <output-dir>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static void write_wat(const std::string &dir, const std::string &name, const std::string &wat)
{
  std::string path = dir + "/" + name + ".wat";
  FILE *f = fopen(path.c_str(), "w");
  if ( !f )
  {
    fprintf(stderr, "cannot write %s\n", path.c_str());
    return;
  }
  fwrite(wat.c_str(), 1, wat.size(), f);
  fclose(f);
  // Compile to wasm
  std::string cmd = "wat2wasm " + path + " -o " + dir + "/" + name + ".wasm";
  if ( system(cmd.c_str()) != 0 )
    fprintf(stderr, "wat2wasm failed for %s\n", name.c_str());
}

static std::string bt_small()
{
  return "(module\n"
         "  (func (export \"dispatch\") (param i32) (result i32)\n"
         "    (local $r i32)\n"
         "    (block $out\n"
         "      (block $a\n"
         "        (block $b\n"
         "          (block $c\n"
         "            local.get 0\n"
         "            (br_table $c $b $a $out)\n"
         "          )\n"
         "          i32.const 10 local.set $r br $out\n"
         "        )\n"
         "        i32.const 20 local.set $r br $out\n"
         "      )\n"
         "      i32.const 30 local.set $r\n"
         "    )\n"
         "    local.get $r))\n";
}

static std::string bt_all_same(int n)
{
  std::string labels;
  for ( int i = 0; i <= n; i++ )
    labels += "$one ";
  return "(module\n"
         "  (func (export \"dispatch\") (param i32) (result i32)\n"
         "    (block $one\n"
         "      local.get 0\n"
         "      (br_table "
    + labels
    + ")\n"
      "    )\n"
      "    i32.const 42))\n";
}

static std::string bt_payload()
{
  return "(module\n"
         "  (func (export \"dispatch\") (param i32) (result i32)\n"
         "    (block $out (result i32)\n"
         "      (block $a (result i32)\n"
         "        (block $b (result i32)\n"
         "          (block $c (result i32)\n"
         "            (i32.add (local.get 0) (i32.const 100))\n"
         "            (local.get 0)\n"
         "            (br_table $c $b $a $out)\n"
         "          )\n"
         "          i32.const 1000 i32.add\n"
         "        )\n"
         "        i32.const 2000 i32.add\n"
         "      )\n"
         "      i32.const 3000 i32.add\n"
         "    )))\n";
}

int main(int argc, char **argv)
{
  std::string dir = argc > 1 ? argv[1] : "tests/fixtures/brtable";
  fprintf(stderr, "Generating br_table fixtures into %s\n", dir.c_str());

  write_wat(dir, "bt_small", bt_small());
  write_wat(dir, "bt_all_same", bt_all_same(8));
  write_wat(dir, "bt_payload", bt_payload());

  fprintf(stderr, "Done — 3 fixtures generated\n");
  return 0;
}
