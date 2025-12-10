#pragma once

#include <wx/window.h>
#include <wx/string.h>
#include <string>

bool ChooseFlight(wxWindow* parent,const std::string& prefix,wxString& chosenFlight);
