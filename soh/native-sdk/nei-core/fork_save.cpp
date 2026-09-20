// Persistencia do NeiSaveData do fork no namespace nei.state do coremod.
#include "fork_save.h"

#include <cstring>
#include <limits>
#include <string>

#include <nlohmann/json.hpp>

#include "fork/nei_save_bridge.h"

namespace LinkSpanNei {
namespace {

using Json = nlohmann::json;

uint64_t MaxUnsigned(uint32_t size) {
    switch (size) {
        case 1:
            return std::numeric_limits<uint8_t>::max();
        case 2:
            return std::numeric_limits<uint16_t>::max();
        case 4:
            return std::numeric_limits<uint32_t>::max();
        default:
            return 0;
    }
}

bool ReadValue(const Json& value, const NeiSaveField& field, int64_t* signedValue, uint64_t* unsignedValue) {
    if (field.isSigned) {
        if (!value.is_number_integer()) {
            return false;
        }
        const int64_t number = value.get<int64_t>();
        const int64_t minimum = field.elementSize == 1 ? std::numeric_limits<int8_t>::min()
                                : field.elementSize == 2 ? std::numeric_limits<int16_t>::min()
                                                        : std::numeric_limits<int32_t>::min();
        const int64_t maximum = field.elementSize == 1 ? std::numeric_limits<int8_t>::max()
                                : field.elementSize == 2 ? std::numeric_limits<int16_t>::max()
                                                        : std::numeric_limits<int32_t>::max();
        if (field.elementSize != 1 && field.elementSize != 2 && field.elementSize != 4 || number < minimum ||
            number > maximum) {
            return false;
        }
        *signedValue = number;
        return true;
    }
    if (!value.is_number_unsigned() && !value.is_number_integer()) {
        return false;
    }
    if (value.is_number_integer() && value.get<int64_t>() < 0) {
        return false;
    }
    const uint64_t number = value.is_number_unsigned() ? value.get<uint64_t>() : static_cast<uint64_t>(value.get<int64_t>());
    if (number > MaxUnsigned(field.elementSize)) {
        return false;
    }
    *unsignedValue = number;
    return true;
}

Json WriteValue(const uint8_t* address, const NeiSaveField& field) {
    if (field.isSigned) {
        switch (field.elementSize) {
            case 1:
                return *reinterpret_cast<const int8_t*>(address);
            case 2:
                return *reinterpret_cast<const int16_t*>(address);
            case 4:
                return *reinterpret_cast<const int32_t*>(address);
            default:
                return nullptr;
        }
    }
    switch (field.elementSize) {
        case 1:
            return *reinterpret_cast<const uint8_t*>(address);
        case 2:
            return *reinterpret_cast<const uint16_t*>(address);
        case 4:
            return *reinterpret_cast<const uint32_t*>(address);
        default:
            return nullptr;
    }
}

void StoreValue(uint8_t* address, const NeiSaveField& field, int64_t signedValue, uint64_t unsignedValue) {
    if (field.isSigned) {
        switch (field.elementSize) {
            case 1:
                *reinterpret_cast<int8_t*>(address) = static_cast<int8_t>(signedValue);
                break;
            case 2:
                *reinterpret_cast<int16_t*>(address) = static_cast<int16_t>(signedValue);
                break;
            case 4:
                *reinterpret_cast<int32_t*>(address) = static_cast<int32_t>(signedValue);
                break;
        }
        return;
    }
    switch (field.elementSize) {
        case 1:
            *reinterpret_cast<uint8_t*>(address) = static_cast<uint8_t>(unsignedValue);
            break;
        case 2:
            *reinterpret_cast<uint16_t*>(address) = static_cast<uint16_t>(unsignedValue);
            break;
        case 4:
            *reinterpret_cast<uint32_t*>(address) = static_cast<uint32_t>(unsignedValue);
            break;
    }
}

} // namespace

void ForkSave::Attach(const ShipOotSaveV1* save, uint64_t handle) {
    mSave = save;
    mSaveHandle = handle;
}

void ForkSave::Detach() {
    mSave = nullptr;
    mSaveHandle = 0;
}

std::string ForkSave::SerializeForTests() const {
    Json doc = Json::object();
    uint32_t count = 0;
    const NeiSaveField* fields = NeiSave_Fields(&count);
    const auto* data = static_cast<const uint8_t*>(NeiSave_Data());
    for (uint32_t i = 0; i < count; ++i) {
        const NeiSaveField& field = fields[i];
        const uint8_t* address = data + field.offset;
        if (field.count == 1) {
            doc[field.name] = WriteValue(address, field);
            continue;
        }
        Json array = Json::array();
        for (uint32_t element = 0; element < field.count; ++element) {
            array.push_back(WriteValue(address + element * field.elementSize, field));
        }
        doc[field.name] = std::move(array);
    }
    return doc.dump();
}

void ForkSave::OnSaveLoaded() {
    // Igual ao NeiSave_Init/Load: cada arquivo começa com os sentinelas do fork.
    NeiSave_Reset();
    LoadFields();
    NeiSave_AfterLoad();
}

void ForkSave::ResetForNewSlot() {
    // Mesmo caminho sem o bloco: é o NeiSave_Init do fork num arquivo que ainda não tem nada gravado.
    NeiSave_Reset();
    NeiSave_AfterLoad();
    mStoredVersion = 0;
}

void ForkSave::LoadFields() {
    if (!mSave || !mSaveHandle) {
        return;
    }
    // Bloco escrito por uma versão futura do NEI: os campos podem ter outro significado. O arquivo fica com os
    // sentinelas desta versão e o bloco não é regravado, para a v1 não apagar o que a v2 gravou.
    uint32_t stored = 0;
    mStoredVersion = 0;
    if (mSave->get_stored_version(mSaveHandle, &stored) == SHIP_NATIVE_OK) {
        mStoredVersion = stored;
        if (stored > kForkSaveVersion) {
            return;
        }
    }
    uint32_t size = 0;
    if (mSave->read(mSaveHandle, nullptr, 0, &size) != SHIP_NATIVE_OK || !size) {
        return;
    }
    std::string text(size, '\0');
    if (mSave->read(mSaveHandle, text.data(), size, &size) != SHIP_NATIVE_OK) {
        return;
    }
    text.resize(size);
    const Json doc = Json::parse(text, nullptr, false);
    if (!doc.is_object()) {
        return;
    }
    uint32_t count = 0;
    const NeiSaveField* fields = NeiSave_Fields(&count);
    auto* data = static_cast<uint8_t*>(NeiSave_Data());
    for (uint32_t i = 0; i < count; ++i) {
        const NeiSaveField& field = fields[i];
        const auto found = doc.find(field.name);
        if (found == doc.end()) {
            continue;
        }
        const Json& source = *found;
        if (field.count == 1) {
            int64_t signedValue = 0;
            uint64_t unsignedValue = 0;
            if (ReadValue(source, field, &signedValue, &unsignedValue)) {
                StoreValue(data + field.offset, field, signedValue, unsignedValue);
            }
            continue;
        }
        // Como o LoadArray do fork: array curto preenche o começo e o resto fica no inicial (tabelas que crescem,
        // como comboObtainedFc, continuam compatíveis); elemento inválido fica no inicial só nele.
        if (!source.is_array()) {
            continue;
        }
        const uint32_t present = source.size() < field.count ? static_cast<uint32_t>(source.size()) : field.count;
        for (uint32_t element = 0; element < present; ++element) {
            int64_t signedValue = 0;
            uint64_t unsignedValue = 0;
            if (ReadValue(source[element], field, &signedValue, &unsignedValue)) {
                StoreValue(data + field.offset + element * field.elementSize, field, signedValue, unsignedValue);
            }
        }
    }
}

void ForkSave::OnSaving() {
    if (!mSave || !mSaveHandle || mSave->get_slot() < 0 || mStoredVersion > kForkSaveVersion) {
        return;
    }
    try {
        const std::string json = SerializeForTests();
        mSave->write(mSaveHandle, json.data(), static_cast<uint32_t>(json.size()));
    } catch (...) {
    }
}

} // namespace LinkSpanNei
