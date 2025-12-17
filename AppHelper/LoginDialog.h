#pragma once
#include <wx/wx.h>
#include "Header.h"

class LoginDialog : public wxDialog {
public:
    explicit LoginDialog(wxWindow* parent);

    bool IsAuthenticated() const { return m_ok; }
    wxString UserId() const { return m_userId; }
    wxString UserStatus() const { return m_status; }

private: 
    void OnOk(wxCommandEvent& e);

    wxTextCtrl* m_login{ nullptr };
    wxTextCtrl* m_password{ nullptr };
    wxStaticText* m_error{ nullptr };

    bool m_ok{ false };
    wxString m_userId;
    wxString m_status;
};
