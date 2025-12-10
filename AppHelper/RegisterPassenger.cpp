#include "Header.h"
#include "RegisterPassenger.h"
#include "SeatSelection.h"
#include "FlightSelection.h"

#include <wx/wx.h>
#include <wx/sizer.h>
#include <wx/choice.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>

#include <set>
#include <sstream>
#include <iomanip>

static int GetNextPassengerNumberFromDb()
{
    int maxId = 0;
    auto st = Db::instance().createStatement();
    std::unique_ptr<sql::ResultSet> rs(
        st->executeQuery(
            "SELECT MAX(CAST(SUBSTRING(PassengerID, 2) AS UNSIGNED)) AS MaxId "
            "FROM Passengers "
            "WHERE PassengerID REGEXP '^P[0-9]+'"
        ));

    if (rs->next() && !rs->isNull("MaxId")) {
        maxId = rs->getInt("MaxId");
    }
    return maxId + 1;
}

static std::string MakePassengerId(int num)
{
    std::ostringstream oss;
    oss << 'P' << std::setw(3) << std::setfill('0') << num;
    return oss.str();
}

class AddPassengerFrame : public wxFrame {
public:
    explicit AddPassengerFrame(wxWindow* parent = nullptr)
        : wxFrame(parent, wxID_ANY, "Register passenger",
            wxDefaultPosition, wxSize(520, 360))
    {
        auto* panel = new wxPanel(this);

        fullName_ = new wxTextCtrl(panel, wxID_ANY);
        docNumber = new wxTextCtrl(panel, wxID_ANY);

        flight = new wxTextCtrl(
            panel, wxID_ANY, "",
            wxDefaultPosition, wxDefaultSize,
            wxTE_PROCESS_ENTER
        );
        auto* chooseFlightBtn = new wxButton(panel, wxID_ANY, "...");

        // seat_ только для отображения выбор через кнопку
        seat_ = new wxTextCtrl(
            panel, wxID_ANY, "",
            wxDefaultPosition, wxDefaultSize,
            wxTE_READONLY
        );
        auto* chooseSeatBtn = new wxButton(panel, wxID_ANY, "Choose seat");

        phone_ = new wxTextCtrl(panel, wxID_ANY);
        email_ = new wxTextCtrl(panel, wxID_ANY);

        baggage_ = new wxChoice(panel, wxID_ANY);
        baggage_->Append("0 (no)");
        baggage_->Append("1 (yes)");
        baggage_->SetSelection(0);

        food_ = new wxChoice(panel, wxID_ANY);
        food_->Append("0 (no)");
        food_->Append("1 (yes)");
        food_->SetSelection(0);

        auto* fg = new wxFlexGridSizer(7, 2, 8, 8);
        fg->AddGrowableCol(1, 1);

        auto L = [panel](const char* t) {
            return new wxStaticText(panel, wxID_ANY, t);
            };

        fg->Add(L("Full name:"), 0, wxALIGN_CENTER_VERTICAL);
        fg->Add(fullName_, 1, wxEXPAND);

        fg->Add(L("Document number:"), 0, wxALIGN_CENTER_VERTICAL);
        fg->Add(docNumber, 1, wxEXPAND);

        fg->Add(L("Flight number:"), 0, wxALIGN_CENTER_VERTICAL);
        auto* flightRow = new wxBoxSizer(wxHORIZONTAL);
        flightRow->Add(flight, 1, wxEXPAND | wxRIGHT, 4);
        flightRow->Add(chooseFlightBtn, 0, wxALIGN_CENTER_VERTICAL);
        fg->Add(flightRow, 1, wxEXPAND);

        fg->Add(L("Baggage (0/1):"), 0, wxALIGN_CENTER_VERTICAL);
        fg->Add(baggage_, 1, wxEXPAND);

        fg->Add(L("Food (0/1):"), 0, wxALIGN_CENTER_VERTICAL);
        fg->Add(food_, 1, wxEXPAND);

        fg->Add(L("Seat number:"), 0, wxALIGN_CENTER_VERTICAL);
        auto* seatRow = new wxBoxSizer(wxHORIZONTAL);
        seatRow->Add(seat_, 1, wxEXPAND | wxRIGHT, 4);
        seatRow->Add(chooseSeatBtn, 0, wxALIGN_CENTER_VERTICAL);
        fg->Add(seatRow, 1, wxEXPAND);

        fg->Add(L("Phone number:"), 0, wxALIGN_CENTER_VERTICAL);
        fg->Add(phone_, 1, wxEXPAND);

        fg->Add(L("Email:"), 0, wxALIGN_CENTER_VERTICAL);
        fg->Add(email_, 1, wxEXPAND);

        auto* registerBtn = new wxButton(panel, wxID_OK, "Register");
        auto* clearBtn = new wxButton(panel, wxID_CLEAR, "Clear");
        auto* closeBtn = new wxButton(panel, wxID_CLOSE, "Close");

        auto* btnRow = new wxBoxSizer(wxHORIZONTAL);
        btnRow->Add(registerBtn, 0, wxRIGHT, 6);
        btnRow->Add(clearBtn, 0, wxRIGHT, 6);
        btnRow->AddStretchSpacer(1);
        btnRow->Add(closeBtn, 0);

        auto* root = new wxBoxSizer(wxVERTICAL);
        root->AddSpacer(8);
        root->Add(fg, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
        root->AddSpacer(8);
        root->Add(btnRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

        panel->SetSizer(root);
        SetMinSize(wxSize(520, 360));
        CentreOnScreen();

        registerBtn->Bind(wxEVT_BUTTON, &AddPassengerFrame::OnRegister, this);
        clearBtn->Bind(wxEVT_BUTTON, &AddPassengerFrame::OnClear, this);
        closeBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); });
        chooseFlightBtn->Bind(wxEVT_BUTTON, &AddPassengerFrame::OnChooseFlight, this);
        flight->Bind(wxEVT_TEXT_ENTER, &AddPassengerFrame::OnChooseFlight, this);
        chooseSeatBtn->Bind(wxEVT_BUTTON, &AddPassengerFrame::OnChooseSeat, this);
    }

