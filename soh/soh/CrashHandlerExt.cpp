#include "CrashHandlerExt.h"
#include "variables.h"
#include "z64.h"
#include "z64actor.h"
#include <stdio.h>
#include <array>
#include <algorithm>
#include <cstring>
#include "soh/ActorDB.h"
#include <fast/interpreter.h>
#include <shiplua/native/NativeDiagnostics.h>

#define WRITE_VAR_LINE(buff, len, varName, varValue, capacity) \
    append_str(buff, len, varName, capacity);                 \
    append_line(buff, len, varValue, capacity);
#define WRITE_VAR(buff, len, varName, varValue, capacity) \
    append_str(buff, len, varName, capacity);            \
    append_str(buff, len, varValue, capacity);

extern "C" PlayState* gPlayState;

static std::array<const char*, ACTORCAT_MAX> sCatToStrArray{
    "SWITCH", "BG", "PLAYER", "EXPLOSIVE", "NPC", "ENEMY", "PROP", "ITEMACTION", "MISC", "BOSS", "DOOR", "CHEST",
};

#define DEFINE_SCENE(_1, _2, enumName, _4, _5, _6) #enumName,

static std::array<const char*, SCENE_ID_MAX> sSceneIdToStrArray{
#include "tables/scene_table.h"
};

#undef DEFINE_SCENE

// CrashHandlerCallback does not expose a capacity. Keep this in sync with
// Ship::CrashHandler::gMaxBufferSize (libultraship, currently 32768 bytes).
static constexpr size_t sCrashReportCapacity = 32768;
static constexpr char sActorCountUnavailable[] = "  ... actor list inconsistent: count unavailable";
// Inclui NUL, newline e um possível newline para separar o prefixo existente.
static constexpr size_t sActorSummaryCapacity =
    std::max(3 * sizeof(size_t) + sizeof("  ...  actors omitted\n"), sizeof(sActorCountUnavailable) + 2);

static bool append_text(char* buf, size_t* len, const char* str, bool newline, size_t capacity) {
    const size_t extra = newline ? 1 : 0;
    if (*len >= capacity || capacity - 1 - *len < extra) {
        return false;
    }
    const size_t available = capacity - 1 - *len - extra;
    size_t length = 0;
    while (length < available && str[length] != '\0') {
        ++length;
    }
    if (str[length] != '\0') {
        return false;
    }
    // Commit only complete strings/lines, always leaving room for NUL.
    memcpy(buf + *len, str, length);
    *len += length;
    if (newline) {
        buf[(*len)++] = '\n';
    }
    buf[*len] = '\0';
    return true;
}

static bool append_str(char* buf, size_t* len, const char* str,
                       size_t capacity = sCrashReportCapacity) {
    return append_text(buf, len, str, false, capacity);
}

static bool append_line(char* buf, size_t* len, const char* str,
                        size_t capacity = sCrashReportCapacity) {
    return append_text(buf, len, str, true, capacity);
}

static bool CrashHandler_ActorListInconsistent(char* buffer, size_t* pos) {
    if (*pos != 0 && buffer[*pos - 1] != '\n') {
        append_line(buffer, pos, "", sCrashReportCapacity);
    }
    append_line(buffer, pos, sActorCountUnavailable, sCrashReportCapacity);
    return false;
}

