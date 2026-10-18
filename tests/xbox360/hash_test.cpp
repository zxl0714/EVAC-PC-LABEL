#include "diagnostics/run_context.h"
#include <iostream>
int main() {
    if (espgaluda2::Sha256Text("")!="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" ||
        espgaluda2::Sha256Text("abc")!="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") return 2;
    std::cout<<"SHA-256 regression passed\n";return 0;
}
