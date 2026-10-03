// Textos do fork NEI que o host não tem (text.cpp).
#pragma once

#include "oot_text.h"
#include <string>

namespace LinkSpanNei {

// Guarda o linkspan.oot.text; sem ele, os ids do fork seguem sem texto (o Message_OpenText do host cai).
void BindText(const ShipOotTextV1* text);
void SetSensorHint(const std::string& hint);

} // namespace LinkSpanNei
