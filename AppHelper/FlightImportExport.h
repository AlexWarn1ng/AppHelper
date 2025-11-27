#pragma once
#include <wx/window.h>

std::string TimePointToSql(const std::chrono::system_clock::time_point& tp);
void ShowFlightImportExport(wxWindow* parent = nullptr);
