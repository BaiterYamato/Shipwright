#include "SohMenuModRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>

#include <imgui.h>

namespace SohGui {
namespace {

// Quadros de UI que o valor local de um slider solto espera a página publicada alcançá-lo.
constexpr int kEditingHoldFrames = 30;

std::string ValueText(const ShipLua::MenuValue& value) {
    return std::visit([](const auto& item) -> std::string {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, bool>) return item ? "On" : "Off";
        else if constexpr (std::is_same_v<T, std::string>) return item;
        else return std::to_string(item);
    }, value);
}

// O core já normaliza o valor para o tipo do slider; a leitura aceita os dois tipos numéricos por garantia.
std::int64_t IntegerOf(const ShipLua::MenuValue& value) {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) return *integer;
    if (const auto* real = std::get_if<double>(&value); real != nullptr && std::isfinite(*real)) {
        return static_cast<std::int64_t>(std::llround(*real));
    }
    return 0;
}

std::int64_t AddIntegerOffset(std::int64_t minimum, std::uint64_t offset) {
    if (minimum >= 0) return minimum + static_cast<std::int64_t>(offset);
    const auto magnitude = static_cast<std::uint64_t>(-(minimum + 1)) + 1;
    if (offset >= magnitude) return static_cast<std::int64_t>(offset - magnitude);
    const auto remaining = magnitude - offset;
    return -static_cast<std::int64_t>(remaining - 1) - 1;
}

std::int64_t QuantizeInteger(std::int64_t current, std::int64_t minimum,
                             std::int64_t maximum, std::uint64_t step) {
    current = std::clamp(current, minimum, maximum);
    // A subtração sem sinal preserva a distância mesmo quando a faixa cruza todo o domínio de int64_t.
    const auto offset = static_cast<std::uint64_t>(current) - static_cast<std::uint64_t>(minimum);
    const auto maximumOffset = static_cast<std::uint64_t>(maximum) - static_cast<std::uint64_t>(minimum);
    auto steps = offset / step;
    const auto remainder = offset % step;
    if (remainder >= step - step / 2) ++steps;
    if (steps > maximumOffset / step) return maximum;
    return AddIntegerOffset(minimum, steps * step);
}

double NumberOf(const ShipLua::MenuValue& value) {
    if (const auto* real = std::get_if<double>(&value)) return *real;
    if (const auto* integer = std::get_if<std::int64_t>(&value)) return static_cast<double>(*integer);
    return 0.0;
}

// O formato vai para o printf do ImGui junto com o valor; só formatos do tipo certo passam.
const char* IntegerFormat(const ShipLua::MenuWidget& widget) {
    if (widget.format == "%d%%" || widget.format == "%lld%%") return "%lld%%";
    return "%lld";
}

const char* NumberFormat(const ShipLua::MenuWidget& widget) {
    static constexpr const char* allowed[] = {"%.0f", "%.1f", "%.2f", "%.3f", "%.0f%%", "%.1f%%", "%.2f%%"};
    for (const char* format : allowed) {
        if (widget.format == format) return format;
    }
    return "%.3f";
}

void Tooltip(const ShipLua::MenuWidget& widget) {
    if (!widget.tooltip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", widget.tooltip.c_str());
}

} // namespace

ShipLua::Result<void> SohMenuModRegistry::Upsert(ShipLua::MenuPage page) {
    if (page.owner.empty() || page.id.empty() || page.columns == 0 || page.columns > 4) {
        return ShipLua::Result<void>::err(ShipLua::ErrorCode::InvalidArgument, "invalid mod menu page");
    }
    std::scoped_lock lock(mMutex);
    mPages[{page.owner, page.id}] = std::move(page);
    return ShipLua::Result<void>::ok();
}

void SohMenuModRegistry::RemoveMod(const std::string& modId) noexcept {
    try {
        std::scoped_lock lock(mMutex);
        std::erase_if(mPages, [&](const auto& entry) { return entry.first.first == modId; });
        std::erase_if(mInputs, [&](const auto& input) { return input.owner == modId; });
    } catch (...) {
    }
}

std::vector<ShipLua::MenuPage> SohMenuModRegistry::Snapshot() const {
    std::scoped_lock lock(mMutex);
    std::vector<ShipLua::MenuPage> pages;
    pages.reserve(mPages.size());
    for (const auto& [key, page] : mPages) { (void)key; pages.push_back(page); }
    return pages;
}

void SohMenuModRegistry::Enqueue(ShipLua::MenuInput input) {
    std::scoped_lock lock(mMutex);
    if (input.kind == ShipLua::MenuInputKind::Change) {
        // Slider arrastado gera um valor por quadro de UI; do mesmo widget, só o último ainda não entregue importa.
        const auto pending = std::find_if(mInputs.begin(), mInputs.end(), [&](const ShipLua::MenuInput& queued) {
            return queued.kind == ShipLua::MenuInputKind::Change && queued.owner == input.owner &&
                   queued.pageId == input.pageId && queued.widgetId == input.widgetId;
        });
        if (pending != mInputs.end()) {
            mInputs.erase(pending);
        }
    }
    if (mInputs.size() < 1024) mInputs.push_back(std::move(input));
}

std::vector<ShipLua::MenuInput> SohMenuModRegistry::DrainInputs(const std::string& modId) {
    std::scoped_lock lock(mMutex);
    std::vector<ShipLua::MenuInput> inputs;
    for (auto it = mInputs.begin(); it != mInputs.end();) {
        if (it->owner == modId) { inputs.push_back(std::move(*it)); it = mInputs.erase(it); }
        else ++it;
    }
    return inputs;
}

