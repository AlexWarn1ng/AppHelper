#include <wx/wx.h>
#include <mysql/jdbc.h>
#include "AppHelper.h"
#include "Header.h"
#include <sstream>

std::string GetAllTablesData(sql::Connection* conn) {
    std::ostringstream out;

    std::unique_ptr<sql::Statement> stmt(conn->createStatement());
    std::unique_ptr<sql::ResultSet> tables(stmt->executeQuery("SHOW TABLES"));

    while (tables->next()) {
        std::string table = tables->getString(1);
        out << "=== TABLE: " << table << " ===\n";

        std::unique_ptr<sql::Statement> s2(conn->createStatement());
        std::unique_ptr<sql::ResultSet> rows(s2->executeQuery("SELECT * FROM `" + table + "`"));

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

class MyApp : public wxApp {
public:
    bool OnInit() override {
        try {
            sql::Driver* driver = get_driver_instance();
            std::unique_ptr<sql::Connection> conn(
                driver->connect("tcp://127.0.0.1:3306", "root", "$T@#MJN)IgfW23")
            );
            conn->setSchema("company");

            std::string data = GetAllTablesData(conn.get());
            if (data.empty()) data = "Нет данных или таблицы пусты.";

            MyFrame* frame = new MyFrame("Database Viewer", data);
            frame->Show();
            return true;
        }
        catch (const sql::SQLException& e) {
            wxMessageBox(wxString::Format("Ошибка БД: %s", e.what()), "Ошибка", wxOK | wxICON_ERROR);
            return false;
        }
    }
};

wxIMPLEMENT_APP(MyApp);
