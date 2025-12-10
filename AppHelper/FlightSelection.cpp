#include "Header.h"
#include "FlightSelection.h"

#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/listbox.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/msgdlg.h>

#include <chrono>
#include <ctime>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>


class FlightSelectDialog : public wxDialog {
public:
    FlightSelectDialog(wxWindow* parent,
        const wxString& initialQuery)
        : wxDialog(parent, wxID_ANY, "Flights",
            wxDefaultPosition, wxSize(500, 500),
            wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    {
        // Загружаем рейсы из бд
        flights = Db::instance().QueryFlights();

        if (flights.empty()) {
            throw AppException("There are no flights in database.");
        }

        BuildDisplayData();

        auto* root = new wxBoxSizer(wxVERTICAL);

        auto* searchRow = new wxBoxSizer(wxHORIZONTAL);
        searchRow->Add(new wxStaticText(this, wxID_ANY, "Search:"), 0,
            wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
        search_ = new wxTextCtrl(this, wxID_ANY);
        searchRow->Add(search_, 1, wxEXPAND);
        root->Add(searchRow, 0, wxEXPAND | wxALL, 8);

        list_ = new wxListBox(this, wxID_ANY);
        root->Add(list_, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

        auto* btns = new wxStdDialogButtonSizer();
        auto* ok = new wxButton(this, wxID_OK, "OK");
        auto* cancel = new wxButton(this, wxID_CANCEL, "Cancel");
        btns->AddButton(ok);
        btns->AddButton(cancel);
        btns->Realize();
        root->Add(btns, 0, wxEXPAND | wxALL, 8);


        SetSizer(root);                     
        SetMinSize(wxSize(550, 550));      
        SetSize(wxSize(550, 500));        
        Layout();

        search_->SetValue(initialQuery);

        // события
        search_->Bind(wxEVT_TEXT, &FlightSelectDialog::OnSearchChanged, this);
        list_->Bind(wxEVT_LISTBOX_DCLICK, &FlightSelectDialog::OnListDClick, this);
        ok->Bind(wxEVT_BUTTON, &FlightSelectDialog::OnOK, this);

        ApplyFilter(initialQuery);
    }

    std::string SelectedFlightNumber() const {
        if (selectionIndex_ < 0 ||
            selectionIndex_ >= (int)visibleIndexes_.size())
            return {};
        const auto idx = visibleIndexes_[selectionIndex_];
        if (idx < 0 || idx >= (int)display.size())
            return {};
        return display[idx].flight.FLIGHTNUMBER;
    }

private:
    struct DisplayFlight {
        Flight      flight;
        std::string depCity;
        std::string arrCity;
        wxString    line;       // строка для списка
        std::string lineUpper;  
    };

    wxTextCtrl* search_{};
    wxListBox* list_{};

    std::vector<Flight>       flights;
    std::vector<DisplayFlight> display;
    std::vector<int>          visibleIndexes_;
    int                       selectionIndex_ = -1;

    void BuildDisplayData()
    {
        display.clear();
        display.reserve(flights.size());

        for (const auto& f : flights) {
            DisplayFlight df;
            df.flight = f;

            std::string depCode = f.DEPARTURE_AIRPORT;
            std::string arrCode = f.ARRIVAL_AIRPORT;
            df.depCity = depCode;
            df.arrCity = arrCode;

            try {
                auto depList = Airport::Search(depCode);
                if (!depList.empty())
                    df.depCity = depList.front().city;
            }
            catch (...) {}

            try {
                auto arrList = Airport::Search(arrCode);
                if (!arrList.empty())
                    df.arrCity = arrList.front().city;
            }
            catch (...) {}

            std::string depTime = ErrorHandler::TimePointToString(f.DEPARTURE_TIME);
            std::string arrTime = ErrorHandler::TimePointToString(f.ARRIVAL_TIME);


            df.line = wxString::Format(
                "%s  |  %s (%s) -> %s (%s)  |  %s -> %s",
                wxString::FromUTF8(f.FLIGHTNUMBER),
                wxString::FromUTF8(df.depCity),
                wxString::FromUTF8(depCode),
                wxString::FromUTF8(df.arrCity),
                wxString::FromUTF8(arrCode),
                wxString::FromUTF8(depTime),
                wxString::FromUTF8(arrTime)
            );

            // верх рег
            std::string u = std::string(df.line.mb_str());
            std::transform(u.begin(), u.end(), u.begin(),
                [](unsigned char c) { return (char)std::toupper(c); });
            df.lineUpper = std::move(u);

            display.push_back(std::move(df));
        }
    }

    void ApplyFilter(const wxString& qWx)
    {
        wxString trimmed = qWx;
        trimmed.Trim(true).Trim(false);

        std::string q = std::string(trimmed.mb_str());
        std::string qUpper;
        qUpper.reserve(q.size());
        for (char c : q)
            qUpper.push_back((char)std::toupper((unsigned char)c));

        list_->Clear();
        visibleIndexes_.clear();
        selectionIndex_ = -1;

        for (int i = 0; i < (int)display.size(); ++i) {
            const auto& df = display[i];

            bool match = false;
            if (qUpper.empty()) {
                match = true;          // пустой запрос = показать всё
            }
            else {
                if (df.lineUpper.find(qUpper) != std::string::npos)
                    match = true;
            }

            if (!match)
                continue;

            visibleIndexes_.push_back(i);
            list_->Append(df.line);
        }

        if (!visibleIndexes_.empty()) {
            selectionIndex_ = 0;
            list_->SetSelection(0);
        }
    }

    void OnSearchChanged(wxCommandEvent&)
    {
        ApplyFilter(search_->GetValue());
    }

    void OnListDClick(wxCommandEvent&)
    {
        // двойной клик это OK
        OnOKDummy();
    }

    void OnOK(wxCommandEvent&)
    {
        OnOKDummy();
    }

    void OnOKDummy()
    {
        int sel = list_->GetSelection();
        if (sel == wxNOT_FOUND || sel < 0 || sel >= (int)visibleIndexes_.size()) {
            wxMessageBox("Please select a flight.",
                "Flights", wxOK | wxICON_INFORMATION, this);
            return;
        }
        selectionIndex_ = sel;
        EndModal(wxID_OK);
    }
};


bool ChooseFlight(wxWindow* parent,
    const std::string& initialQuery,
    wxString& chosenFlight)
{
    chosenFlight.clear();
    std::string err;

    bool ok = ErrorHandler::Try([&] {

        wxString initWx = wxString::FromUTF8(initialQuery);
        FlightSelectDialog dlg(parent, initWx);

        if (dlg.ShowModal() != wxID_OK)
            return; // Cancel  не ошибка

        std::string fn = dlg.SelectedFlightNumber();
        chosenFlight = wxString::FromUTF8(fn);

        }, err);

    if (!ok) {
        wxMessageBox(wxString::FromUTF8(err.c_str()),
            "Select flight",
            wxOK | wxICON_ERROR, parent);
        return false;
    }
    return true;
}
