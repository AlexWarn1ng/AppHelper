#include "Header.h"
#include "AdminEditor.h"

#include <wx/wx.h>
#include <wx/grid.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <set>
#include <chrono>
#include <sstream>
#include <iomanip>
#include "FlightImportExport.h"   

// простая проверка строки 
static bool IsNonEmpty(const wxString& s) {
    wxString v = s;
    v.Trim(true).Trim(false);
    return !v.IsEmpty();
}

enum class RefKind {
    Employees,
    Aircrafts,
    Airports,
    Flights
};

static const char* RefKindToString(RefKind k) {
    switch (k) {
    case RefKind::Employees: return "Employees";
    case RefKind::Aircrafts: return "Aircrafts";
    case RefKind::Airports:  return "Airports";
    case RefKind::Flights:   return "Flights";
    }
    return "";
}

class AdminRefEditFrame : public wxFrame {
public:
    explicit AdminRefEditFrame(wxWindow* parent = nullptr)
        : wxFrame(parent, wxID_ANY, "Admin reference editor",
            wxDefaultPosition, wxSize(900, 500))
    {
        auto* panel = new wxPanel(this);

        choice_ = new wxChoice(panel, wxID_ANY);
        choice_->Append("Employees");  // 0
        choice_->Append("Aircrafts");  // 1
        choice_->Append("Airports");   // 2
        choice_->Append("Flights");    // 3
        choice_->SetSelection(0);

        auto* reloadBtn = new wxButton(panel, wxID_ANY, "Reload");
        auto* addBtn = new wxButton(panel, wxID_ANY, "Add row");
        auto* delBtn = new wxButton(panel, wxID_ANY, "Delete selected");
        auto* saveBtn = new wxButton(panel, wxID_ANY, "Save changes");

        errorLabel_ = new wxStaticText(panel, wxID_ANY, "");
        errorLabel_->SetForegroundColour(*wxRED);

        grid_ = new wxGrid(panel, wxID_ANY);
        grid_->CreateGrid(0, 0);
        grid_->EnableEditing(true);
        grid_->EnableDragColSize(true);
        grid_->EnableDragRowSize(true);
        grid_->SetRowLabelSize(40);

        auto* topRow = new wxBoxSizer(wxHORIZONTAL);
        topRow->Add(new wxStaticText(panel, wxID_ANY, "Reference:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        topRow->Add(choice_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
        topRow->Add(reloadBtn, 0, wxRIGHT, 4);
        topRow->Add(addBtn, 0, wxRIGHT, 4);
        topRow->Add(delBtn, 0, wxRIGHT, 4);
        topRow->Add(saveBtn, 0, wxRIGHT, 4);
        topRow->AddStretchSpacer(1);
        topRow->Add(errorLabel_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

        auto* root = new wxBoxSizer(wxVERTICAL);
        root->Add(topRow, 0, wxEXPAND | wxALL, 6);
        root->Add(grid_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

        panel->SetSizer(root);

        choice_->Bind(wxEVT_CHOICE, &AdminRefEditFrame::OnKindChanged, this);
        reloadBtn->Bind(wxEVT_BUTTON, &AdminRefEditFrame::OnReload, this);
        addBtn->Bind(wxEVT_BUTTON, &AdminRefEditFrame::OnAddRow, this);
        delBtn->Bind(wxEVT_BUTTON, &AdminRefEditFrame::OnDeleteRows, this);
        saveBtn->Bind(wxEVT_BUTTON, &AdminRefEditFrame::OnSave, this);

        // изначальная загрузка
        currentKind_ = RefKind::Employees;
        DoReload();
    }

private:
    wxChoice* choice_{};
    wxGrid* grid_{};
    wxStaticText* errorLabel_{};

    RefKind currentKind_{ RefKind::Employees };

    std::vector<int>           employeesBeforeIds_;
    std::vector<std::string>   aircraftBeforeRegs_;
    std::vector<std::string>   airportsBeforeIata_;
    std::vector<std::string>   flightsBeforeNumbers_;


    void ClearError() { errorLabel_->SetLabel(""); }

    void OnKindChanged(wxCommandEvent&) {
        ClearError();
        currentKind_ = static_cast<RefKind>(choice_->GetSelection());
        DoReload();
    }

    void OnReload(wxCommandEvent&) {
        ClearError();
        DoReload();
    }

    void OnAddRow(wxCommandEvent&) {
        ClearError();
        int rows = grid_->GetNumberRows();
        grid_->AppendRows(1);
        grid_->SetGridCursor(rows, 0);
    }

    void OnDeleteRows(wxCommandEvent&) {
        ClearError();
        auto sel = grid_->GetSelectedRows();
        if (sel.IsEmpty()) {
            wxMessageBox("No rows selected.", "Delete",
                wxOK | wxICON_INFORMATION, this);
            return;
        }

        int rc = wxMessageBox(
            wxString::Format("Delete %u selected row(s)?", sel.GetCount()),
            "Confirm delete",
            wxYES_NO | wxICON_QUESTION,
            this);
        if (rc != wxYES)
            return;

        for (int i = (int)sel.GetCount() - 1; i >= 0; --i) {
            grid_->DeleteRows(sel[i]);
        }
    }


    void OnSave(wxCommandEvent&) {
        ClearError();
        std::string err;
        bool ok = ErrorHandler::Try([&] {
            switch (currentKind_) {
            case RefKind::Employees: SaveEmployees(); break;
            case RefKind::Aircrafts: SaveAircrafts(); break;
            case RefKind::Airports:  SaveAirports();  break;
            case RefKind::Flights:   SaveFlights();   break;
            }
            }, err, errorLabel_);

        if (ok) {
            wxMessageBox("Changes saved.", "Save", wxOK | wxICON_INFORMATION, this);
            DoReload();
        }
        else {
            wxMessageBox(wxString::Format("Error: %s", err), "Save error",
                wxOK | wxICON_ERROR, this);
        }
    }

    void DoReload() {
        switch (currentKind_) {
        case RefKind::Employees: LoadEmployees(); break;
        case RefKind::Aircrafts: LoadAircrafts(); break;
        case RefKind::Airports:  LoadAirports();  break;
        case RefKind::Flights:   LoadFlights();   break;
        }
    }


    void SetupEmployeesGrid() {
        grid_->ClearGrid();
        if (grid_->GetNumberRows() > 0)
            grid_->DeleteRows(0, grid_->GetNumberRows());
        if (grid_->GetNumberCols() > 0)
            grid_->DeleteCols(0, grid_->GetNumberCols());

        grid_->AppendCols(6);
        grid_->SetColLabelValue(0, "EmployeeID");
        grid_->SetColLabelValue(1, "Password");
        grid_->SetColLabelValue(2, "Position");
        grid_->SetColLabelValue(3, "PhoneNumber");
        grid_->SetColLabelValue(4, "Email");
        grid_->SetColLabelValue(5, "FullName");

        auto* attrId = new wxGridCellAttr();
        attrId->SetReadOnly(true);
        attrId->SetBackgroundColour(*wxLIGHT_GREY);
        grid_->SetColAttr(0, attrId);

        grid_->AutoSizeColumns();
    }



    void LoadEmployees() {
        SetupEmployeesGrid();
        employeesBeforeIds_.clear();

        auto st = Db::instance().createStatement();
        std::unique_ptr<sql::ResultSet> rs(
            st->executeQuery(
                "SELECT EmployeeID, Password, Position, PhoneNumber, Email, FullName "
                "FROM employees ORDER BY EmployeeID"));

        int row = 0;
        while (rs->next()) {
            grid_->AppendRows(1);
            int id = rs->getInt("EmployeeID");
            employeesBeforeIds_.push_back(id);

            grid_->SetCellValue(row, 0, wxString::Format("%d", id));
            grid_->SetCellValue(row, 1, wxString::FromUTF8(rs->getString("Password")));
            grid_->SetCellValue(row, 2, wxString::FromUTF8(rs->getString("Position")));
            grid_->SetCellValue(row, 3, wxString::FromUTF8(rs->getString("PhoneNumber")));
            grid_->SetCellValue(row, 4, wxString::FromUTF8(rs->getString("Email")));
            grid_->SetCellValue(row, 5, wxString::FromUTF8(rs->getString("FullName")));
            ++row;
        }
        grid_->AutoSizeColumns();
    }
    void SaveEmployees() {
        int rows = grid_->GetNumberRows();
        std::set<int> currentIds;      // для определения удалённых

        for (int r = 0; r < rows; ++r) {
            int rowNo = r + 1;

            wxString idStr = grid_->GetCellValue(r, 0).Strip(wxString::both);
            wxString pass = grid_->GetCellValue(r, 1).Strip(wxString::both);
            wxString pos = grid_->GetCellValue(r, 2).Strip(wxString::both);
            wxString phone = grid_->GetCellValue(r, 3).Strip(wxString::both);
            wxString email = grid_->GetCellValue(r, 4).Strip(wxString::both);
            wxString fname = grid_->GetCellValue(r, 5).Strip(wxString::both);

            if (!IsNonEmpty(pass) || !IsNonEmpty(pos) || !IsNonEmpty(fname)) {
                throw AppException(
                    "Employee row " + std::to_string(rowNo) +
                    ": Password/Position/FullName must not be empty."
                );
            }

            if (!ErrorHandler::IsLettersAndSpaces(pos)) {
                throw AppException(
                    "Employee row " + std::to_string(rowNo) +
                    ": Position must contain only letters and spaces."
                );
            }
            pos = ErrorHandler::NormalizeFullName(pos);

            if (!ErrorHandler::IsLettersAndSpaces(fname)) {
                throw AppException(
                    "Employee row " + std::to_string(rowNo) +
                    ": FullName must contain only letters and spaces."
                );
            }
            fname = ErrorHandler::NormalizeFullName(fname);

            if (!phone.IsEmpty() && !ErrorHandler::IsValidPhone(phone)) {
                throw AppException(
                    "Employee row " + std::to_string(rowNo) +
                    ": invalid phone number."
                );
            }

            if (!email.IsEmpty() && !ErrorHandler::IsValidEmail(email)) {
                throw AppException(
                    "Employee row " + std::to_string(rowNo) +
                    ": invalid email."
                );
            }

            bool hasId = !idStr.IsEmpty();
            if (hasId) {
                int id = wxAtoi(idStr);
                currentIds.insert(id);

                auto ps = Db::instance().prepare(
                    "UPDATE employees SET Password=?, Position=?, PhoneNumber=?, Email=?, FullName=? "
                    "WHERE EmployeeID=?");
                ps->setString(1, std::string(pass.mb_str()));
                ps->setString(2, std::string(pos.mb_str()));
                ps->setString(3, std::string(phone.mb_str()));
                ps->setString(4, std::string(email.mb_str()));
                ps->setString(5, std::string(fname.mb_str()));
                ps->setInt(6, id);
                ps->execute();
            }
            else {
                // новый сотрудник id автоинкремент
                auto ps = Db::instance().prepare(
                    "INSERT INTO employees (Password, Position, PhoneNumber, Email, FullName) "
                    "VALUES (?, ?, ?, ?, ?)");
                ps->setString(1, std::string(pass.mb_str()));
                ps->setString(2, std::string(pos.mb_str()));
                ps->setString(3, std::string(phone.mb_str()));
                ps->setString(4, std::string(email.mb_str()));
                ps->setString(5, std::string(fname.mb_str()));
                ps->execute();
            }
        }

        // DELETE те айди которых теперь нет
        for (int idOld : employeesBeforeIds_) {
            if (currentIds.find(idOld) == currentIds.end()) {
                auto ps = Db::instance().prepare(
                    "DELETE FROM employees WHERE EmployeeID=?");
                ps->setInt(1, idOld);
                ps->execute();
            }
        }
    }



    void SetupAircraftsGrid() {
        grid_->ClearGrid();
        if (grid_->GetNumberRows() > 0)
            grid_->DeleteRows(0, grid_->GetNumberRows());
        if (grid_->GetNumberCols() > 0)
            grid_->DeleteCols(0, grid_->GetNumberCols());
        grid_->AppendCols(7);
        grid_->SetColLabelValue(0, "RegistrationNumber");
        grid_->SetColLabelValue(1, "Model");
        grid_->SetColLabelValue(2, "TotalSeats");
        grid_->SetColLabelValue(3, "EconomySeats");
        grid_->SetColLabelValue(4, "BusinessSeats");
        grid_->SetColLabelValue(5, "FirstClassSeats");
        grid_->SetColLabelValue(6, "Status");
        grid_->AutoSizeColumns();
    }

    void LoadAircrafts() {
        SetupAircraftsGrid();
        aircraftBeforeRegs_.clear();

        auto st = Db::instance().createStatement();
        std::unique_ptr<sql::ResultSet> rs(
            st->executeQuery(
                "SELECT RegistrationNumber, Model, TotalSeats, EconomySeats, "
                "BusinessSeats, FirstClassSeats, Status "
                "FROM Aircrafts ORDER BY RegistrationNumber"));

        int row = 0;
        while (rs->next()) {
            grid_->AppendRows(1);
            std::string reg = rs->getString("RegistrationNumber");
            aircraftBeforeRegs_.push_back(reg);

            grid_->SetCellValue(row, 0, wxString::FromUTF8(reg));
            grid_->SetCellValue(row, 1, wxString::FromUTF8(rs->getString("Model")));
            grid_->SetCellValue(row, 2, wxString::Format("%d", rs->getInt("TotalSeats")));
            grid_->SetCellValue(row, 3, wxString::Format("%d", rs->getInt("EconomySeats")));
            grid_->SetCellValue(row, 4, wxString::Format("%d", rs->getInt("BusinessSeats")));
            grid_->SetCellValue(row, 5, wxString::Format("%d", rs->getInt("FirstClassSeats")));
            grid_->SetCellValue(row, 6, wxString::FromUTF8(rs->getString("Status")));
            ++row;
        }
        grid_->AutoSizeColumns();
    }

    void SaveAircrafts() {
        int rows = grid_->GetNumberRows();
        std::set<std::string> currentRegs;

        struct RowData {
            std::string reg;
            std::string model;
            std::string status;
            int total{};
            int eco{};
            int bus{};
            int first{};
            bool existedBefore{};
        };

        std::vector<RowData> parsed;
        parsed.reserve(rows);

        for (int r = 0; r < rows; ++r) {
            int rowNo = r + 1;

            wxString reg = grid_->GetCellValue(r, 0).Strip(wxString::both);
            wxString model = grid_->GetCellValue(r, 1).Strip(wxString::both);
            wxString tStr = grid_->GetCellValue(r, 2).Strip(wxString::both);
            wxString eStr = grid_->GetCellValue(r, 3).Strip(wxString::both);
            wxString bStr = grid_->GetCellValue(r, 4).Strip(wxString::both);
            wxString fStr = grid_->GetCellValue(r, 5).Strip(wxString::both);
            wxString status = grid_->GetCellValue(r, 6).Strip(wxString::both);

         
            if (reg.IsEmpty()) {
                throw AppException("Aircraft row " + std::to_string(rowNo) +
                    ": RegistrationNumber is required.");
            }

            wxString regForCheck = reg;
            regForCheck.MakeUpper();
            regForCheck.Replace("-", "");
            if (!Flight::IsValidFlightNumberFormat(std::string(regForCheck.mb_str()))) {
                throw AppException("Aircraft row " + std::to_string(rowNo) +
                    ": invalid RegistrationNumber format.");
            }

            reg.MakeUpper();
            std::string regStd = std::string(reg.mb_str());
            currentRegs.insert(regStd);

            if (model.IsEmpty()) {
                throw AppException("Aircraft row " + std::to_string(rowNo) +
                    ": Model is required.");
            }
            for (wxUniChar c : model) {
                if (!(wxIsalpha(c) || wxIsdigit(c) || wxIsspace(c))) {
                    throw AppException("Aircraft row " + std::to_string(rowNo) +
                        ": Model may contain only letters, digits and spaces.");
                }
            }

            if (!ErrorHandler::IsLettersAndSpaces(status)) {
                throw AppException("Aircraft row " + std::to_string(rowNo) +
                    ": Status must contain only letters and spaces.");
            }
            status = ErrorHandler::NormalizeFullName(status);

            auto parseInt = [&](const wxString& s, const char* fieldName, bool allowZero)->int {
                long v = 0;
                if (!s.ToLong(&v) || (!allowZero && v <= 0) || (allowZero && v < 0)) {
                    std::string msg = "Aircraft row " + std::to_string(rowNo) + ": ";
                    msg += fieldName;
                    msg += allowZero ? " must be >= 0 and contain only digits."
                        : " must be > 0 and contain only digits.";
                    throw AppException(msg);
                }
                return static_cast<int>(v);
                };

            int total = parseInt(tStr, "TotalSeats", false);
            int eco = parseInt(eStr, "EconomySeats", true);
            int bus = parseInt(bStr, "BusinessSeats", true);
            int first = parseInt(fStr, "FirstClassSeats", true);

            if (eco + bus + first > total) {
                throw AppException("Aircraft row " + std::to_string(rowNo) +
                    ": sum of class seats > TotalSeats.");
            }

            bool existedBefore =
                std::find(aircraftBeforeRegs_.begin(), aircraftBeforeRegs_.end(), regStd)
                != aircraftBeforeRegs_.end();

            RowData d;
            d.reg = regStd;
            d.model = std::string(model.mb_str());
            d.status = std::string(status.mb_str());
            d.total = total;
            d.eco = eco;
            d.bus = bus;
            d.first = first;
            d.existedBefore = existedBefore;
            parsed.push_back(std::move(d));
        }

        for (const auto& d : parsed) {
            if (d.existedBefore) {
                auto ps = Db::instance().prepare(
                    "UPDATE Aircrafts SET Model=?, TotalSeats=?, EconomySeats=?, "
                    "BusinessSeats=?, FirstClassSeats=?, Status=? "
                    "WHERE RegistrationNumber=?");
                ps->setString(1, d.model);
                ps->setInt(2, d.total);
                ps->setInt(3, d.eco);
                ps->setInt(4, d.bus);
                ps->setInt(5, d.first);
                ps->setString(6, d.status);
                ps->setString(7, d.reg);
                ps->execute();
            }
            else {
                auto ps = Db::instance().prepare(
                    "INSERT INTO Aircrafts "
                    "(RegistrationNumber, Model, TotalSeats, EconomySeats, "
                    "BusinessSeats, FirstClassSeats, Status) "
                    "VALUES (?,?,?,?,?,?,?)");
                ps->setString(1, d.reg);
                ps->setString(2, d.model);
                ps->setInt(3, d.total);
                ps->setInt(4, d.eco);
                ps->setInt(5, d.bus);
                ps->setInt(6, d.first);
                ps->setString(7, d.status);
                ps->execute();
            }
        }

        for (const auto& regOld : aircraftBeforeRegs_) {
            if (currentRegs.find(regOld) == currentRegs.end()) {
                auto ps = Db::instance().prepare(
                    "DELETE FROM Aircrafts WHERE RegistrationNumber=?");
                ps->setString(1, regOld);
                ps->execute();
            }
        }
    }





    void SetupAirportsGrid() {
        grid_->ClearGrid();
        if (grid_->GetNumberRows() > 0)
            grid_->DeleteRows(0, grid_->GetNumberRows());
        if (grid_->GetNumberCols() > 0)
            grid_->DeleteCols(0, grid_->GetNumberCols());
        grid_->AppendCols(7);
        grid_->SetColLabelValue(0, "iata");
        grid_->SetColLabelValue(1, "icao");
        grid_->SetColLabelValue(2, "name");
        grid_->SetColLabelValue(3, "city");
        grid_->SetColLabelValue(4, "country");
        grid_->SetColLabelValue(5, "latitude");
        grid_->SetColLabelValue(6, "longitude");
        grid_->AutoSizeColumns();
    }

    void LoadAirports() {
        SetupAirportsGrid();
        airportsBeforeIata_.clear();

        auto st = Db::instance().createStatement();
        std::unique_ptr<sql::ResultSet> rs(
            st->executeQuery(
                "SELECT iata, icao, name, city, country, latitude, longitude "
                "FROM airports ORDER BY city, name"));

        int row = 0;
        while (rs->next()) {
            grid_->AppendRows(1);
            std::string iata = rs->isNull("iata") ? "" : rs->getString("iata");
            airportsBeforeIata_.push_back(iata);

            grid_->SetCellValue(row, 0, wxString::FromUTF8(iata));
            grid_->SetCellValue(row, 1, wxString::FromUTF8(rs->getString("icao")));
            grid_->SetCellValue(row, 2, wxString::FromUTF8(rs->getString("name")));
            grid_->SetCellValue(row, 3, wxString::FromUTF8(rs->getString("city")));
            grid_->SetCellValue(row, 4, wxString::FromUTF8(rs->getString("country")));
            grid_->SetCellValue(row, 5, wxString::Format("%.6f", rs->getDouble("latitude")));
            grid_->SetCellValue(row, 6, wxString::Format("%.6f", rs->getDouble("longitude")));
            ++row;
        }
        grid_->AutoSizeColumns();
    }

    void SaveAirports() {
        int rows = grid_->GetNumberRows();
        std::set<std::string> currentIata;

        for (int r = 0; r < rows; ++r) {
            wxString iata = grid_->GetCellValue(r, 0).Strip(wxString::both);
            wxString icao = grid_->GetCellValue(r, 1).Strip(wxString::both);
            wxString name = grid_->GetCellValue(r, 2).Strip(wxString::both);
            wxString city = grid_->GetCellValue(r, 3).Strip(wxString::both);
            wxString country = grid_->GetCellValue(r, 4).Strip(wxString::both);
            wxString latStr = grid_->GetCellValue(r, 5).Strip(wxString::both);
            wxString lonStr = grid_->GetCellValue(r, 6).Strip(wxString::both);

            if (!IsNonEmpty(name) || !IsNonEmpty(city) || !IsNonEmpty(country)) {
                throw AppException("Airport row " + std::to_string(r + 1) + ": name/city/country required.");
            }

            double lat = wxAtof(latStr);
            double lon = wxAtof(lonStr);

            // std::string – UTF-8, а не mb_str()
            std::string iataStd = std::string(iata.ToUTF8());
            currentIata.insert(iataStd);

            bool existedBefore =
                std::find(airportsBeforeIata_.begin(), airportsBeforeIata_.end(), iataStd)
                != airportsBeforeIata_.end();

            if (existedBefore) {
                auto ps = Db::instance().prepare(
                    "UPDATE airports SET icao=?, name=?, city=?, country=?, latitude=?, longitude=? "
                    "WHERE iata=?");
                ps->setString(1, std::string(icao.ToUTF8()));
                ps->setString(2, std::string(name.ToUTF8()));
                ps->setString(3, std::string(city.ToUTF8()));
                ps->setString(4, std::string(country.ToUTF8()));
                ps->setDouble(5, lat);
                ps->setDouble(6, lon);
                ps->setString(7, iataStd);
                ps->execute();
            }
            else {
                auto ps = Db::instance().prepare(
                    "INSERT INTO airports (iata, icao, name, city, country, latitude, longitude) "
                    "VALUES (?,?,?,?,?,?,?)");
                ps->setString(1, iataStd);
                ps->setString(2, std::string(icao.ToUTF8()));
                ps->setString(3, std::string(name.ToUTF8()));
                ps->setString(4, std::string(city.ToUTF8()));
                ps->setString(5, std::string(country.ToUTF8()));
                ps->setDouble(6, lat);
                ps->setDouble(7, lon);
                ps->execute();
            }
        }

        for (const auto& old : airportsBeforeIata_) {
            if (currentIata.find(old) == currentIata.end()) {
                auto ps = Db::instance().prepare(
                    "DELETE FROM airports WHERE iata=?");
                ps->setString(1, old);
                ps->execute();
            }
        }
    }


    void SetupFlightsGrid() {
        grid_->ClearGrid();
        if (grid_->GetNumberRows() > 0)
            grid_->DeleteRows(0, grid_->GetNumberRows());
        if (grid_->GetNumberCols() > 0)
            grid_->DeleteCols(0, grid_->GetNumberCols());
        grid_->AppendCols(7);
        grid_->SetColLabelValue(0, "FlightNumber");
        grid_->SetColLabelValue(1, "DepartureAirport");
        grid_->SetColLabelValue(2, "ArrivalAirport");
        grid_->SetColLabelValue(3, "DepartureTime");
        grid_->SetColLabelValue(4, "ArrivalTime");
        grid_->SetColLabelValue(5, "AircraftType");
        grid_->SetColLabelValue(6, "Status");
        grid_->AutoSizeColumns();
    }

    static wxString TimePointToWx(const std::chrono::system_clock::time_point& tp) {
        std::time_t t = std::chrono::system_clock::to_time_t(tp);
        std::tm* tm = std::localtime(&t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
        return wxString::FromUTF8(buf);
    }

    static std::chrono::system_clock::time_point WxToTimePoint(const wxString& sWx) {
        std::string s = std::string(sWx.mb_str());
        std::tm tm{};
        std::istringstream ss(s);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        if (ss.fail())
            throw AppException("Invalid datetime format: " + s +
                " (expected YYYY-MM-DD HH:MM:SS)");
        std::time_t t = std::mktime(&tm);
        return std::chrono::system_clock::from_time_t(t);
    }

    void LoadFlights() {
        SetupFlightsGrid();
        flightsBeforeNumbers_.clear();

        Flight::Filter f;
        auto flights = Db::instance().QueryFlights(f);
        int row = 0;
        for (const auto& fl : flights) {
            grid_->AppendRows(1);
            flightsBeforeNumbers_.push_back(fl.FLIGHTNUMBER);

            grid_->SetCellValue(row, 0, wxString::FromUTF8(fl.FLIGHTNUMBER));
            grid_->SetCellValue(row, 1, wxString::FromUTF8(fl.DEPARTURE_AIRPORT));
            grid_->SetCellValue(row, 2, wxString::FromUTF8(fl.ARRIVAL_AIRPORT));
            grid_->SetCellValue(row, 3, TimePointToWx(fl.DEPARTURE_TIME));
            grid_->SetCellValue(row, 4, TimePointToWx(fl.ARRIVAL_TIME));
            grid_->SetCellValue(row, 5, wxString::FromUTF8(fl.AIRCRAFT_TYPE));
            grid_->SetCellValue(row, 6, wxString::FromUTF8(fl.STATUS));
            ++row;
        }
        grid_->AutoSizeColumns();
    }

    void SaveFlights() {
        int rows = grid_->GetNumberRows();
        std::vector<Flight> toSave;
        std::set<std::string> currentNums;

        struct RowInfo {
            Flight fl;
            bool existedBefore{};
        };
        std::vector<RowInfo> parsed;
        parsed.reserve(rows);

        for (int r = 0; r < rows; ++r) {
            int rowNo = r + 1;

            wxString fn = grid_->GetCellValue(r, 0).Strip(wxString::both);
            wxString dep = grid_->GetCellValue(r, 1).Strip(wxString::both);
            wxString arr = grid_->GetCellValue(r, 2).Strip(wxString::both);
            wxString dt = grid_->GetCellValue(r, 3).Strip(wxString::both);
            wxString at = grid_->GetCellValue(r, 4).Strip(wxString::both);
            wxString ac = grid_->GetCellValue(r, 5).Strip(wxString::both);
            wxString st = grid_->GetCellValue(r, 6).Strip(wxString::both);

            if (fn.IsEmpty()) {
                throw AppException("Flight row " + std::to_string(rowNo) +
                    ": FlightNumber is required.");
            }
            fn.MakeUpper();
            {
                std::string fnStd = std::string(fn.mb_str());
                if (!Flight::IsValidFlightNumberFormat(fnStd)) {
                    throw AppException("Flight row " + std::to_string(rowNo) +
                        ": invalid FlightNumber format.");
                }
            }

            auto checkAirport = [&](wxString& code, const char* fieldName) {
                if (code.IsEmpty()) {
                    throw AppException("Flight row " + std::to_string(rowNo) +
                        ": " + std::string(fieldName) + " is required.");
                }
                code.MakeUpper();
                if (code.length() != 3) {
                    throw AppException("Flight row " + std::to_string(rowNo) +
                        ": " + std::string(fieldName) +
                        " must be 3-letter IATA code (e.g. LHR).");
                }
                for (wxUniChar ch : code) {
                    if (!wxIsalpha(ch)) {
                        throw AppException("Flight row " + std::to_string(rowNo) +
                            ": " + std::string(fieldName) +
                            " must contain only letters.");
                    }
                }
                };

            checkAirport(dep, "DepartureAirport");
            checkAirport(arr, "ArrivalAirport");

            if (dt.IsEmpty() || at.IsEmpty()) {
                throw AppException("Flight row " + std::to_string(rowNo) +
                    ": DepartureTime and ArrivalTime are required.");
            }
            auto depTime = WxToTimePoint(dt);
            auto arrTime = WxToTimePoint(at);

            if (ac.IsEmpty()) {
                throw AppException("Flight row " + std::to_string(rowNo) +
                    ": AircraftType is required.");
            }
            ac.MakeUpper();
            {
                wxString acCheck = ac;
                acCheck.Replace("-", "");
                std::string acStdCheck = std::string(acCheck.mb_str());
                if (!Flight::IsValidFlightNumberFormat(acStdCheck)) {
                    throw AppException("Flight row " + std::to_string(rowNo) +
                        ": invalid AircraftType format.");
                }
            }

            if (!ErrorHandler::IsLettersAndSpaces(st)) {
                throw AppException("Flight row " + std::to_string(rowNo) +
                    ": Status must contain only letters and spaces.");
            }
            st.MakeUpper();  // всегда CAPS

            Flight fl;
            fl.FLIGHTNUMBER = std::string(fn.mb_str());
            fl.DEPARTURE_AIRPORT = std::string(dep.mb_str());
            fl.ARRIVAL_AIRPORT = std::string(arr.mb_str());
            fl.DEPARTURE_TIME = depTime;
            fl.ARRIVAL_TIME = arrTime;
            fl.AIRCRAFT_TYPE = std::string(ac.mb_str());
            fl.STATUS = std::string(st.mb_str());
            fl.AVAILABLE_SEATS = 0;  // пересчитается позже

            fl.Validate();

            RowInfo info;
            info.fl = fl;
            info.existedBefore =
                std::find(flightsBeforeNumbers_.begin(),
                    flightsBeforeNumbers_.end(),
                    fl.FLIGHTNUMBER) != flightsBeforeNumbers_.end();

            parsed.push_back(std::move(info));
            currentNums.insert(fl.FLIGHTNUMBER);
        }

        std::vector<Flight> flightsForSeats;
        flightsForSeats.reserve(parsed.size());
        for (const auto& ri : parsed)
            flightsForSeats.push_back(ri.fl);
        Flight::ComputeAvailableSeats(flightsForSeats);

        for (size_t i = 0; i < parsed.size(); ++i) {
            parsed[i].fl.AVAILABLE_SEATS = flightsForSeats[i].AVAILABLE_SEATS;
        }

        for (const auto& ri : parsed) {
            const Flight& fl = ri.fl;

            if (ri.existedBefore) {
                auto ps = Db::instance().prepare(
                    "UPDATE Flights SET "
                    "DepartureAirport=?, ArrivalAirport=?, "
                    "DepartureTime=?, ArrivalTime=?, "
                    "AircraftType=?, Status=?, AvailableSeats=? "
                    "WHERE FlightNumber=?");
                ps->setString(1, fl.DEPARTURE_AIRPORT);
                ps->setString(2, fl.ARRIVAL_AIRPORT);
                ps->setString(3, TimePointToSql(fl.DEPARTURE_TIME));
                ps->setString(4, TimePointToSql(fl.ARRIVAL_TIME));
                ps->setString(5, fl.AIRCRAFT_TYPE);
                ps->setString(6, fl.STATUS);
                ps->setInt(7, fl.AVAILABLE_SEATS);
                ps->setString(8, fl.FLIGHTNUMBER);
                ps->execute();
            }
            else {
                auto ps = Db::instance().prepare(
                    "INSERT INTO Flights "
                    "(FlightNumber, DepartureAirport, ArrivalAirport, "
                    "DepartureTime, ArrivalTime, AircraftType, Status, "
                    "AvailableSeats) "
                    "VALUES (?,?,?,?,?,?,?,?)");
                ps->setString(1, fl.FLIGHTNUMBER);
                ps->setString(2, fl.DEPARTURE_AIRPORT);
                ps->setString(3, fl.ARRIVAL_AIRPORT);
                ps->setString(4, TimePointToSql(fl.DEPARTURE_TIME));
                ps->setString(5, TimePointToSql(fl.ARRIVAL_TIME));
                ps->setString(6, fl.AIRCRAFT_TYPE);
                ps->setString(7, fl.STATUS);
                ps->setInt(8, fl.AVAILABLE_SEATS);
                ps->execute();
            }
        }

        for (const auto& oldFn : flightsBeforeNumbers_) {
            if (currentNums.find(oldFn) == currentNums.end()) {
                auto ps = Db::instance().prepare(
                    "DELETE FROM Flights WHERE FlightNumber=?");
                ps->setString(1, oldFn);
                ps->execute();
            }
        }
    }


};

void ShowAdminRefEditor(wxWindow* parent)
{
    (new AdminRefEditFrame(parent))->Show();
}
