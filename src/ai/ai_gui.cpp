/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file ai_gui.cpp %Window for configuring the AIs. */

#include "../stdafx.h"
#include "../error.h"
#include "../company_base.h"
#include "../window_func.h"
#include "../network/network.h"
#include "../settings_func.h"
#include "../network/network_content.h"
#include "../core/geometry_func.hpp"

#include "ai.hpp"
#include "ai_config.hpp"
#include "ai_info.hpp"
#include "../script/script_gui.h"

#include "table/strings.h"
#include "../table/sprites.h"
#include "../company_cmd.h"

#include "../safeguards.h"


/** Widgets for the configure AI window. */
static constexpr std::initializer_list<NWidgetPart> _nested_ai_config_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_MAUVE),
		NWidget(WWT_CAPTION, COLOUR_MAUVE), SetStringTip(STR_AI_CONFIG_CAPTION_AI, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_MAUVE, WID_AIC_BACKGROUND),
		NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_wide, 0), SetPadding(WidgetDimensions::unscaled.sparse),
			NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_sparse, 0),
				NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_wide, 0),
					NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
						NWidget(WWT_PUSHARROWBTN, COLOUR_YELLOW, WID_AIC_DECREASE_NUMBER), SetArrowWidgetTypeTip(AWV_DECREASE),
						NWidget(WWT_PUSHARROWBTN, COLOUR_YELLOW, WID_AIC_INCREASE_NUMBER), SetArrowWidgetTypeTip(AWV_INCREASE),
					EndContainer(),
					NWidget(WWT_TEXT, INVALID_COLOUR, WID_AIC_NUMBER), SetFill(1, 0),
				EndContainer(),
				NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_wide, 0),
					NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
						NWidget(WWT_PUSHARROWBTN, COLOUR_YELLOW, WID_AIC_DECREASE_INTERVAL), SetArrowWidgetTypeTip(AWV_DECREASE),
						NWidget(WWT_PUSHARROWBTN, COLOUR_YELLOW, WID_AIC_INCREASE_INTERVAL), SetArrowWidgetTypeTip(AWV_INCREASE),
					EndContainer(),
					NWidget(WWT_TEXT, INVALID_COLOUR, WID_AIC_INTERVAL), SetFill(1, 0),
				EndContainer(),
				NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
					NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_MOVE_UP), SetResize(1, 0), SetFill(1, 0), SetStringTip(STR_AI_CONFIG_MOVE_UP, STR_AI_CONFIG_MOVE_UP_TOOLTIP),
					NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_MOVE_DOWN), SetResize(1, 0), SetFill(1, 0), SetStringTip(STR_AI_CONFIG_MOVE_DOWN, STR_AI_CONFIG_MOVE_DOWN_TOOLTIP),
				EndContainer(),
			EndContainer(),
			NWidget(WWT_FRAME, COLOUR_MAUVE), SetStringTip(STR_AI_CONFIG_AI), SetPIP(0, WidgetDimensions::unscaled.vsep_sparse, 0),
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_MATRIX, COLOUR_MAUVE, WID_AIC_LIST), SetMinimalSize(288, 210), SetFill(1, 0), SetMatrixDataTip(1, 15, STR_AI_CONFIG_AILIST_TOOLTIP),
				EndContainer(),
				NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
					NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_CONFIGURE), SetFill(1, 0), SetStringTip(STR_AI_CONFIG_CONFIGURE, STR_AI_CONFIG_CONFIGURE_TOOLTIP),
					NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_START_STOP_TOGGLE), SetFill(1, 0), SetStringTip(STR_AI_CONFIG_START, STR_AI_CONFIG_START_TOOLTIP),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_wide, 0),
				NWidget(NWID_VERTICAL, NWidContainerFlag::EqualSize),
					NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_CHANGE), SetFill(1, 1), SetStringTip(STR_AI_CONFIG_CHANGE_AI, STR_AI_CONFIG_CHANGE_TOOLTIP),
					NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_CONTENT_DOWNLOAD), SetFill(1, 1), SetStringTip(STR_INTRO_ONLINE_CONTENT, STR_INTRO_TOOLTIP_ONLINE_CONTENT),
				EndContainer(),
				NWidget(NWID_VERTICAL, NWidContainerFlag::EqualSize),
					NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
						NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_OPEN_URL), SetResize(1, 0), SetFill(1, 0), SetStringTip(STR_CONTENT_OPEN_URL, STR_CONTENT_OPEN_URL_TOOLTIP),
						NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_TEXTFILE + TFT_README), SetFill(1, 1), SetResize(1, 0), SetStringTip(STR_TEXTFILE_VIEW_README, STR_TEXTFILE_VIEW_README_TOOLTIP),
					EndContainer(),
					NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
						NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_TEXTFILE + TFT_CHANGELOG), SetFill(1, 1), SetResize(1, 0), SetStringTip(STR_TEXTFILE_VIEW_CHANGELOG, STR_TEXTFILE_VIEW_CHANGELOG_TOOLTIP),
						NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_TEXTFILE + TFT_LICENSE), SetFill(1, 1), SetResize(1, 0), SetStringTip(STR_TEXTFILE_VIEW_LICENCE, STR_TEXTFILE_VIEW_LICENCE_TOOLTIP),
					EndContainer(),
				EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

