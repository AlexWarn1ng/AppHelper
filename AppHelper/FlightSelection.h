#pragma once

#include <wx/window.h>
#include <wx/string.h>
#include <string>

// Открывает диалог выбора рейса.
//  prefix      – префикс (SU, DP и т.п.), по которому фильтруем рейсы;
//  chosenFlight – сюда вернём выбранный FlightNumber (или пусто, если отмена).
// Возвращает true, если диалог открылся без ошибок (даже если пользователь нажал Cancel).
bool ChooseFlight(wxWindow* parent,const std::string& prefix,wxString& chosenFlight);