private:
    wxTextCtrl* fullName_{};
    wxTextCtrl* docNumber{};
    wxTextCtrl* flight{};
    wxTextCtrl* seat_{};
    wxTextCtrl* phone_{};
    wxTextCtrl* email_{};
    wxChoice* baggage_{};
    wxChoice* food_{};

    void OnClear(wxCommandEvent&) {
        fullName_->Clear();
        docNumber->Clear();
        flight->Clear();
        seat_->Clear();
        phone_->Clear();
        email_->Clear();
        baggage_->SetSelection(0);
        food_->SetSelection(0);
    }

    void OnRegister(wxCommandEvent&) {
        std::string err;

        bool ok = ErrorHandler::Try([&] {
            Passenger p;

            int nextNum = GetNextPassengerNumberFromDb();
            p.PASSENGERID = MakePassengerId(nextNum);

            wxString fullName = fullName_->GetValue();
            fullName = fullName.Strip(wxString::both);
            if (fullName.IsEmpty() || !ErrorHandler::IsLettersAndSpaces(fullName)) {
                throw AppException("Full name must contain only letters and spaces.");
            }
            fullName = ErrorHandler::NormalizeFullName(fullName);
            p.FULLNAME = std::string(fullName.mb_str());


            wxString doc = docNumber->GetValue();
            if (!ErrorHandler::NormalizeDocNumber(doc)) {
                throw AppException("DocNumber is required and must contain only letters and digits.");
            }
            p.DOCNUMBER = std::string(doc.mb_str());

            wxString flt = flight->GetValue().Strip(wxString::both);
            if (flt.IsEmpty())
                throw AppException("FlightNumber is required.");

            std::string fltStd = std::string(flt.mb_str());

            std::set<std::string> validFlights;
            {
                auto flights = Db::instance().QueryFlights();
                for (const auto& f : flights)
                    validFlights.insert(f.FLIGHTNUMBER);
            }
            if (!validFlights.count(fltStd)) {
                throw AppException(
                    "FlightNumber '" + fltStd + "' does not exist in flights table."
                );
            }
            p.FLIGHTNUMBER = fltStd;

            auto selB = baggage_->GetSelection();
            auto selF = food_->GetSelection();
            p.BAGGAGE = (selB == 1);
            p.FOOD = (selF == 1);

            wxString seat = seat_->GetValue();
            if (!ErrorHandler::IsValidSeat(seat)) {
                throw AppException("Seat must have format NNX, e.g. 12A.");
            }
            seat.MakeUpper();
            p.SEATNUMBER = std::string(seat.mb_str());

            wxString phone = phone_->GetValue().Strip(wxString::both);
            if (!ErrorHandler::IsValidPhone(phone)) {
                throw AppException(
                    "PhoneNumber must have format +7XXXXXXXXXX."
                );
            }
            p.PHONENUMBER = std::string(phone.mb_str());

            wxString email = email_->GetValue().Strip(wxString::both);
            if (!ErrorHandler::IsValidEmail(email)) {
                throw AppException(
                    "Email must have format like user@host.domain."
                );
            }
            p.EMAIL = std::string(email.mb_str());

            Db::instance().InsertOrUpdatePassenger(p);

            auto& s = Session::I();
            std::string empId = std::string(s.userId.mb_str());
            std::string empPos = std::string(s.position.mb_str());

            Logger::I().Write(
                "Employee " + empId +
                " (" + empPos + ") registered new passenger " +
                p.PASSENGERID + " on flight " + p.FLIGHTNUMBER
            );

            wxMessageBox("Passenger has been registered.",
                "Register passenger",
                wxOK | wxICON_INFORMATION, this);

            OnClear(*(new wxCommandEvent)); // чистка формы

            }, err);

        if (!ok) {
            wxMessageBox(wxString::FromUTF8(err.c_str()),
                "Error",
                wxOK | wxICON_ERROR, this);
        }
    }

    void OnChooseFlight(wxCommandEvent&)
    {
        wxString current = flight->GetValue().Strip(wxString::both);
        std::string initialQuery = std::string(current.mb_str());

        wxString chosen;
        if (!ChooseFlight(this, initialQuery, chosen))
            return;

        if (!chosen.IsEmpty()) {
            flight->SetValue(chosen);
        }
    }



    void OnChooseSeat(wxCommandEvent&)
    {
        // проверка выбран ли рейс
        wxString fltWx = flight->GetValue().Strip(wxString::both);
        if (fltWx.IsEmpty()) {
            wxMessageBox("Please enter/select FlightNumber first.","Choose seat", wxOK | wxICON_WARNING, this);
            return;
        }

        wxString chosen;
        if (ChooseSeatForFlight(this,
            std::string(fltWx.mb_str()),
            seat_->GetValue(),
            chosen))
        {
            seat_->SetValue(chosen);
        }
        // если вернулось фолс то либо отмена либо ошибка уже показана внутри
    }

};

void ShowAddPassenger(wxWindow* parent)
{
    (new AddPassengerFrame(parent))->Show();
}