/** Window definition for the configure AI window. */
static WindowDesc _ai_config_desc(
	WDP_CENTER, {}, 0, 0,
	WC_GAME_OPTIONS, WC_NONE,
	{},
	_nested_ai_config_widgets
);

/**
 * Window to configure which AIs will start.
 */
struct AIConfigWindow : public Window {
	CompanyID selected_slot = CompanyID::Invalid(); ///< The currently selected AI slot or \c CompanyID::Invalid().
	int line_height = 0; ///< Height of a single AI-name line.

	AIConfigWindow() : Window(_ai_config_desc)
	{
		this->InitNested(WN_GAME_OPTIONS_AI); // Initializes 'this->line_height' as a side effect.
		this->selected_slot = CompanyID::Invalid();
		this->OnInvalidateData(0);
	}

	void Close([[maybe_unused]] int data = 0) override
	{
		CloseWindowByClass(WC_SCRIPT_LIST);
		if (_game_mode == GM_MENU) CloseWindowByClass(WC_SCRIPT_SETTINGS);
		this->Window::Close();
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		switch (widget) {
			case WID_AIC_NUMBER:
				return GetString(STR_AI_CONFIG_MAX_COMPETITORS, GetGameSettings().difficulty.max_no_competitors);

			case WID_AIC_INTERVAL:
				return GetString(STR_AI_CONFIG_COMPETITORS_INTERVAL, GetGameSettings().difficulty.competitors_interval);

			default:
				return this->Window::GetWidgetString(widget, stringid);
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		switch (widget) {
			case WID_AIC_DECREASE_NUMBER:
			case WID_AIC_INCREASE_NUMBER:
			case WID_AIC_DECREASE_INTERVAL:
			case WID_AIC_INCREASE_INTERVAL:
				size = maxdim(size, NWidgetScrollbar::GetHorizontalDimension());
				break;

			case WID_AIC_LIST: {
				uint highest_icon = 0;
				static const SpriteID icons[] = { SPR_SCRIPT_DEAD, SPR_SCRIPT_ELIGIBLE, SPR_SCRIPT_ALIVE, SPR_SCRIPT_HUMAN, SPR_SCRIPT_RANDOM };
				for (uint i = 0; i < std::size(icons); i++) {
					highest_icon = std::max(highest_icon, GetSpriteSize(icons[i]).height);
				}
				this->line_height = std::max(static_cast<uint>(GetCharacterHeight(FS_NORMAL)), highest_icon) + padding.height;
				fill.height = resize.height = this->line_height;
				size.height = 15 * this->line_height;
				break;
			}

			case WID_AIC_START_STOP_TOGGLE: {
				Dimension dim = GetStringBoundingBox(STR_AI_CONFIG_START);
				dim = maxdim(dim, GetStringBoundingBox(STR_AI_CONFIG_STOP));

				dim.width += padding.width;
				dim.height += padding.height;
				size = maxdim(size, dim);
				break;
			}
		}
	}

	/**
	 * Can the AI config in the given company slot be edited?
	 * @param slot The slot to query.
	 * @return True if and only if the given AI Config slot can be edited.
	 */
	static bool IsEditable(CompanyID slot)
	{
		return slot < MAX_COMPANIES;
	}

	/**
	 * Get the current total number of valid AI owned Companies, dead and alive.
	 */
	static int GetCurrentNoAIs()
	{
		int current_no_ais = 0;
		for (const Company *c : Company::Iterate()) {
			if (c->is_ai) current_no_ais++;
		}

		return current_no_ais;
	}

	/**
	 * Get text to display for a given company slot.
	 * @param cid Company to display.
	 * @returns Text to display for company.
	 */
	std::string GetSlotText(CompanyID cid) const
	{
		if (const AIInfo *info = AIConfig::GetConfig(cid)->GetInfo(); info != nullptr) return info->GetName();
		return GetString(STR_AI_CONFIG_RANDOM_AI);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_AIC_LIST) return;

		uint widest_icon = 0;
		static const SpriteID icons[] = { SPR_SCRIPT_DEAD, SPR_SCRIPT_ELIGIBLE, SPR_SCRIPT_ALIVE, SPR_SCRIPT_HUMAN, SPR_SCRIPT_RANDOM };
		for (uint i = 0; i < std::size(icons); i++) {
			widest_icon = std::max(widest_icon, GetSpriteSize(icons[i]).width);
		}
		uint dead_x_offset = (widest_icon - GetSpriteSize(SPR_SCRIPT_DEAD).width) / 2;
		uint eligible_x_offset = (widest_icon - GetSpriteSize(SPR_SCRIPT_ELIGIBLE).width) / 2;
		uint alive_x_offset = (widest_icon - GetSpriteSize(SPR_SCRIPT_ALIVE).width) / 2;
		uint human_x_offset = (widest_icon - GetSpriteSize(SPR_SCRIPT_HUMAN).width) / 2;

		uint widest_cid = GetStringBoundingBox(GetString(STR_JUST_INT, GetParamMaxValue(MAX_COMPANIES))).width;

		Rect tr = r.Shrink(WidgetDimensions::scaled.matrix);

		bool rtl = _current_text_dir == TD_RTL;
		Rect icon_rect = tr.WithWidth(widest_icon, rtl);
		Rect rai_rect = tr.Indent(widest_icon + WidgetDimensions::scaled.hsep_normal, rtl).WithWidth(widest_icon, rtl);
		Rect cid_rect = tr.Indent(widest_icon + WidgetDimensions::scaled.hsep_normal + widest_icon + WidgetDimensions::scaled.hsep_wide, rtl).WithWidth(widest_cid, rtl);
		Rect ai_rect = tr.Indent(widest_icon + WidgetDimensions::scaled.hsep_normal + widest_icon + WidgetDimensions::scaled.hsep_wide + widest_cid + WidgetDimensions::scaled.hsep_wide, rtl);

		int max_slot = GetGameSettings().difficulty.max_no_competitors - GetCurrentNoAIs();
		for (CompanyID cid = CompanyID::Begin(); cid < max_slot && cid < MAX_COMPANIES; ++cid) {
			if (Company::IsValidID(cid)) max_slot++;
		}

		for (CompanyID cid = CompanyID::Begin(); cid < MAX_COMPANIES; ++cid) {
			if (cid < max_slot && (_game_mode != GM_MENU || cid != CompanyID::Begin()) && (_game_mode == GM_MENU || !Company::IsValidID(cid))) {
				DrawSprite(SPR_SCRIPT_ELIGIBLE, PAL_NONE, icon_rect.left + eligible_x_offset, tr.top);
			} else if (Company::IsValidHumanID(cid)) {
				DrawSprite(SPR_SCRIPT_HUMAN, PAL_NONE, icon_rect.left + human_x_offset, tr.top);
			} else if (Company::IsValidAiID(cid)) {
				if (!IsConsideredDead(cid)) {
					DrawSprite(SPR_SCRIPT_ALIVE, PAL_NONE, icon_rect.left + alive_x_offset, tr.top);
				} else {
					DrawSprite(SPR_SCRIPT_DEAD, PAL_NONE, icon_rect.left + dead_x_offset, tr.top);
				}
				if (AIConfig::GetConfig(cid, AIConfig::SSS_FORCE_GAME)->GetInfo() == nullptr) {
					DrawSprite(SPR_SCRIPT_RANDOM, PAL_NONE, rai_rect.left, tr.top);
				}
			}

			DrawString(cid_rect.left, cid_rect.right, tr.top, GetString(STR_JUST_INT, cid + 1), TC_LIGHT_BLUE);

			DrawString(ai_rect.left, ai_rect.right, tr.top, this->GetSlotText(cid), this->selected_slot == cid ? TC_WHITE : TC_ORANGE);
			tr.top += this->line_height;
		}
	}

