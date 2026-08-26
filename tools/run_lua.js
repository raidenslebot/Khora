// Real Lua 5.4, compiled to WebAssembly, run in-process by node.
//
// Not a reimplementation of Lua semantics in JavaScript: the emitted file has to
// satisfy the actual interpreter or it does not count. `tools/toolchains.ps1`
// installs the package; this script is in git and the 40MB of node_modules is
// not, which is why the module is resolved by explicit path rather than by
// walking up from the working directory.
//
// With no argument it prints the interpreter version, which is how
// differential_bench probes for a genuine toolchain rather than trusting a name
// on PATH.
const path = require('path');
const fs = require('fs');

const root = path.join(__dirname, '..', 'data', 'toolchains', 'node_modules');
let LuaFactory;
try {
  ({ LuaFactory } = require(path.join(root, 'wasmoon')));
} catch (e) {
  console.error('wasmoon not installed -- run tools/toolchains.ps1');
  process.exitCode = 1;
  return;
}

const file = process.argv[2];
new LuaFactory().createEngine()
  .then(async (lua) => {
    await lua.doString(file ? fs.readFileSync(file, 'utf8') : 'print(_VERSION)');
  })
  .catch((e) => { console.error(String(e)); process.exitCode = 1; });
