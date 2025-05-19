#include "PassManager.h"
#include "Passes.h"
#include "../utils/Exec.h"

#include <iostream>
#include <fstream>
#include <sstream>

using namespace sys;

PassManager::PassManager(ModuleOp *module, const Options &opts):
  module(module), opts(opts) {
  if (opts.compareWith.size()) {
    std::ifstream ifs(opts.compareWith);
    std::stringstream ss;
    ss << ifs.rdbuf();
    truth = ss.str();

    // Strip the string.
    while (truth.size() && std::isspace(truth.back()))
      truth.pop_back();
    
    // We need to separate the final line.
    auto pos = truth.rfind('\n');
    if (pos == std::string::npos) {
      // This is the only line of file.
      exitcode = std::stoi(truth);
      truth.clear();
    } else {
      exitcode = std::stoi(truth.substr(pos + 1));
      truth.erase(pos);
    }
  }

  if (opts.simulateInput.size()) {
    std::ifstream ifs(opts.simulateInput);
    std::stringstream ss;
    ss << ifs.rdbuf();
    input = ss.str();
  }
}

PassManager::~PassManager() {
  for (auto pass : passes)
    delete pass;
}

void PassManager::run() {
  pastFlatten = false;
  pastMem2Reg = false;
  inBackend = false;

  for (auto pass : passes) {
    if (pass->name() == "flatten-cfg")
      pastFlatten = true;
    if (pass->name() == "mem2reg")
      pastMem2Reg = true;
    if (pass->name() == "rv-lower" || pass->name() == "arm-lower")
      inBackend = true;

    pass->run();

    if (opts.verbose || pass->name() == opts.printAfter) {
      std::cerr << "===== After " << pass->name() << " =====\n\n";
      module->dump(std::cerr);
      std::cerr << "\n\n";
    }

    // Before mem2reg, we don't have phis.
    // Verify pass only checks phis; so no point running it before that.
    if (opts.verify && pastMem2Reg)
      Verify(module).run();

    // We can't simulate for backend.
    // Technically we have the capacity, but it's too much work.
    if (opts.compareWith.size() && pastFlatten && !inBackend) {
      exec::Interpreter itp(module);
      std::stringstream buffer(input);
      itp.run(buffer);
      std::string str = itp.out();
      // Strip output.
      while (str.size() && std::isspace(str.back()))
        str.pop_back();

      if (str != truth) {
        std::cerr << "output mismatch:\n" << str << "\n";
        std::cerr << "after pass: " << pass->name() << "\n";
        assert(false);
      }
      if (exitcode != itp.exitcode()) {
        std::cerr << "exit code mismatch:" << itp.exitcode() << " (expected " << exitcode << ")\n";
        std::cerr << "after pass: " << pass->name() << "\n";
        assert(false);
      }
    }
    
    if (opts.stats) {
      std::cerr << pass->name() << ":\n";

      auto stats = pass->stats();
      if (!stats.size())
        std::cerr << "  <no stats>\n";

      for (auto [k, v] : stats)
        std::cerr << "  " << k << " : " << v << "\n";
    }
  }
}