	/**
	 * Given the current selected Company slot and a direction to search,
	 * get the first non-AI Company slot that is found.
	 * @note Returns INVALID_COMPANY if none was found.
	 * @note A direction must be specified.
	 * @param dir The direction to search (-1 to search above, 1 to search below)
	 * @pre (dir == -1 || dir == 1)
	 * @return the first non-AI CompanyID that is found from the given direction
	 */
	CompanyID GetFreeSlot(int dir = 0)
	{
		assert(dir == -1 || dir == 1);

		if (dir == -1) {
			for (CompanyID slot = CompanyID(this->selected_slot - 1); slot >= CompanyID::Begin(); slot = CompanyID(slot - 1)) {
				if (!Company::IsValidAiID(slot)) return slot;
			}
		} else if (dir == 1) {
			for (CompanyID slot = CompanyID(this->selected_slot + 1); slot < MAX_COMPANIES; ++slot) {
				if (!Company::IsValidAiID(slot)) return slot;
			}
		}

		return CompanyID::Invalid();
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget >= WID_AIC_TEXTFILE && widget < WID_AIC_TEXTFILE + TFT_CONTENT_END) {
			if (this->selected_slot == CompanyID::Invalid() || AIConfig::GetConfig(this->selected_slot) == nullptr) return;

			ShowScriptTextfileWindow(this, static_cast<TextfileType>(widget - WID_AIC_TEXTFILE), this->selected_slot);
			return;
		}

