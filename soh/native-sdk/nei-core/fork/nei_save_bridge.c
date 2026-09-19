// Ponte C entre NeiSaveData do fork e a persistencia JSON do coremod.
#include "nei_save_bridge.h"

#include <stddef.h>
#include <string.h>

#include "mods/nei_save.h"

#define NEI_SAVE_SCALAR(name, field) { name, offsetof(NeiSaveData, field), sizeof(((NeiSaveData*)0)->field), 1, 0 }
#define NEI_SAVE_ARRAY(name, field) \
    { name, offsetof(NeiSaveData, field), sizeof(((NeiSaveData*)0)->field[0]), sizeof(((NeiSaveData*)0)->field) / sizeof(((NeiSaveData*)0)->field[0]), 0 }

static const NeiSaveField kFields[] = {
    NEI_SAVE_ARRAY("ownedItems", ownedItems),
    NEI_SAVE_SCALAR("shovelOwned", shovelOwned),
    NEI_SAVE_SCALAR("dominionOwned", dominionOwned),
    NEI_SAVE_SCALAR("pokeballOwned", pokeballOwned),
    NEI_SAVE_SCALAR("extEquipOwnedBits", extEquipOwnedBits),
    NEI_SAVE_SCALAR("lanternFireType", lanternFireType),
    NEI_SAVE_SCALAR("lanternCapturedTypes", lanternCapturedTypes),
    NEI_SAVE_SCALAR("twilightUpgrade", twilightUpgrade),
    NEI_SAVE_SCALAR("ultrashotOwned", ultrashotOwned),
    NEI_SAVE_SCALAR("clawshotModeActive", clawshotModeActive),
    NEI_SAVE_SCALAR("galeBoomerangModeActive", galeBoomerangModeActive),
    NEI_SAVE_SCALAR("weaponUpgrades", weaponUpgrades),
    NEI_SAVE_SCALAR("extEquipSword", extEquipSword),
    NEI_SAVE_SCALAR("extEquipShield", extEquipShield),
    NEI_SAVE_SCALAR("extEquipTunic", extEquipTunic),
    NEI_SAVE_SCALAR("extEquipBoots", extEquipBoots),
    NEI_SAVE_ARRAY("bottleSlots", bottleSlots),
    NEI_SAVE_SCALAR("bottomlessBottleMode", bottomlessBottleMode),
    NEI_SAVE_SCALAR("netEquipped", netEquipped),
    NEI_SAVE_SCALAR("bottomlessContent", bottomlessContent),
    NEI_SAVE_SCALAR("bottomlessCount", bottomlessCount),
    NEI_SAVE_SCALAR("powerKegOwned", powerKegOwned),
    NEI_SAVE_SCALAR("powerKegCount", powerKegCount),
    NEI_SAVE_SCALAR("powerKegMode", powerKegMode),
    NEI_SAVE_SCALAR("tradeAdultOwned", tradeAdultOwned),
    // A foto I5 custa 11200 elementos JSON. As flags pequenas continuam no save.
    NEI_SAVE_SCALAR("pictoboxOwned", pictoboxOwned),
    NEI_SAVE_SCALAR("pictoHasPhoto", pictoHasPhoto),
    NEI_SAVE_SCALAR("pictoFlags0", pictoFlags0),
    NEI_SAVE_SCALAR("pictoFlags1", pictoFlags1),
    NEI_SAVE_SCALAR("shieldOwned", shieldOwned),
    NEI_SAVE_SCALAR("mmQuestItems", mmQuestItems),
    NEI_SAVE_ARRAY("comboObtained", comboObtained),
    NEI_SAVE_ARRAY("comboObtainedFc", comboObtainedFc),
    NEI_SAVE_ARRAY("comboAppliedFc", comboAppliedFc),
    NEI_SAVE_SCALAR("comboTriforce", comboTriforce),
    NEI_SAVE_SCALAR("comboGoalFlags", comboGoalFlags),
    NEI_SAVE_SCALAR("capeHidden", capeHidden),
    NEI_SAVE_SCALAR("pendantEffectOff", pendantEffectOff),
    NEI_SAVE_SCALAR("capeOwned", capeOwned),
    NEI_SAVE_SCALAR("extTunicLayoutVersion", extTunicLayoutVersion),
    NEI_SAVE_SCALAR("caneSkills", caneSkills),
    NEI_SAVE_SCALAR("caneType", caneType),
    NEI_SAVE_ARRAY("caneSkillSel", caneSkillSel),
    NEI_SAVE_SCALAR("trirodEchoesLo", trirodEchoesLo),
    NEI_SAVE_SCALAR("trirodEchoesHi", trirodEchoesHi),
    NEI_SAVE_SCALAR("trirodSel", trirodSel),
    NEI_SAVE_SCALAR("trirodLayoutVersion", trirodLayoutVersion),
    NEI_SAVE_SCALAR("trirodFullList", trirodFullList),
    NEI_SAVE_SCALAR("season", season),
    NEI_SAVE_SCALAR("seasonsOwned", seasonsOwned),
    NEI_SAVE_SCALAR("quartzOwned", quartzOwned),
    NEI_SAVE_SCALAR("quartzCategory", quartzCategory),
    NEI_SAVE_SCALAR("quartzSubcat", quartzSubcat),
    NEI_SAVE_SCALAR("pendantOwned", pendantOwned),
    NEI_SAVE_SCALAR("extBootsLayoutVersion", extBootsLayoutVersion),
    NEI_SAVE_SCALAR("sw97BowElement", sw97BowElement),
    NEI_SAVE_SCALAR("sw97SlingElement", sw97SlingElement),
    NEI_SAVE_SCALAR("bombArrowsOwned", bombArrowsOwned),
    NEI_SAVE_SCALAR("wandMode", wandMode),
    NEI_SAVE_SCALAR("wandRodsOwned", wandRodsOwned),
    NEI_SAVE_SCALAR("sw97LayoutVersion", sw97LayoutVersion),
    NEI_SAVE_SCALAR("ootMasksOwned", ootMasksOwned),
    NEI_SAVE_SCALAR("slateMode", slateMode),
    NEI_SAVE_SCALAR("slateRunesOwned", slateRunesOwned),
    NEI_SAVE_SCALAR("ritoMaskFlags", ritoMaskFlags),
};

const NeiSaveField* NeiSave_Fields(uint32_t* count) {
    if (count) {
        *count = (uint32_t)(sizeof(kFields) / sizeof(kFields[0]));
    }
    return kFields;
}

void* NeiSave_Data(void) {
    return Nei_Save();
}

void NeiSave_Reset(void) {
    NeiSaveData* save = Nei_Save();
    memset(save, 0, sizeof(*save));
    for (size_t i = 0; i < sizeof(save->ownedItems) / sizeof(save->ownedItems[0]); ++i) {
        save->ownedItems[i] = 0xFF;
    }
    memset(save->bottleSlots, 0xFF, sizeof(save->bottleSlots));
    save->bottomlessContent = 0xFF;
    save->season = SEASON_SPRING;
}

void NeiSave_AfterLoad(void) {
    NeiSaveData* save = Nei_Save();
    // A v2 reordenou a tabela de ecos: bits de uma máscara v1 apontam para outros ecos, então o fork zera.
    if (save->trirodLayoutVersion < 2) {
        save->trirodEchoesLo = 0;
        save->trirodEchoesHi = 0;
        save->trirodSel = 0;
        save->trirodLayoutVersion = 2;
    }
}
