#pragma once

#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "oot_save.h"

namespace ShipLuaHost {

// Serviço linkspan.oot.save: chamadas de mods só na thread dona. As funções de
// integração abaixo trancam o mesmo mutex e podem rodar na thread de save.
void InitializeOotNativeSave(std::thread::id ownerThread = std::this_thread::get_id());
void ResetOotNativeSave();
const ShipOotSaveV1& GetOotNativeSaveService();

// Novo arquivo ou início de carga: esvazia os blocos e o slot.
void ClearOotSaveData();
void SetOotSaveSlot(int32_t slot);
// Conteúdo de sections.linkspan.data de um arquivo. Entradas malformadas são ignoradas.
void ImportOotSaveSection(const nlohmann::json& section);
// O que vai para sections.linkspan.data; transações abertas gravam o estado de antes do begin.
nlohmann::json ExportOotSaveSection();
// Namespaces marcados como obrigatórios no arquivo e que nenhum mod abriu.
std::vector<std::string> MissingRequiredOotNamespaces();

// Blocos do próprio host (prefixo "linkspan.").
void SetOotHostSaveBlock(const std::string& name, uint32_t version, nlohmann::json data);
bool GetOotHostSaveBlock(const std::string& name, nlohmann::json& data, uint32_t& version);

// Integração com o SaveManager (OotNativeSaveGame.cpp, só no jogo).
void RegisterOotSaveSection();
void OotBeforeSave(int32_t fileNum, int32_t sectionId);
void OotAfterCopy(int32_t from, int32_t to);

} // namespace ShipLuaHost