		switch (widget) {
			case WID_AIC_DECREASE_NUMBER:
			case WID_AIC_INCREASE_NUMBER: {
				int new_value;
				if (widget == WID_AIC_DECREASE_NUMBER) {
					new_value = std::max(0, GetGameSettings().difficulty.max_no_competitors - 1);
				} else {
					new_value = std::min<int>(MAX_COMPANIES, GetGameSettings().difficulty.max_no_competitors + 1);
				}
				IConsoleSetSetting("difficulty.max_no_competitors", new_value);
				this->InvalidateData();
				break;
			}

			case WID_AIC_DECREASE_INTERVAL:
			case WID_AIC_INCREASE_INTERVAL: {
				int new_value;
				if (widget == WID_AIC_DECREASE_INTERVAL) {
					new_value = std::max(static_cast<int>(MIN_COMPETITORS_INTERVAL), GetGameSettings().difficulty.competitors_interval - 1);
				} else {
					new_value = std::min(static_cast<int>(MAX_COMPETITORS_INTERVAL), GetGameSettings().difficulty.competitors_interval + 1);
				}
				IConsoleSetSetting("difficulty.competitors_interval", new_value);
				this->InvalidateData();
				break;
			}

			case WID_AIC_LIST: { // Select a slot
				this->selected_slot = static_cast<CompanyID>(this->GetRowFromWidget(pt.y, widget, 0, this->line_height));
				this->InvalidateData();
				if (click_count > 1 && this->selected_slot != CompanyID::Invalid()) {
					if (!Company::IsValidAiID(this->selected_slot)) {
						ShowScriptListWindow(this->selected_slot, _ctrl_pressed);
					} else {
						if (!GetVisibleSettingsList(this->selected_slot).empty()) ShowScriptSettingsWindow(this->selected_slot);
					}
				}
				break;
			}

			case WID_AIC_MOVE_UP: {
				CompanyID slot_above = this->GetFreeSlot(-1);
				if (IsEditable(this->selected_slot) && !Company::IsValidAiID(this->selected_slot) && IsEditable(slot_above)) {
					std::swap(GetGameSettings().script_config.ai[this->selected_slot], GetGameSettings().script_config.ai[slot_above]);
					this->selected_slot = slot_above;
					this->InvalidateData();
				}
				break;
			}

			case WID_AIC_MOVE_DOWN: {
				CompanyID slot_below = this->GetFreeSlot(1);
				if (IsEditable(this->selected_slot) && !Company::IsValidAiID(this->selected_slot) && IsEditable(slot_below)) {
					std::swap(GetGameSettings().script_config.ai[this->selected_slot], GetGameSettings().script_config.ai[slot_below]);
					this->selected_slot = slot_below;
					this->InvalidateData();
				}
				break;
			}

			case WID_AIC_OPEN_URL: {
				const AIConfig *config = AIConfig::GetConfig(this->selected_slot);
				if (this->selected_slot == CompanyID::Invalid() || config == nullptr || config->GetInfo() == nullptr) return;
				OpenBrowser(config->GetInfo()->GetURL());
				break;
			}

			case WID_AIC_CHANGE:  // choose other AI
				if (IsEditable(this->selected_slot) && (_game_mode != GM_NORMAL || !Company::IsValidAiID(this->selected_slot))) ShowScriptListWindow(this->selected_slot, _ctrl_pressed);
				break;

			case WID_AIC_CONFIGURE: // change the settings for an AI
				if (IsEditable(this->selected_slot) && !GetVisibleSettingsList(this->selected_slot).empty()) ShowScriptSettingsWindow(this->selected_slot);
				break;

			case WID_AIC_START_STOP_TOGGLE: // start or stop an AI
				if (_game_mode == GM_NORMAL && !Company::IsValidHumanID(this->selected_slot)) {
					if (Company::IsValidAiID(this->selected_slot)) { // clicking 'Stop AI'
						Command<Commands::CompanyControl>::Post(CCA_DELETE, this->selected_slot, CRR_MANUAL, INVALID_CLIENT_ID); // to stop an AI, remove company with AI
					} else { // clicking 'Start AI'
						if (GetCurrentNoAIs() < GetGameSettings().difficulty.max_no_competitors) {
							if (AI::CanStartNew()) {
								Command<Commands::CompanyControl>::Post(CCA_NEW_AI, this->selected_slot, CRR_NONE, INVALID_CLIENT_ID); // start company for AI
							} else {
								ShowErrorMessage(GetEncodedString(STR_ERROR_AI_CAN_T_START), GetEncodedString(STR_ERROR_AI_ALLOW_IN_MULTIPLAYER), WL_INFO);
							}
						}
					}
				}
				break;

			case WID_AIC_CONTENT_DOWNLOAD:
				if (!_network_available) {
					ShowErrorMessage(GetEncodedString(STR_NETWORK_ERROR_NOTAVAILABLE), {}, WL_ERROR);
				} else {
					ShowNetworkContentListWindow(nullptr, CONTENT_TYPE_AI);
				}
				break;
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!IsEditable(this->selected_slot)) {
			this->selected_slot = CompanyID::Invalid();
		}

