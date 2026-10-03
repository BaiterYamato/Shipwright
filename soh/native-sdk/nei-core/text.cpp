// Textos que o fork NEI monta na hora de abrir a caixa (hooks OnOpenText em C++ do fork: ItemMessages.cpp,
// picto_message.cpp) e que não entram na DLL. Sem eles o id não existe na tabela do host e o Message_OpenText
// copia de um ponteiro nulo (o Time Gate derrubava o jogo, NEI-011). O nei_host_overrides.h desvia as
// Message_StartTextbox/Message_ContinueTextbox do código do fork para cá: antes de abrir, o texto do id é gravado
// pelo linkspan.oot.text, com o estado do momento (a cor do fogo da Lantern). Só o inglês, como os textos de
// get-item do registro.
#include "text.h"

#include <cstdint>
#include <string>

namespace {

const ShipOotTextV1* gText = nullptr;
std::string gSensorHint;

// Mesmos códigos do CustomMessage do SoH: %r %g %b %c %p %y %w mudam a cor e & quebra a linha. O terminador 0x02
// o host acrescenta.
std::string Encode(const char* text) {
    std::string out;
    for (const char* p = text; *p; ++p) {
        if (*p == '&') {
            out += '\x01';
        } else if (*p == '%' && p[1]) {
            static const char kCodes[] = "wrgbcpy";
            const char* code = std::char_traits<char>::find(kCodes, 7, p[1]);
            if (code) {
                out += '\x05';
                out += static_cast<char>(0x40 + (code - kCodes));
                ++p;
                continue;
            }
            out += *p;
        } else {
            out += *p;
        }
    }
    return out;
}

// Tipos e posições de caixa do z64message.h.
constexpr uint8_t kBoxBlack = 0;
constexpr uint8_t kBoxBlue = 2;
constexpr uint8_t kPosBottom = 3;

void Set(uint16_t id, uint8_t boxType, const char* text) {
    const std::string bytes = Encode(text);
    gText->set_message(LINKSPAN_OOT_TEXT_ENGLISH, id, boxType, kPosBottom,
                       reinterpret_cast<const uint8_t*>(bytes.data()), static_cast<uint32_t>(bytes.size()));
}

// \x13\xB4 é o ícone do ITEM_LANTERN na caixa.
const char* LanternText(uint8_t fireType) {
    switch (fireType) {
        case 1:
            return "\x13\xB4You caught %rRegular Fire%w!&Swing to %rlight torches%w,&%rburn grass%w and spawn flames.";
        case 2:
            return "\x13\xB4You caught %bBlue Fire%w!&Swing to release %bblue fire%w&that %cmelts red ice%w.";
        case 3:
            return "\x13\xB4You caught %pPoe Fire%w!&%pReveals the invisible%w and&%pdispels illusions%w. No magic.";
        case 4:
            return "\x13\xB4You caught %gGreen Fire%w!&Slowly %gregenerates health%w&while it stays lit.";
        default:
            return "\x13\xB4The lantern is empty.";
    }
}

} // namespace

extern "C" uint8_t gLanternCatchPending;

namespace LinkSpanNei {

void BindText(const ShipOotTextV1* text) {
    gText = text && text->size >= sizeof(ShipOotTextV1) && text->set_message ? text : nullptr;
}
void SetSensorHint(const std::string& hint) { gSensorHint = hint; }

} // namespace LinkSpanNei

extern "C" int NeiText_Prepare(uint16_t id) {
    if (!gText) {
        return 0;
    }
    switch (id) {
        case 0x9300:
            if (gSensorHint.empty()) return 0;
            Set(id, kBoxBlue, gSensorHint.c_str());
            return 1;
        case 0x9301:
            Set(id, kBoxBlack, "Asking costs one %rHeart Container%w,&forever. Ask the slate?\x1B%g&&Yes&No%w");
            return 1;
        case 0x9216: // TEXT_TIME_GATE_PROMPT (item_time_gate.c); \x1B abre as duas escolhas
            Set(id, kBoxBlack, "Travel through time?\x1B%g&&Yes&No%w");
            return 1;
        case 0x00F9: // TEXT_LANTERN_CATCH (z_player.c do fork)
            Set(id, kBoxBlue, LanternText(gLanternCatchPending));
            return 1;
        case 0x6F08: // PICTO_KEEP_TEXTID (picto_box.c); o fork nomeia o alvo da foto, aqui fica a pergunta do MM
            Set(id, kBoxBlack, "Keep this %rpicture%w?\x1B%g&Yes&No%w");
            return 1;
        case 0x6F0A: // MM_TRADE_USE_TEXTID (trade_items.c)
            Set(id, kBoxBlack, "Oak's words echoed... There's a&time and place for everything,&but not now.");
            return 1;
        default:
            return 0;
    }
}
