#pragma once
// Fonte de itens ativos do NEI para linkspan.attributes (RFC 0030).
#include "linkspan/attributes/linkspan_attributes.h"
#include "registry.h"

namespace LinkSpanNei {
// Cada item NEI possuído: num botão C conta como equipped, senão como owned. Item não possuído não emite.
// Para no primeiro status não OK e o devolve.
ShipNativeStatus EmitActiveItems(const Registry& registry, LinkSpanAttrEmitFn emit, void* sink);
} // namespace LinkSpanNei
