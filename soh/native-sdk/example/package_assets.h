#pragma once

#include <string>

// Pasta assets/ do pacote do mod, ao lado de provider/: o core extrai o ZIP e carrega a DLL de
// dentro dele. Vazio quando a pasta não existe.
std::string ProviderAssetsDirectory();