		if (!gui_scope) return;

		this->SetWidgetDisabledState(WID_AIC_DECREASE_NUMBER, GetGameSettings().difficulty.max_no_competitors == 0);
		this->SetWidgetDisabledState(WID_AIC_INCREASE_NUMBER, GetGameSettings().difficulty.max_no_competitors == MAX_COMPANIES);
		this->SetWidgetDisabledState(WID_AIC_DECREASE_INTERVAL, GetGameSettings().difficulty.competitors_interval == MIN_COMPETITORS_INTERVAL);
		this->SetWidgetDisabledState(WID_AIC_INCREASE_INTERVAL, GetGameSettings().difficulty.competitors_interval == MAX_COMPETITORS_INTERVAL);
		this->SetWidgetDisabledState(WID_AIC_MOVE_UP, this->selected_slot == CompanyID::Invalid() || Company::IsValidAiID(this->selected_slot) || this->GetFreeSlot(-1) == CompanyID::Invalid());
		this->SetWidgetDisabledState(WID_AIC_MOVE_DOWN, this->selected_slot == CompanyID::Invalid() || Company::IsValidAiID(this->selected_slot) || this->GetFreeSlot(1) == CompanyID::Invalid());
		this->SetWidgetDisabledState(WID_AIC_CHANGE, this->selected_slot == CompanyID::Invalid() || (_game_mode == GM_NORMAL && Company::IsValidAiID(this->selected_slot)));
		this->SetWidgetDisabledState(WID_AIC_CONFIGURE, this->selected_slot == CompanyID::Invalid() || GetVisibleSettingsList(this->selected_slot).empty());
		this->SetWidgetDisabledState(WID_AIC_START_STOP_TOGGLE, _game_mode != GM_NORMAL || this->selected_slot == CompanyID::Invalid() || Company::IsValidHumanID(this->selected_slot) || (GetCurrentNoAIs() >= GetGameSettings().difficulty.max_no_competitors && !Company::IsValidID(this->selected_slot)));

