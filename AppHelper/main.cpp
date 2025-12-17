#include <wx/wx.h>
#include <wx/sizer.h>
#include <wx/dcbuffer.h>
#include <memory>
#include "Header.h"
#include "LoginDialog.h"
#include "PassengerImportExport.h"
#include "FlightImportExport.h"
#include "EditFindPassenger.h"
#include "RegisterPassenger.h"  
#include "PDFreport.h" 
#include "AdminEditor.h"

class GradientHeader : public wxPanel {
public:
    GradientHeader(wxWindow* p, const wxString& title, const wxString& subtitle, const wxColour& c1 = wxColour(30, 64, 175), const wxColour& c2 = wxColour(65, 105, 225)) : wxPanel(p, wxID_ANY), t(title), s(subtitle), C1(c1), C2(c2) {
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

        case Role::Receptionist: {
            auto* b1 = new wxButton(body, wxID_ANY, "Import/export from .xslx passengers", wxDefaultPosition, wxSize(220, 48));
            auto* b2 = new wxButton(body, wxID_ANY, "Add passenger", wxDefaultPosition, wxSize(220, 48));
            auto* b3 = new wxButton(body, wxID_ANY, "Edit/find passenger", wxDefaultPosition, wxSize(220, 48));
            auto* b4 = new wxButton(body, wxID_ANY, "Import/export from .xslx flights", wxDefaultPosition, wxSize(220, 48));
            auto* b5 = new wxButton(body, wxID_ANY, "Make a PDF report of passengers", wxDefaultPosition, wxSize(220, 48));
            grid->Add(b1, 0, wxALIGN_CENTER);
            grid->Add(b2, 0, wxALIGN_CENTER);
            grid->Add(b3, 0, wxALIGN_CENTER);
            grid->Add(b4, 0, wxALIGN_CENTER);
            grid->Add(b5, 0, wxALIGN_CENTER);
            b1->Bind(wxEVT_BUTTON, &MainFrame::OnImportFromXSLX, this);
            b2->Bind(wxEVT_BUTTON, &MainFrame::OnAddPassenger, this);
            b3->Bind(wxEVT_BUTTON, &MainFrame::OnEditFindpassenger, this);
            b4->Bind(wxEVT_BUTTON, &MainFrame::OnImportFromXSLXFLIGHTS, this);
            b5->Bind(wxEVT_BUTTON, &MainFrame::OnPDFreport, this);

            break;
        }

        case Role::Unknown:
        default: {
            break;
        }
        case Role::Admin: {
            auto* b1 = new wxButton(body, wxID_ANY, "Edit airports/employee/aircrafts/flights", wxDefaultPosition, wxSize(220, 48));
            grid->Add(b1, 0, wxALIGN_CENTER);
            b1->Bind(wxEVT_BUTTON, &MainFrame::OnChange, this);
            wxMessageBox("You are logged in as admin. Be sure to log out after finishing your work.", "WARNING", wxOK | wxICON_INFORMATION, this);
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
    void OnAddPassenger(wxCommandEvent&) { ShowAddPassenger(this); }
    void OnImportFromXSLX(wxCommandEvent&) { ShowPassengerImportExport(this); }
    void OnImportFromXSLXFLIGHTS(wxCommandEvent&) { ShowFlightImportExport(this); }
    void OnEditFindpassenger(wxCommandEvent&) { ShowEditFindPassenger(this); }
    void OnPDFreport(wxCommandEvent&) { ShowPDFreport(this); }
    void OnChange(wxCommandEvent&) { ShowAdminRefEditor(this); }
};

class MyApp : public wxApp {
public:
    bool OnInit() override {
        try {
            Db::init("tcp://127.0.0.1:3306", "root", "$T@#MJN)IgfW23", "company");

            LoginDialog login(nullptr);
            if (login.ShowModal() != wxID_OK || !login.IsAuthenticated())
                return false;
            auto& sess = Session::I();
            sess.userId = login.UserId();
            sess.role = ParseRole(login.UserStatus());
            if (sess.role == Role::Receptionist) {     //ТУТ ИСПРАВЛЯЛ ПОКАЗ РОЛИ ЕСЛИ ЧТО МОЖЕТ БАГАТЬ
                sess.position = "Receptionist";
            }
            else if (sess.role == Role::Admin) {
                sess.position = "Admin";
            }
            else {
                sess.position = "Unknown";
            }

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
