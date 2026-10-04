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

#define WRITE_VAR_LINE(buff, len, varName, varValue) \
    append_str(buff, len, varName);                  \
    append_line(buff, len, varValue);
#define WRITE_VAR(buff, len, varName, varValue) \
    append_str(buff, len, varName);             \
    append_str(buff, len, varValue);

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
// Three decimal digits per byte cover any size_t; includes newline and NUL.
// Reserve this even before Actors, so earlier sections cannot consume its space.
static constexpr size_t sActorSummaryCapacity = 3 * sizeof(size_t) + sizeof("  ...  atores omitidos\n");
static constexpr size_t sCrashReportTextCapacity = sCrashReportCapacity - sActorSummaryCapacity;

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
                       size_t capacity = sCrashReportTextCapacity) {
    return append_text(buf, len, str, false, capacity);
}

static bool append_line(char* buf, size_t* len, const char* str,
                        size_t capacity = sCrashReportTextCapacity) {
    return append_text(buf, len, str, true, capacity);
}

// False means the omission summary is the final line of the report.
static bool CrashHandler_WriteActorData(char* buffer, size_t* pos) {
    size_t omitted = 0;
    for (unsigned int i = 0; i < ACTORCAT_MAX; ++i) {
        for (Actor* cur = gPlayState->actorCtx.actorLists[i].head; cur != nullptr; cur = cur->next) {
            ++omitted;
        }
    }

    for (unsigned int i = 0; i < ACTORCAT_MAX; ++i) {
        Actor* cur = gPlayState->actorCtx.actorLists[i].head;
        if (cur == nullptr) {
            continue;
        }
        const std::string categoryLine = std::string("  Category: ") + sCatToStrArray[i];
        if (!append_line(buffer, pos, categoryLine.c_str())) {
            break;
        }
        while (cur != nullptr) {
            std::string actorLine = "    ";
            actorLine += ActorDB::Instance->RetrieveEntry(cur->id).entry.valid
                             ? ActorDB::Instance->RetrieveEntry(cur->id).entry.desc
                             : "???";
            actorLine += " (" + std::to_string(cur->params) + ")";
            if (!append_line(buffer, pos, actorLine.c_str())) {
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
        snprintf(summary, sizeof(summary), "  ... %zu atores omitidos", omitted);
        append_line(buffer, pos, summary, sCrashReportCapacity);
        return false;
    }
    return true;
}

extern "C" void CrashHandler_PrintSohData(char* buffer, size_t* pos) {
    // If LUS arrives full, recover space for the summary inside its own buffer.
    *pos = std::min(*pos, sCrashReportTextCapacity - 1);
    buffer[*pos] = '\0';
    char intCharBuffer[16];
    append_line(buffer, pos, "Build Information:");
    WRITE_VAR_LINE(buffer, pos, "  Game Version: ", (const char*)gBuildVersion);
    WRITE_VAR_LINE(buffer, pos, "  Git Branch: ", (const char*)gGitBranch);
    WRITE_VAR_LINE(buffer, pos, "  Git Commit: ", (const char*)gGitCommitHash);
    WRITE_VAR_LINE(buffer, pos, "  Build Date: ", (const char*)gBuildDate);

    // SOH [Link-Span] mod nativo ativo, DLLs de mods na pilha e proteção de boot; sem alocar.
    // NativeDiagnostics writes up to capacity bytes and does not append NUL.
    const size_t nativeCapacity = std::min<size_t>(4096, sCrashReportTextCapacity - 1 - *pos);
    *pos += ShipLua::WriteNativeCrashReport(buffer + *pos, nativeCapacity);
    buffer[*pos] = '\0';

    if (gPlayState != nullptr) {
        // SOH [Link-Span] cenas registradas por mods ficam fora da tabela vanilla.
        WRITE_VAR_LINE(buffer, pos, "Scene: ",
                       gPlayState->sceneNum >= 0 && gPlayState->sceneNum < SCENE_ID_MAX
                           ? sSceneIdToStrArray[gPlayState->sceneNum]
                           : "mod scene");

        snprintf(intCharBuffer, sizeof(intCharBuffer), "%i", gPlayState->roomCtx.curRoom.num);
        WRITE_VAR_LINE(buffer, pos, "Room: ", intCharBuffer);

        append_line(buffer, pos, "Actors:");
        if (!CrashHandler_WriteActorData(buffer, pos)) {
            return;
        }

        append_line(buffer, pos, "GFX Stack:");
        for (auto& disp : Fast::g_exec_stack.disp_stack) {
            std::string line = "  ";
            line += disp.file;
            line += ":";
            line += std::to_string(disp.line);
            append_line(buffer, pos, line.c_str());
        }
    }
}