		/* Display either Start AI or Stop AI button */
		NWidgetCore *toggle_button = this->GetWidget<NWidgetCore>(WID_AIC_START_STOP_TOGGLE);
		if (this->selected_slot == CompanyID::Invalid() || Company::IsValidHumanID(this->selected_slot) || !Company::IsValidAiID(this->selected_slot)) {
			toggle_button->SetStringTip(STR_AI_CONFIG_START, STR_AI_CONFIG_START_TOOLTIP);
		} else {
			toggle_button->SetStringTip(STR_AI_CONFIG_STOP, STR_AI_CONFIG_STOP_TOOLTIP);
		}

		AIConfig *config = this->selected_slot == CompanyID::Invalid() ? nullptr : AIConfig::GetConfig(this->selected_slot);
		this->SetWidgetDisabledState(WID_AIC_OPEN_URL, this->selected_slot == CompanyID::Invalid() || config->GetInfo() == nullptr || config->GetInfo()->GetURL().empty());
		for (TextfileType tft = TFT_CONTENT_BEGIN; tft < TFT_CONTENT_END; tft++) {
			this->SetWidgetDisabledState(WID_AIC_TEXTFILE + tft, this->selected_slot == CompanyID::Invalid() || !config->GetTextfile(tft, this->selected_slot).has_value());
		}
	}
};

/** Open the AI config window. */
void ShowAIConfigWindow()
{
	CloseWindowByClass(WC_GAME_OPTIONS);
	new AIConfigWindow();
}
