/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file company_func.h Functions related to companies. */

#ifndef COMPANY_FUNC_H
#define COMPANY_FUNC_H

#include "command_type.h"
#include "company_type.h"
#include "gfx_type.h"
#include "vehicle_type.h"

bool CheckTakeoverVehicleLimit(CompanyID cbig, CompanyID small);
void ChangeOwnershipOfCompanyItems(Owner old_owner, Owner new_owner);
std::array<StringParameter, 2> GetParamsForOwnedBy(Owner owner, TileIndex tile);
void SetLocalCompany(CompanyID new_company);
void ShowBuyCompanyDialog(CompanyID company, bool hostile_takeover);
void CompanyAdminUpdate(const Company *company);
void CompanyAdminBankrupt(CompanyID company_id);
void UpdateLandscapingLimits();
void UpdateCompanyLiveries(Company *c);

Money GetAvailableMoney(CompanyID company);
Money GetAvailableMoneyForCommand();
bool CheckCompanyHasMoney(CommandCost &cost);
void SubtractMoneyFromCompany(const CommandCost &cost);
void SubtractMoneyFromCompanyFract(CompanyID company, const CommandCost &cost);
CommandCost CheckOwnership(Owner owner, TileIndex tile = {});
CommandCost CheckTileOwnership(TileIndex tile);

extern CompanyID _local_company;
extern CompanyID _current_company;

extern TypedIndexContainer<std::array<Colours, MAX_COMPANIES>, CompanyID> _company_colours;
extern std::string _company_manager_face;
PaletteID GetCompanyPalette(CompanyID company);

/**
 * Is the current company the local company?
 * @return \c true of the current company is the local company, \c false otherwise.
 */
inline bool IsLocalCompany()
{
	return _local_company == _current_company;
}

/**
 * Is the user representing \a company?
 * @param company Company where interaction is needed with.
 * @return Gives \c true if the user can answer questions interactively as representative of \a company, else \c false
 */
inline bool IsInteractiveCompany(CompanyID company)
{
	return company == _local_company;
}

int CompanyServiceInterval(const Company *c, VehicleType type);
CompanyID GetFirstPlayableCompanyID();

#endif /* COMPANY_FUNC_H */
