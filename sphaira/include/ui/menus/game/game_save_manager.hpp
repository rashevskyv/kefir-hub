#pragma once

#include "fs.hpp"
#include <functional>
#include <string>
#include <vector>

namespace sphaira::ui::menu::game {

struct GameSaveRow {
    FsSaveDataInfo info{};
    std::string account{};
    FsSaveDataExtraData extra{};
    Result extra_rc{0};
    bool has_extra{false};
};

auto GetSaveSpaceLabel(u8 space_id) -> std::string;
auto FormatSaveInfoMessage(const GameSaveRow& row) -> std::string;

void LoadGameSaves(u64 app_id, std::vector<GameSaveRow>& out_saves, u64& out_allocated_size);

void PromptCreateSaveSlot(
    u64 app_id,
    const std::string& game_name,
    const std::vector<GameSaveRow>& current_saves,
    std::function<void()> on_refresh
);

void PromptIncreaseSaveSize(
    const std::string& game_name,
    const GameSaveRow& row,
    std::function<void()> on_refresh
);

} // namespace sphaira::ui::menu::game
