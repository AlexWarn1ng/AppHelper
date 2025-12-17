#include "LoginDialog.h"
#include <memory>
#include <wx/dcbuffer.h>  
#include <wx/graphics.h>
#include "Header.h"

class GradientHeader : public wxPanel {
public:
    GradientHeader(wxWindow* parent,
        const wxString& title,
        const wxString& subtitle,
        const wxColour& c1 = wxColour(30, 64, 175),   
        const wxColour& c2 = wxColour(65, 105, 225)) : wxPanel(parent, wxID_ANY), m_title(title), m_subtitle(subtitle), m_c1(c1), m_c2(c2)
    {
        SetMinSize(wxSize(-1, 96));
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &GradientHeader::OnPaint, this);
    }

private:
    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        wxSize sz = GetClientSize();
        wxRect r(0, 0, sz.x, sz.y);

        dc.GradientFillLinear(r, m_c1, m_c2, wxSOUTH);

        dc.SetTextForeground(*wxWHITE);

        wxFont titleFont = wxFontInfo(18).Bold();
        wxFont subFont = wxFontInfo(10);
        dc.SetFont(titleFont);
        dc.DrawText(m_title, 16, 18);

        dc.SetFont(subFont);
        dc.DrawText(m_subtitle, 16, 48);
    }

    wxString m_title;
    wxString m_subtitle;
    wxColour m_c1, m_c2;
};

LoginDialog::LoginDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, "Login to the system",
        wxDefaultPosition, wxSize(520, 360),     
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* header = new GradientHeader(this, "Welcome", "Please sign in to continue");

    auto* formWrap = new wxPanel(this);
    formWrap->SetBackgroundColour(wxColour(247, 247, 249)); 
    auto* formOuter = new wxBoxSizer(wxVERTICAL);
    auto* form = new wxFlexGridSizer(2, 12, 12);
    form->AddGrowableCol(1, 1);

    m_login = new wxTextCtrl(formWrap, wxID_ANY, "", wxDefaultPosition, wxSize(-1, 28)); m_login->SetMaxLength(4);// no more than 4 cifri
    m_password = new wxTextCtrl(formWrap, wxID_ANY, "", wxDefaultPosition, wxSize(-1, 28), wxTE_PASSWORD);

    m_login->Bind(wxEVT_CHAR, [this](wxKeyEvent& event) {
        int key = event.GetKeyCode();
        if ((key >= '0' && key <= '9') || key == WXK_BACK || key == WXK_DELETE ||
            key == WXK_TAB || key == WXK_RETURN || key == WXK_NUMPAD0 || key == WXK_NUMPAD9 ||
            key == WXK_LEFT || key == WXK_RIGHT) {
            event.Skip(); 
        }
        else {
            wxBell();     
        }
        });


    form->Add(new wxStaticText(formWrap, wxID_ANY, "Login:"), 0, wxALIGN_CENTER_VERTICAL);
    form->Add(m_login, 1, wxEXPAND);
    form->Add(new wxStaticText(formWrap, wxID_ANY, "Password:"), 0, wxALIGN_CENTER_VERTICAL);
    form->Add(m_password, 1, wxEXPAND);

    formOuter->AddSpacer(12);
    formOuter->Add(form, 0, wxLEFT | wxRIGHT | wxEXPAND, 16);
    formOuter->AddSpacer(12);
    formWrap->SetSizer(formOuter);

    m_error = new wxStaticText(this, wxID_ANY, "");
    m_error->SetForegroundColour(wxColour(200, 0, 0));
    m_error->SetMinSize(wxSize(-1, 22)); 

    auto* btns = CreateSeparatedButtonSizer(wxOK | wxCANCEL);

    root->Add(header, 0, wxEXPAND);
    root->Add(formWrap, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 0);
    root->Add(m_error, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 16);
    root->Add(btns, 0, wxALL | wxEXPAND, 12);

    SetSizerAndFit(root);
    SetMinSize(wxSize(520, 360));
    CentreOnScreen();

    Bind(wxEVT_BUTTON, &LoginDialog::OnOk, this, wxID_OK);
}

void LoginDialog::OnOk(wxCommandEvent& e) {
    m_error->SetLabel("");
    m_ok = false;

    const wxString login = m_login->GetValue();
    const wxString pass = m_password->GetValue();

    if (login.IsEmpty() || pass.IsEmpty()) {
        m_error->SetLabel("Enter login and password.");
        return;
    }

    try {
        auto emp = ErrorHandler::Auth([&] {
            return Employee::Authenticate(login, pass);
            }, m_error); // ErrorHandler sisteminje iskl

        if (emp) {
            m_userId = emp->id();
            m_status = emp->position();
            m_ok = true;
            e.Skip(); 
            return;
        }

        m_error->SetLabel("Invalid ID or password.");
    }
    catch (const AuthException& ex) {
        m_error->SetLabel(wxString::FromUTF8(ex.what()));
        return;
    }
    catch (const DatabaseException&) {
        return;
    }
    catch (const AppException&) {
        // tekst uzhe est v error handler
        return;
    }
}
