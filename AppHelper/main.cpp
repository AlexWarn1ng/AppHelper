#include <wx/wx.h>
#include "Header.h"
#include <sstream>

std::string GetAllTablesData() {
    std::ostringstream out;
    auto stmt = Db::instance().createStatement();
    std::unique_ptr<sql::ResultSet> tables(stmt->executeQuery("SHOW TABLES"));

    while (tables->next()) {
        std::string table = tables->getString(1);
        out << "=== TABLE: " << table << " ===\n";

        auto s2 = Db::instance().createStatement();
        std::unique_ptr<sql::ResultSet> rows(
            s2->executeQuery("SELECT * FROM `" + table + "`"));

        sql::ResultSetMetaData* meta = rows->getMetaData();
        int cols = meta->getColumnCount();

        for (int i = 1; i <= cols; ++i) {
            out << meta->getColumnName(i);
            if (i < cols) out << " | ";
        }
        out << "\n";

        while (rows->next()) {
            for (int i = 1; i <= cols; ++i) {
                out << rows->getString(i);
                if (i < cols) out << " | ";
            }
            out << "\n";
        }
        out << "\n";
    }
    return out.str();
}

class MyFrame : public wxFrame {
public:
    MyFrame(const wxString& title, const wxString& content)
        : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(800, 600)) {
        auto* textCtrl = new wxTextCtrl(this, wxID_ANY, content,
            wxDefaultPosition, wxDefaultSize,
            wxTE_MULTILINE | wxTE_READONLY | wxHSCROLL);
        textCtrl->SetFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE));
    }
};
//
class MyApp : public wxApp {
public:
    bool OnInit() override {
        try {
            Db::init("tcp://127.0.0.1:3306", "root", "$T@#MJN)IgfW23", "company");
            std::string data = GetAllTablesData();
            if (data.empty()) data = "No data or tables are empty.";
            (new MyFrame("Database Viewer", data))->Show();
            return true;
        }
        catch (const sql::SQLException& e) {
            wxMessageBox(wxString::Format("error DB %s", e.what()), "error", wxOK | wxICON_ERROR);
            return false;
        }
    }
};

wxIMPLEMENT_APP(MyApp);