void SohMenuModRegistry::Draw() {
    const auto pages = Snapshot();
    for (auto editing = mEditing.begin(); editing != mEditing.end();) {
        const auto& [owner, pageId, widgetId, generation] = editing->first;
        const bool present = std::any_of(pages.begin(), pages.end(), [&](const ShipLua::MenuPage& page) {
            if (page.owner != owner || page.id != pageId || page.generation != generation) return false;
            return std::any_of(page.widgets.begin(), page.widgets.end(), [&](const ShipLua::MenuWidget& widget) {
                return widget.id == widgetId &&
                       (widget.type == ShipLua::MenuWidgetType::SliderInteger ||
                        widget.type == ShipLua::MenuWidgetType::SliderNumber);
            });
        });
        if (!present || --editing->second.framesLeft <= 0) editing = mEditing.erase(editing);
        else ++editing;
    }
    if (pages.empty()) {
        ImGui::TextDisabled("No loaded mod declared a menu page.");
        return;
    }
    for (const ShipLua::MenuPage& page : pages) {
        ImGui::PushID(page.owner.c_str());
        ImGui::PushID(page.id.c_str());
        const std::string heading = page.title + " - " + page.sidebar;
        if (ImGui::CollapsingHeader(heading.c_str(), ImGuiTreeNodeFlags_DefaultOpen) &&
            ImGui::BeginTable("mod-menu", static_cast<int>(page.columns), ImGuiTableFlags_SizingStretchSame)) {
            for (std::uint32_t column = 0; column < page.columns; ++column) {
                ImGui::TableNextColumn();
                for (const ShipLua::MenuWidget& widget : page.widgets) {
                    if (!widget.visible || widget.column != column) continue;
                    ImGui::PushID(widget.id.c_str());
                    ImGui::BeginDisabled(!widget.enabled);
                    bool changed = false;
                    ShipLua::MenuValue value = widget.value;
                    const bool slider = widget.type == ShipLua::MenuWidgetType::SliderInteger ||
                                        widget.type == ShipLua::MenuWidgetType::SliderNumber;
                    const EditingKey editingKey{page.owner, page.id, widget.id, page.generation};
                    auto editing = slider ? mEditing.find(editingKey) : mEditing.end();
                    if (editing != mEditing.end()) value = editing->second.value;
                    switch (widget.type) {
                        case ShipLua::MenuWidgetType::Checkbox: {
                            bool current = std::get_if<bool>(&value) != nullptr && std::get<bool>(value);
                            changed = ImGui::Checkbox(widget.label.c_str(), &current);
                            value = current;
                            break;
                        }
                        case ShipLua::MenuWidgetType::SliderInteger: {
                            std::int64_t current = IntegerOf(value);
                            const auto minimum = static_cast<std::int64_t>(std::ceil(widget.minimum));
                            const auto maximum = static_cast<std::int64_t>(std::floor(widget.maximum));
                            changed = ImGui::SliderScalar(widget.label.c_str(), ImGuiDataType_S64, &current,
                                                          &minimum, &maximum, IntegerFormat(widget));
                            if (changed && widget.step > 1.0) {
                                current = QuantizeInteger(current, minimum, maximum,
                                                          static_cast<std::uint64_t>(widget.step));
                            }
                            value = current;
                            break;
                        }
                        case ShipLua::MenuWidgetType::SliderNumber: {
                            double current = NumberOf(value);
                            changed = ImGui::SliderScalar(widget.label.c_str(), ImGuiDataType_Double, &current,
                                                          &widget.minimum, &widget.maximum, NumberFormat(widget));
                            if (changed && widget.step > 0.0) {
                                current = widget.minimum +
                                          std::round((current - widget.minimum) / widget.step) * widget.step;
                                current = std::clamp(current, widget.minimum, widget.maximum);
                            }
                            value = current;
                            break;
                        }
                        case ShipLua::MenuWidgetType::Choice: {
                            std::string preview = ValueText(value);
                            const auto selectedChoice = std::find_if(widget.choices.begin(), widget.choices.end(),
                                [&](const auto& choice) { return choice.value == value; });
                            if (selectedChoice != widget.choices.end()) preview = selectedChoice->label;
                            if (ImGui::BeginCombo(widget.label.c_str(), preview.c_str())) {
                                for (const auto& choice : widget.choices) {
                                    const bool selected = choice.value == value;
                                    if (ImGui::Selectable(choice.label.c_str(), selected)) { value = choice.value; changed = true; }
                                }
                                ImGui::EndCombo();
                            }
                            break;
                        }
                        case ShipLua::MenuWidgetType::Action:
                            if (ImGui::Button(widget.label.c_str())) {
                                Enqueue({page.owner, page.id, widget.id, page.generation,
                                         ShipLua::MenuInputKind::Activate, false});
                            }
                            break;
                        case ShipLua::MenuWidgetType::Text:
                            ImGui::TextWrapped("%s", widget.label.c_str());
                            break;
                        case ShipLua::MenuWidgetType::Separator:
                            if (!widget.label.empty()) ImGui::TextUnformatted(widget.label.c_str());
                            ImGui::Separator();
                            break;
                    }
                    Tooltip(widget);
                    if (slider) {
                        // Arrastando: o valor local manda. Solto: espera a página publicada chegar nele, com prazo,
                        // porque o core pode recusar o valor ou o jogo pode estar parado.
                        if (changed || ImGui::IsItemActive()) {
                            mEditing.insert_or_assign(editingKey, Editing{value, kEditingHoldFrames});
                        } else if (editing != mEditing.end() &&
                                   NumberOf(widget.value) == NumberOf(editing->second.value)) {
                            mEditing.erase(editing);
                        }
                    }
                    if (changed) Enqueue({page.owner, page.id, widget.id, page.generation,
                                          ShipLua::MenuInputKind::Change, std::move(value)});
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
        ImGui::PopID();
    }
}

} // namespace SohGui
