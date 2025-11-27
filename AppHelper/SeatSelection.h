#pragma once
#include <wx/window.h>
#include <wx/string.h>
#include <string>

// Открывает диалог выбора места для рейса flightNumber.
// currentSeat — текущее значение (для предвыбора).
// Если пользователь выбрал место и нажал OK -> true и outSeat заполнен.
// Если отменил или была ошибка -> false.
bool ChooseSeatForFlight(wxWindow* parent, const std::string& flightNumber, const wxString& currentSeat, wxString& outSeat);
