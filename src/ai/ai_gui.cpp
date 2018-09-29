/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file ai_gui.cpp %Window for configuring the AIs */

#include "../stdafx.h"
#include "../error.h"
#include "../company_base.h"
#include "../window_func.h"
#include "../network/network.h"
#include "../settings_func.h"
#include "../network/network_content.h"
#include "../core/geometry_func.hpp"

#include "ai.hpp"
#include "ai_gui.hpp"
#include "ai_config.hpp"
#include "ai_info.hpp"
#include "../script/script_gui.h"
#include "table/strings.h"
#include "table/sprites.h"
#include "company_cmd.h"

#include "../safeguards.h"


/** Widgets for the configure AI window. */
static const NWidgetPart _nested_ai_config_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_MAUVE),
		NWidget(WWT_CAPTION, COLOUR_MAUVE), SetDataTip(STR_AI_CONFIG_CAPTION_AI, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_MAUVE, WID_AIC_BACKGROUND),
		NWidget(NWID_VERTICAL), SetPIP(4, 4, 4),
			NWidget(NWID_HORIZONTAL), SetPIP(7, 0, 7),
				NWidget(WWT_PUSHARROWBTN, COLOUR_YELLOW, WID_AIC_DECREASE), SetDataTip(AWV_DECREASE, STR_NULL),
				NWidget(WWT_PUSHARROWBTN, COLOUR_YELLOW, WID_AIC_INCREASE), SetDataTip(AWV_INCREASE, STR_NULL),
				NWidget(NWID_SPACER), SetMinimalSize(6, 0),
				NWidget(WWT_TEXT, COLOUR_MAUVE, WID_AIC_NUMBER), SetDataTip(STR_AI_CONFIG_MAX_COMPETITORS, STR_NULL), SetFill(1, 0),
			EndContainer(),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(7, 0, 7),
				NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_MOVE_UP), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_AI_CONFIG_MOVE_UP, STR_AI_CONFIG_MOVE_UP_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_MOVE_DOWN), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_AI_CONFIG_MOVE_DOWN, STR_AI_CONFIG_MOVE_DOWN_TOOLTIP),
			EndContainer(),
		EndContainer(),
		NWidget(WWT_FRAME, COLOUR_MAUVE), SetDataTip(STR_AI_CONFIG_AI, STR_NULL), SetPadding(0, 5, 0, 5),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_MATRIX, COLOUR_MAUVE, WID_AIC_LIST), SetMinimalSize(288, 210), SetFill(1, 0), SetMatrixDataTip(1, 15, STR_AI_CONFIG_AILIST_TOOLTIP),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 9),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(7, 0, 7),
			NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_CHANGE), SetFill(1, 0), SetMinimalSize(93, 0), SetDataTip(STR_AI_CONFIG_CHANGE_AI, STR_AI_CONFIG_CHANGE_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_CONFIGURE), SetFill(1, 0), SetMinimalSize(93, 0), SetDataTip(STR_AI_CONFIG_CONFIGURE, STR_AI_CONFIG_CONFIGURE_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_START_STOP_TOGGLE), SetFill(1, 0), SetMinimalSize(93, 0), SetDataTip(STR_AI_CONFIG_START, STR_AI_CONFIG_START_TOOLTIP),
		EndContainer(),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(7, 0, 7),
			NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_TEXTFILE + TFT_README), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TEXTFILE_VIEW_README, STR_NULL),
			NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_TEXTFILE + TFT_CHANGELOG), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TEXTFILE_VIEW_CHANGELOG, STR_NULL),
			NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_TEXTFILE + TFT_LICENSE), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TEXTFILE_VIEW_LICENCE, STR_NULL),
		EndContainer(),
		NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_CONTENT_DOWNLOAD), SetFill(1, 0), SetMinimalSize(279, 0), SetPadding(0, 7, 9, 7), SetDataTip(STR_INTRO_ONLINE_CONTENT, STR_INTRO_TOOLTIP_ONLINE_CONTENT),
	EndContainer(),
};

