#include <tinyxml2.h>

#include "soh/resource/importer/scenecommand/SetSoundSettingsFactory.h"
#include "soh/resource/type/scenecommand/SetSoundSettings.h"
#include "soh/resource/logging/SceneCommandLoggers.h"
#include "soh/unbound/UnboundAudio.h"
#include <spdlog/spdlog.h>

namespace SOH {
std::shared_ptr<Ship::IResource> SetSoundSettingsFactory::ReadResource(std::shared_ptr<Ship::ResourceInitData> initData,
                                                                       std::shared_ptr<Ship::BinaryReader> reader) {
    auto setSoundSettings = std::make_shared<SetSoundSettings>(initData);

    ReadCommandId(setSoundSettings, reader);

    setSoundSettings->settings.reverb = reader->ReadInt8();
    setSoundSettings->settings.natureAmbienceId = reader->ReadInt8();
    setSoundSettings->settings.seqId = reader->ReadInt8();

    if (CVarGetInteger(CVAR_DEVELOPER_TOOLS("ResourceLogging"), 0)) {
        LogSoundSettingsAsXML(setSoundSettings);
    }

    return setSoundSettings;
}

std::shared_ptr<Ship::IResource>
SetSoundSettingsFactoryXML::ReadResource(std::shared_ptr<Ship::ResourceInitData> initData,
                                         tinyxml2::XMLElement* reader) {
    auto setSoundSettings = std::make_shared<SetSoundSettings>(initData);

    setSoundSettings->cmdId = SceneCommandID::SetSoundSettings;

    setSoundSettings->settings.reverb = reader->IntAttribute("Reverb");
    setSoundSettings->settings.natureAmbienceId = reader->IntAttribute("NatureAmbienceId");
    setSoundSettings->settings.seqId = reader->IntAttribute("SeqId");
    // SOH [Link-Span] OOT-CORE-008: música de mod por caminho (sound.song do SPEC.md §4.2 do Unbound).
    if (const char* song = reader->Attribute("Song"); song != nullptr && *song != '\0') {
        setSoundSettings->unboundSongPath = song;
        setSoundSettings->unboundSongSeqId = SOH::Unbound::SequenceIdForPath(song);
        if (setSoundSettings->unboundSongSeqId == 0) {
            SPDLOG_ERROR("{}: song {} is not a loaded sequence (no mounted archive provides it)", initData->Path,
                         song);
        }
    }

    return setSoundSettings;
}
} // namespace SOH
