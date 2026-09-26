// Textos do fork NEI que o host não tem (text.cpp).
#pragma once

#include "oot_text.h"

namespace LinkSpanNei {

// Guarda o linkspan.oot.text; sem ele, os ids do fork seguem sem texto (o Message_OpenText do host cai).
void BindText(const ShipOotTextV1* text);

} // namespace LinkSpanNei