/** Window definition for the configure AI window. */
static WindowDesc _ai_config_desc(
	WDP_CENTER, "settings_script_config", 0, 0,
	WC_GAME_OPTIONS, WC_NONE,
	0,
	_nested_ai_config_widgets, lengthof(_nested_ai_config_widgets)
);

/**
 * Window to configure which AIs will start.
 */
struct AIConfigWindow : public Window {
	CompanyID selected_slot; ///< The currently selected AI slot or \c INVALID_COMPANY.
	int line_height;         ///< Height of a single AI-name line.

	AIConfigWindow() : Window(&_ai_config_desc)
	{
		this->InitNested(WN_GAME_OPTIONS_AI); // Initializes 'this->line_height' as a side effect.
		this->selected_slot = INVALID_COMPANY;
		this->OnInvalidateData(0);
	}

	void Close() override
	{
		CloseWindowByClass(WC_SCRIPT_LIST);
		if (_game_mode == GM_MENU) CloseWindowByClass(WC_SCRIPT_SETTINGS);
		this->Window::Close();
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_AIC_NUMBER:
				SetDParam(0, GetGameSettings().difficulty.max_no_competitors);
				break;
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_AIC_DECREASE:
			case WID_AIC_INCREASE:
				*size = maxdim(*size, NWidgetScrollbar::GetHorizontalDimension());
				break;

			case WID_AIC_LIST: {
				uint highest_icon = 0;
				static const SpriteID icons[] = { SPR_SCRIPT_DEAD, SPR_SCRIPT_ELIGIBLE, SPR_SCRIPT_ALIVE, SPR_SCRIPT_HUMAN, SPR_SCRIPT_RANDOM };
				for (uint i = 0; i < lengthof(icons); i++) {
					highest_icon = std::max(highest_icon, GetSpriteSize(icons[i]).height);
				}
				this->line_height = std::max((uint)FONT_HEIGHT_NORMAL, highest_icon) + padding.height;
				resize->height = this->line_height;
				size->height = 15 * this->line_height;
				break;
			}

			case WID_AIC_START_STOP_TOGGLE: {
				Dimension dim = GetStringBoundingBox(STR_AI_CONFIG_START);
				dim = maxdim(dim, GetStringBoundingBox(STR_AI_CONFIG_STOP));

				dim.width += padding.width;
				dim.height += padding.height;
				*size = maxdim(*size, dim);
				break;
			}
		}
	}

	/**
	 * Can the AI config in the given company slot be selected?
	 * @param slot The slot to query.
	 * @return True if and only if the given AI Config slot can be selected.
	 */
	static bool IsSelectable(CompanyID slot)
	{
		return (slot >= COMPANY_FIRST && slot < MAX_COMPANIES);
	}

	/**
	 * Get the current total number of valid AI owned Companies, dead and alive.
	 */
	static int GetCurrentNoAIs()
	{
		int current_no_ais = 0;
		for (CompanyID cid = COMPANY_FIRST; cid < MAX_COMPANIES; cid++) {
			if (Company::IsValidAiID(cid)) current_no_ais++;
		}
		return current_no_ais;
	}

	/**
	 * Is the AI config in the given slot eligible to start?
	 * @param slot The slot to query.
	 * @return True if and only if the given AI Config of this slot can start.
	 */
	static bool IsEligible(CompanyID slot)
	{
		if (Company::IsValidID(slot) || slot < COMPANY_FIRST) return false;

		int empty_slots = 0;
		CompanyID max_slot;
		for (max_slot = COMPANY_FIRST; max_slot < MAX_COMPANIES && GetGameSettings().difficulty.max_no_competitors - GetCurrentNoAIs() > empty_slots; max_slot++) {
			if (!Company::IsValidID(max_slot)) empty_slots++;
		}
		return slot < max_slot;
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		if (widget != WID_AIC_LIST) return;

		uint widest_icon = 0;
		static const SpriteID icons[] = { SPR_SCRIPT_DEAD, SPR_SCRIPT_ELIGIBLE, SPR_SCRIPT_ALIVE, SPR_SCRIPT_HUMAN, SPR_SCRIPT_RANDOM };
		for (uint i = 0; i < lengthof(icons); i++) {
			widest_icon = std::max(widest_icon, GetSpriteSize(icons[i]).width);
		}
		uint dead_x_offset = (widest_icon - GetSpriteSize(SPR_SCRIPT_DEAD).width) / 2;
		uint eligible_x_offset = (widest_icon - GetSpriteSize(SPR_SCRIPT_ELIGIBLE).width) / 2;
		uint alive_x_offset = (widest_icon - GetSpriteSize(SPR_SCRIPT_ALIVE).width) / 2;
		uint human_x_offset = (widest_icon - GetSpriteSize(SPR_SCRIPT_HUMAN).width) / 2;

		SetDParamMaxValue(0, MAX_COMPANIES);
		uint widest_cid = GetStringBoundingBox(STR_JUST_COMMA).width;

		Rect tr = r.Shrink(WidgetDimensions::scaled.matrix);

		bool rtl = _current_text_dir == TD_RTL;
		Rect icon_rect = tr.WithWidth(widest_icon, rtl);
		Rect rai_rect = tr.Indent(widest_icon + WidgetDimensions::scaled.hsep_normal, rtl).WithWidth(widest_icon, rtl);
		Rect cid_rect = tr.Indent(widest_icon + WidgetDimensions::scaled.hsep_normal + widest_icon + WidgetDimensions::scaled.hsep_wide, rtl).WithWidth(widest_cid, rtl);
		Rect ai_rect = tr.Indent(widest_icon + WidgetDimensions::scaled.hsep_normal + widest_icon + WidgetDimensions::scaled.hsep_wide + widest_cid + WidgetDimensions::scaled.hsep_wide, rtl);

		for (CompanyID i = COMPANY_FIRST; i < MAX_COMPANIES; i++) {
			if (IsEligible(i)) {
				DrawSprite(SPR_SCRIPT_ELIGIBLE, PAL_NONE, icon_rect.left + eligible_x_offset, tr.top);
			} else {
				if (Company::IsValidHumanID(i)) {
					DrawSprite(SPR_SCRIPT_HUMAN, PAL_NONE, icon_rect.left + human_x_offset, tr.top);
				} else {
					if (Company::IsValidAiID(i)) {
						if (!IsConsideredDead(i)) {
							DrawSprite(SPR_SCRIPT_ALIVE, PAL_NONE, icon_rect.left + alive_x_offset, tr.top);
						} else {
							DrawSprite(SPR_SCRIPT_DEAD, PAL_NONE, icon_rect.left + dead_x_offset, tr.top);
						}
					}
				}
			}

			if (AIConfig::GetConfig(i)->GetInfo() != nullptr && AIConfig::GetConfig(i)->IsRandom()) {
				DrawSprite(SPR_SCRIPT_RANDOM, PAL_NONE, rai_rect.left, tr.top);
			}

			SetDParam(0, i + 1);
			DrawString(cid_rect.left, cid_rect.right, tr.top, STR_JUST_INT, TC_LIGHT_BLUE);

			StringID text;
			if (AIConfig::GetConfig(i)->GetInfo() != nullptr) {
				SetDParamStr(0, AIConfig::GetConfig(i)->GetInfo()->GetName());
				text = STR_JUST_RAW_STRING;
			} else {
				text = STR_AI_CONFIG_RANDOM_AI;
			}
			DrawString(ai_rect.left, ai_rect.right, tr.top, text, (this->selected_slot == i) ? TC_WHITE : TC_ORANGE);
			tr.top += this->line_height;
		}
	}

	/**
	 * Given the current selected Company slot and a direction to search,
	 * get the first non-AI Company slot that is found.
	 * @note Returns INVALID_COMPANY if none was found.
	 * @note A direction must be specified.
	 * @param slot The currently selected Company slot
	 * @param dir The direction to search (-1 to search above, +1 to search below)
	 * @pre (dir == -1 || dir == +1)
	 * @return the first non-AI CompanyID that is found from the given direction
	 */
	static CompanyID GetFreeSlot(CompanyID slot, int dir = 0)
	{
		assert(dir == -1 || dir == +1);
		if (dir == -1) {
			slot--;
			for (; slot >= COMPANY_FIRST; slot--) {
				if (!Company::IsValidAiID(slot)) return slot;
			}
		} else {
			if (dir == +1) {
				slot++;
				for (; slot < MAX_COMPANIES; slot++) {
					if (!Company::IsValidAiID(slot)) return slot;
				}
			}
		}
		return slot = INVALID_COMPANY;
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		if (widget >= WID_AIC_TEXTFILE && widget < WID_AIC_TEXTFILE + TFT_END) {
			if (this->selected_slot == INVALID_COMPANY || AIConfig::GetConfig(this->selected_slot) == nullptr) return;

			ShowScriptTextfileWindow((TextfileType)(widget - WID_AIC_TEXTFILE), this->selected_slot);
			return;
		}

		switch (widget) {
			case WID_AIC_DECREASE:
			case WID_AIC_INCREASE: {
				int new_value;
				if (widget == WID_AIC_DECREASE) {
					new_value = std::max(0, GetGameSettings().difficulty.max_no_competitors - 1);
				} else {
					new_value = std::min((int)MAX_COMPANIES, GetGameSettings().difficulty.max_no_competitors + 1);
				}
				IConsoleSetSetting("difficulty.max_no_competitors", new_value);
				break;
			}

			case WID_AIC_LIST: { // Select a slot
				this->selected_slot = (CompanyID)this->GetRowFromWidget(pt.y, widget, 0, this->line_height);
				this->InvalidateData();
				if (click_count > 1 && this->selected_slot != INVALID_COMPANY) {
					if (!Company::IsValidAiID(this->selected_slot)) {
						ShowScriptListWindow(this->selected_slot);
					} else {
						if (!GetVisibleSettingsList(this->selected_slot).empty()) ShowScriptSettingsWindow(this->selected_slot);
					}
				}
				break;
			}

			case WID_AIC_MOVE_UP: {
				CompanyID slot_above = GetFreeSlot(this->selected_slot, -1);
				if (IsSelectable(this->selected_slot) && !Company::IsValidAiID(this->selected_slot) && IsSelectable(slot_above)) {
					Swap(GetGameSettings().ai_config[this->selected_slot], GetGameSettings().ai_config[slot_above]);
					this->selected_slot = slot_above;
					this->InvalidateData();
				}
				break;
			}

			case WID_AIC_MOVE_DOWN: {
				CompanyID slot_below = GetFreeSlot(this->selected_slot, +1);
				if (IsSelectable(this->selected_slot) && !Company::IsValidAiID(this->selected_slot) && IsSelectable(slot_below)) {
					Swap(GetGameSettings().ai_config[this->selected_slot], GetGameSettings().ai_config[slot_below]);
					this->selected_slot = slot_below;
					this->InvalidateData();
				}
				break;
			}

			case WID_AIC_CHANGE:  // choose other AI
				ShowScriptListWindow(this->selected_slot);
				break;

			case WID_AIC_CONFIGURE: // change the settings for an AI
				ShowScriptSettingsWindow(this->selected_slot);
				break;

			case WID_AIC_START_STOP_TOGGLE: // start or stop an AI
				if (_game_mode == GM_NORMAL && !Company::IsValidHumanID(this->selected_slot)) {
					if (Company::IsValidAiID(this->selected_slot)) { // clicking 'Stop AI'
						Command<CMD_COMPANY_CTRL>::Post(CCA_DELETE, this->selected_slot, CRR_MANUAL, INVALID_CLIENT_ID); // to stop an AI, remove company with AI
					} else { // clicking 'Start AI'
						if (GetCurrentNoAIs() < GetGameSettings().difficulty.max_no_competitors) {
							if (AI::CanStartNew()) {
								Command<CMD_COMPANY_CTRL>::Post(CCA_NEW_AI, this->selected_slot, CRR_NONE, INVALID_CLIENT_ID); // start company for AI
							} else {
								ShowErrorMessage(STR_ERROR_AI_CAN_T_START, STR_ERROR_AI_ALLOW_IN_MULTIPLAYER, WL_INFO);
							}
						}
					}
				}
				break;

			case WID_AIC_CONTENT_DOWNLOAD:
				if (!_network_available) {
					ShowErrorMessage(STR_NETWORK_ERROR_NOTAVAILABLE, INVALID_STRING_ID, WL_ERROR);
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
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!IsSelectable(this->selected_slot)) {
			this->selected_slot = INVALID_COMPANY;
		}

		if (!gui_scope) return;

		this->SetWidgetDisabledState(WID_AIC_DECREASE, GetGameSettings().difficulty.max_no_competitors == 0);
		this->SetWidgetDisabledState(WID_AIC_INCREASE, GetGameSettings().difficulty.max_no_competitors == MAX_COMPANIES);
		this->SetWidgetDisabledState(WID_AIC_MOVE_UP, this->selected_slot == INVALID_COMPANY || Company::IsValidAiID(this->selected_slot) || GetFreeSlot(this->selected_slot, -1) == INVALID_COMPANY);
		this->SetWidgetDisabledState(WID_AIC_MOVE_DOWN, this->selected_slot == INVALID_COMPANY || Company::IsValidAiID(this->selected_slot) || GetFreeSlot(this->selected_slot, +1) == INVALID_COMPANY);
		this->SetWidgetDisabledState(WID_AIC_CHANGE, this->selected_slot == INVALID_COMPANY || Company::IsValidAiID(this->selected_slot));
		this->SetWidgetDisabledState(WID_AIC_CONFIGURE, this->selected_slot == INVALID_COMPANY || GetVisibleSettingsList(this->selected_slot).empty());
		this->SetWidgetDisabledState(WID_AIC_START_STOP_TOGGLE, _game_mode != GM_NORMAL || this->selected_slot == INVALID_COMPANY || Company::IsValidHumanID(this->selected_slot) || (GetCurrentNoAIs() >= GetGameSettings().difficulty.max_no_competitors && !Company::IsValidID(this->selected_slot)));

		/* Display either Start AI or Stop AI button */
		NWidgetCore *toggle_button = this->GetWidget<NWidgetCore>(WID_AIC_START_STOP_TOGGLE);
		if (this->selected_slot == INVALID_COMPANY || Company::IsValidHumanID(this->selected_slot) || !Company::IsValidAiID(this->selected_slot)) {
			toggle_button->SetDataTip(STR_AI_CONFIG_START, STR_AI_CONFIG_START_TOOLTIP);
		} else {
			toggle_button->SetDataTip(STR_AI_CONFIG_STOP, STR_AI_CONFIG_STOP_TOOLTIP);
		}

		for (TextfileType tft = TFT_BEGIN; tft < TFT_END; tft++) {
			this->SetWidgetDisabledState(WID_AIC_TEXTFILE + tft, this->selected_slot == INVALID_COMPANY || (AIConfig::GetConfig(this->selected_slot)->GetTextfile(tft, this->selected_slot) == nullptr));
		}
	}
};

/** Open the AI config window. */
void ShowAIConfigWindow()
{
	CloseWindowByClass(WC_GAME_OPTIONS);
	new AIConfigWindow();
}
