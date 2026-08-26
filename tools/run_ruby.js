// Real CRuby, compiled to WebAssembly, run in-process by node. Same standard as
// tools/run_lua.js: the actual interpreter, not a reimplementation of it.
//
// TWO THINGS THAT LOOK LIKE "THE PROGRAM PRINTED NOTHING" AND ARE NOT.
//
// WASI buffers the guest's stdout, so calling process.exit() the instant eval
// returns truncates everything the program wrote. There is no exit here.
//
// And the emitted Ruby driver RETURNS the joined output as its last expression
// instead of printing it, so the host has to print the value of the eval.
// `ruby emitted.rb` on a real CRuby prints nothing at all, which is a fact about
// the driver rather than about the emitted functions.
//
// With no argument it prints RUBY_VERSION, which is how differential_bench
// probes for a genuine toolchain.
const path = require('path');
const fs = require('fs');

const root = path.join(__dirname, '..', 'data', 'toolchains', 'node_modules');
let DefaultRubyVM;
try {
  ({ DefaultRubyVM } = require(path.join(root, '@ruby', 'wasm-wasi', 'dist', 'cjs', 'node.js')));
} catch (e) {
  console.error('@ruby/wasm-wasi not installed -- run tools/toolchains.ps1');
  process.exitCode = 1;
  return;
}

const file = process.argv[2];
(async () => {
  const wasm = path.join(root, '@ruby', 'head-wasm-wasi', 'dist', 'ruby.wasm');
  const mod = await WebAssembly.compile(fs.readFileSync(wasm));
  const { vm } = await DefaultRubyVM(mod);
  const src = file ? fs.readFileSync(file, 'utf8') : '"ruby " + RUBY_VERSION';
  const out = vm.eval(src);
  console.log(out.toString());
})().catch((e) => { console.error(String(e)); process.exitCode = 1; });
