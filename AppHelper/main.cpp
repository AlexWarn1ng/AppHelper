#include <wx/wx.h>
#include <wx/sizer.h>
#include <wx/dcbuffer.h>
#include <memory>
#include "Header.h"
#include "LoginDialog.h"
#include "WeatherForecast.h"
#include "PassengerImportExport.h"
#include "EditFindPassenger.h"

class GradientHeader : public wxPanel {
public:
    GradientHeader(wxWindow* p, const wxString& title, const wxString& subtitle,
        const wxColour& c1 = wxColour(30, 64, 175),
        const wxColour& c2 = wxColour(65, 105, 225))
        : wxPanel(p, wxID_ANY), t(title), s(subtitle), C1(c1), C2(c2) {
        SetMinSize(wxSize(-1, 96));
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
            wxAutoBufferedPaintDC dc(this);
            wxSize sz = GetClientSize();
            dc.GradientFillLinear(wxRect(0, 0, sz.x, sz.y), C1, C2, wxSOUTH);
            dc.SetTextForeground(*wxWHITE);
            dc.SetFont(wxFontInfo(18).Bold());
            dc.DrawText(t, 16, 18);
            dc.SetFont(wxFontInfo(10));
            dc.DrawText(s, 16, 48);
            });
    }
private:
    wxString t, s;
    wxColour C1, C2;
};

class MainFrame : public wxFrame {
public:
    MainFrame() :
        wxFrame(nullptr, wxID_ANY, "Main Menu", wxDefaultPosition, wxSize(720, 420))
    {
        auto& sess = Session::I();

        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* header = new GradientHeader(
            this,
            wxString::Format("User: %s | Status: %s", sess.userId, sess.position),
            "Select an action to continue"
        );

        auto* body = new wxPanel(this);
        body->SetBackgroundColour(wxColour(247, 247, 249));
        auto* grid = new wxGridSizer(1, 3, 16, 16);

        switch (sess.role) {
        case Role::Pilot:
        case Role::CoPilot: {
            auto* b1 = new wxButton(body, wxID_ANY, "View weather forecast", wxDefaultPosition, wxSize(220, 48));
            auto* b2 = new wxButton(body, wxID_ANY, "Passenger manifest", wxDefaultPosition, wxSize(220, 48));
            auto* b3 = new wxButton(body, wxID_ANY, "Report incident", wxDefaultPosition, wxSize(220, 48));
            grid->Add(b1, 0, wxALIGN_CENTER);
            grid->Add(b2, 0, wxALIGN_CENTER);
            grid->Add(b3, 0, wxALIGN_CENTER);
            b1->Bind(wxEVT_BUTTON, &MainFrame::OnPilotFlightPlan, this);
            b2->Bind(wxEVT_BUTTON, &MainFrame::OnPilotManifest, this);
            b3->Bind(wxEVT_BUTTON, &MainFrame::OnPilotIncident, this);
            break;
        }

        case Role::Receptionist: {
            auto* b1 = new wxButton(body, wxID_ANY, "Import/export from .xslx", wxDefaultPosition, wxSize(220, 48));
            auto* b2 = new wxButton(body, wxID_ANY, "Print Ticket", wxDefaultPosition, wxSize(220, 48));
            auto* b3 = new wxButton(body, wxID_ANY, "Edit/find passenger", wxDefaultPosition, wxSize(220, 48));
            grid->Add(b1, 0, wxALIGN_CENTER);
            grid->Add(b2, 0, wxALIGN_CENTER);
            grid->Add(b3, 0, wxALIGN_CENTER);
            b1->Bind(wxEVT_BUTTON, &MainFrame::OnImportFromXSLX, this);
            b2->Bind(wxEVT_BUTTON, &MainFrame::OnPrintTicket, this);
            b3->Bind(wxEVT_BUTTON, &MainFrame::OnEditFindpassenger, this);

            break;
        }

        case Role::Trainee: {
            break;
        }

        case Role::Unknown:
        default: {
            auto* b1 = new wxButton(body, wxID_ANY, "Find passenger", wxDefaultPosition, wxSize(220, 48));
            grid->Add(b1, 0, wxALIGN_CENTER);
            b1->Bind(wxEVT_BUTTON, &MainFrame::OnFindPassenger, this);
            break;
        }
        }


        auto* inner = new wxBoxSizer(wxVERTICAL);
        inner->AddSpacer(24);
        inner->Add(grid, 1, wxALIGN_CENTER_HORIZONTAL);
        inner->AddSpacer(24);
        body->SetSizer(inner);

        root->Add(header, 0, wxEXPAND);
        root->Add(body, 1, wxEXPAND);
        SetSizer(root);
        SetMinSize(wxSize(640, 360));
        CentreOnScreen();
    }

private:
    void OnFindPassenger(wxCommandEvent&) { wxMessageBox("You clicked 'Find passenger'", "Info", wxOK | wxICON_INFORMATION, this); }
    void OnPrintTicket(wxCommandEvent&) { wxMessageBox("You clicked 'Print passenger ticket'", "Info", wxOK | wxICON_INFORMATION, this); }
    void OnPilotFlightPlan(wxCommandEvent&) { ShowWeatherFrame(this); }
    void OnImportFromXSLX(wxCommandEvent&) { ShowPassengerImportExport(this); }
    void OnEditFindpassenger(wxCommandEvent&) { ShowEditFindPassenger(this); }
    void OnPilotManifest(wxCommandEvent&) { wxMessageBox("You clicked 'Passenger manifest'", "Info", wxOK | wxICON_INFORMATION, this); }
    void OnPilotIncident(wxCommandEvent&) { wxMessageBox("You clicked 'Report incident'", "Info", wxOK | wxICON_INFORMATION, this); }
};

class MyApp : public wxApp {
public:
    bool OnInit() override {
        try {
            Db::init("tcp://127.0.0.1:3306", "root", "$T@#MJN)IgfW23", "company");

            LoginDialog login(nullptr);
            if (login.ShowModal() != wxID_OK || !login.IsAuthenticated())
                return false;
            Session::I().userId = login.UserId();
            Session::I().position = login.UserStatus();
            Session::I().role = ParseRole(login.UserStatus());

            auto* frame = new MainFrame();
            frame->Show();
            return true;
        }
        catch (const sql::SQLException& e) {
            wxMessageBox(wxString::Format("Database error: %s", e.what()), "Error", wxOK | wxICON_ERROR);
            return false;
        }
    }

};

wxIMPLEMENT_APP(MyApp);