// False indica que o callback deve retornar, preservando o texto já escrito.
static bool CrashHandler_WriteActorData(char* buffer, size_t* pos, size_t textCapacity) {
    size_t omitted = 0;
    // Orçamento global por passagem, não por categoria. Um ciclo também o esgota.
    size_t countBudget = ACTOR_NUMBER_MAX;
    for (unsigned int i = 0; i < ACTORCAT_MAX; ++i) {
        for (Actor* cur = gPlayState->actorCtx.actorLists[i].head; cur != nullptr; cur = cur->next) {
            if (countBudget == 0) {
                return CrashHandler_ActorListInconsistent(buffer, pos);
            }
            --countBudget;
            ++omitted;
        }
    }

    size_t printBudget = ACTOR_NUMBER_MAX;
    for (unsigned int i = 0; i < ACTORCAT_MAX; ++i) {
        Actor* cur = gPlayState->actorCtx.actorLists[i].head;
        if (cur == nullptr) {
            continue;
        }
        const std::string categoryLine = std::string("  Category: ") + sCatToStrArray[i];
        if (!append_line(buffer, pos, categoryLine.c_str(), textCapacity)) {
            break;
        }
        while (cur != nullptr) {
            if (printBudget == 0 || omitted == 0) {
                return CrashHandler_ActorListInconsistent(buffer, pos);
            }
            --printBudget;
            std::string actorLine = "    ";
            actorLine += ActorDB::Instance->RetrieveEntry(cur->id).entry.valid
                             ? ActorDB::Instance->RetrieveEntry(cur->id).entry.desc
                             : "???";
            actorLine += " (" + std::to_string(cur->params) + ")";
            if (!append_line(buffer, pos, actorLine.c_str(), textCapacity)) {
                break;
            }
            --omitted;
            cur = cur->next;
        }
        if (cur != nullptr) {
            break;
        }
    }

    if (omitted != 0) {
        // An incoming prefix or native diagnostic may end without a newline.
        if (*pos != 0 && buffer[*pos - 1] != '\n') {
            append_line(buffer, pos, "", sCrashReportCapacity);
        }
        char summary[sActorSummaryCapacity];
        snprintf(summary, sizeof(summary), "  ... %zu actors omitted", omitted);
        append_line(buffer, pos, summary, sCrashReportCapacity);
        return false;
    }
    return true;
}

extern "C" void CrashHandler_PrintSohData(char* buffer, size_t* pos) {
    // Sem espaço (ou posição inválida): omita a extensão, nunca recue o prefixo LUS.
    if (*pos >= sCrashReportCapacity - 1) {
        return;
    }
    buffer[*pos] = '\0';
    const size_t available = sCrashReportCapacity - 1 - *pos;
    const size_t summaryReserve = gPlayState != nullptr ? std::min(available, sActorSummaryCapacity) : 0;
    const size_t textCapacity = sCrashReportCapacity - summaryReserve;
    char intCharBuffer[16];
    append_line(buffer, pos, "Build Information:", textCapacity);
    WRITE_VAR_LINE(buffer, pos, "  Game Version: ", (const char*)gBuildVersion, textCapacity);
    WRITE_VAR_LINE(buffer, pos, "  Git Branch: ", (const char*)gGitBranch, textCapacity);
    WRITE_VAR_LINE(buffer, pos, "  Git Commit: ", (const char*)gGitCommitHash, textCapacity);
    WRITE_VAR_LINE(buffer, pos, "  Build Date: ", (const char*)gBuildDate, textCapacity);

    // SOH [Link-Span] mod nativo ativo, DLLs de mods na pilha e proteção de boot; sem alocar.
    // NativeDiagnostics writes up to capacity bytes and does not append NUL.
    const size_t nativeCapacity = std::min<size_t>(4096, textCapacity - 1 - *pos);
    if (nativeCapacity != 0) {
        *pos += ShipLua::WriteNativeCrashReport(buffer + *pos, nativeCapacity);
        buffer[*pos] = '\0';
    }

    if (gPlayState != nullptr) {
        // SOH [Link-Span] cenas registradas por mods ficam fora da tabela vanilla.
        WRITE_VAR_LINE(buffer, pos, "Scene: ",
                       gPlayState->sceneNum >= 0 && gPlayState->sceneNum < SCENE_ID_MAX
                           ? sSceneIdToStrArray[gPlayState->sceneNum]
                           : "mod scene", textCapacity);

        snprintf(intCharBuffer, sizeof(intCharBuffer), "%i", gPlayState->roomCtx.curRoom.num);
        WRITE_VAR_LINE(buffer, pos, "Room: ", intCharBuffer, textCapacity);

        append_line(buffer, pos, "Actors:", textCapacity);
        if (!CrashHandler_WriteActorData(buffer, pos, textCapacity)) {
            return;
        }

        append_line(buffer, pos, "GFX Stack:", textCapacity);
        for (auto& disp : Fast::g_exec_stack.disp_stack) {
            std::string line = "  ";
            line += disp.file;
            line += ":";
            line += std::to_string(disp.line);
            append_line(buffer, pos, line.c_str(), textCapacity);
        }
    }
}
