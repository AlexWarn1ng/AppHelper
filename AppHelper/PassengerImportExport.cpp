#include "Header.h"
#include <xlnt/xlnt.hpp>
#include <wx/wx.h>
#include <wx/filedlg.h>
#include <wx/choice.h>
#include <wx/grid.h>
#include "PassengerImportExport.h"
#include <sstream>
#include <iomanip>
#include <set>
#include <map>

static int GetNextPassengerNumberFromDb() {
    int maxId = 0;
    auto st = Db::instance().createStatement();
    std::unique_ptr<sql::ResultSet> rs(st->executeQuery(
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

static void WritePassengersToXlsx(const std::string& path, const std::vector<Passenger>& passengers) {
    xlnt::workbook wb;
    {
        auto titles = wb.sheet_titles();
        for (const auto& t : titles)
            wb.remove_sheet(wb.sheet_by_title(t));
    }
    auto ws = wb.create_sheet();
    ws.title("Passengers");

    const char* H[] = {
        "PassengerID","FullName","DocNumber","FlightNumber",
        "Baggage","Food","SeatNumber","PhoneNumber","Email"
    };
    for (int i = 0; i < 9; ++i)
        ws.cell(1 + i, 1).value(H[i]);

    int row = 2;
    for (std::size_t i = 0; i < passengers.size(); ++i) {
        Passenger p = passengers[i];

        ws.cell(1, row).value(p.PASSENGERID);
         ws.cell(2, row).value(p.FULLNAME);
         ws.cell(3, row).value(p.DOCNUMBER);
        ws.cell(4, row).value(p.FLIGHTNUMBER);
         ws.cell(5, row).value(p.BAGGAGE ? 1 : 0);
        ws.cell(6, row).value(p.FOOD ? 1 : 0);
        ws.cell(7, row).value(p.SEATNUMBER);
        ws.cell(8, row).value(p.PHONENUMBER);
        ws.cell(9, row).value(p.EMAIL);
        ++row;
    }

    for (int c = 1; c <= 9; ++c) {
        auto& cp = ws.column_properties(c);
        cp.hidden = false;
        cp.width = 14.0;
    }

    wb.save(path); // Первое сохранение

    try {// перепроверка открытия что файл сохранен норм
        xlnt::workbook check;
        check.load(path);
        auto cws = check.active_sheet();
        const auto a1 = cws.cell(1, 1).to_string();
        (void)a1;
    }
    catch (...) {
    }
}

std::vector<Passenger> ReadPassengersFromXlsx(const std::string& path)
{
    xlnt::workbook wb;
    wb.load(path);
    auto ws = wb.active_sheet();

    std::map<std::string, int> col;
    int lastCol = ws.highest_column().index;
    for (int c = 1; c <= lastCol; ++c) {
        auto v = ws.cell(c, 1).to_string();
        if (!v.empty()) col[v] = c;
    }

    std::vector<Passenger> passengers;
    int rLast = ws.highest_row();

    auto strAt = [&](int c, int r)->std::string {
        return (c > 0) ? ws.cell(c, r).to_string() : "";
        };
    auto intAt = [&](int c, int r)->int {
        if (c <= 0) return 0;
        try { return std::stoi(ws.cell(c, r).to_string()); }
        catch (...) { return 0; }
        };

    int cPid = 0;
    auto itPid = col.find("PassengerID");
    if (itPid != col.end())
        cPid = itPid->second;

    for (int r = 2; r <= rLast; ++r) {
        Passenger p;

        if (cPid > 0) {
            p.PASSENGERID = strAt(cPid, r);
        }
        else {
            p.PASSENGERID.clear();
        }

        p.FULLNAME = strAt(col["FullName"], r);
        p.DOCNUMBER = strAt(col["DocNumber"], r);
        p.FLIGHTNUMBER = strAt(col["FlightNumber"], r);
        p.BAGGAGE = intAt(col["Baggage"], r) != 0;
        p.FOOD = intAt(col["Food"], r) != 0;
        p.SEATNUMBER = strAt(col["SeatNumber"], r);
        p.PHONENUMBER = strAt(col["PhoneNumber"], r);
        p.EMAIL = strAt(col["Email"], r);

        passengers.push_back(std::move(p));
    }

    return passengers;
}

std::vector<Passenger> FilterPassengers(const std::vector<Passenger>& all, const Passenger::Filter& f)
{
    if (f.empty()) return all;
    std::vector<Passenger> out;
    out.reserve(all.size());

    // Если фильтр не задан то строка пройдет автоматически если фильтр задан строка должна совпадать с требованием фильтрации иначе фолс
    for (const auto& p : all) {
        auto match = [&](const std::optional<std::string>& opt, const std::string& val) {
            return !opt || (*opt == val); //Если пустой то тру если если опт не пустой и значения равны то тру иначе фолс
            };
        auto matchi = [&](const std::optional<int>& opt, int val) {
            return !opt || (*opt == val);
            };

        if (match(f.PassengerID, p.PASSENGERID) && match(f.FullName, p.FULLNAME)  && match(f.DocNumber, p.DOCNUMBER)  && match(f.FlightNumber, p.FLIGHTNUMBER)
            && match(f.SeatNumber, p.SEATNUMBER) && match(f.Email, p.EMAIL) && match(f.PhoneNumber, p.PHONENUMBER) && matchi(f.Baggage, p.BAGGAGE ? 1 : 0) && matchi(f.Food, p.FOOD ? 1 : 0)){
            out.push_back(p);
        }
    }
    return out;
}

class PassengersEditDialog : public wxDialog {
public:
    PassengersEditDialog(wxWindow* parent, const std::vector<Passenger>& data, const wxString& title, bool requirePassengerId,  bool validate) : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxSize(980, 520),
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER), requirePid_(requirePassengerId), validate_(validate) { //valide для того что бы проверки выполнялись
        auto* top = new wxBoxSizer(wxHORIZONTAL);
        auto* addBtn = new wxButton(this, wxID_ADD, "Add row");
        auto* delBtn = new wxButton(this, wxID_DELETE, "Delete selected");
        top->Add(addBtn, 0, wxRIGHT, 8);
        top->Add(delBtn, 0, wxRIGHT, 8);
        addBtn->Disable();
        delBtn->Disable();
        grid = new wxGrid(this, wxID_ANY);
        grid->CreateGrid(0, 9);
        const char* H[] = {
            "PassengerID","FullName","DocNumber","FlightNumber",
            "Baggage","Food","SeatNumber","PhoneNumber","Email"
        };
        for (int c = 0; c < 9; ++c) {
            grid->SetColLabelValue(c, H[c]);
            grid->SetColSize(c, 130);
        }
        grid->SetColFormatBool(4);
        grid->SetColFormatBool(5);
        grid->EnableEditing(false);
        grid->EnableDragRowSize(false);
        grid->EnableDragColSize(false);
        grid->EnableDragGridSize(false);
        grid->SetSelectionMode(wxGrid::wxGridSelectCells);
        grid->Bind(wxEVT_GRID_RANGE_SELECT,
            [](wxGridRangeSelectEvent& e) { e.Veto(); });
        grid->Bind(wxEVT_GRID_SELECT_CELL,
            [](wxGridEvent& e) { e.Veto(); });

        if (requirePid_) {
            auto* attr = new wxGridCellAttr();
            attr->SetReadOnly(true);
            attr->SetBackgroundColour(*wxLIGHT_GREY);
            grid->SetColAttr(0, attr);
        }
        else {
            grid->HideCol(0);
        }
        grid->AppendRows((int)data.size());
        for (int r = 0; r < (int)data.size(); ++r) {
            const auto& p = data[r];
            grid->SetCellValue(r, 0, p.PASSENGERID);
            grid->SetCellValue(r, 1, p.FULLNAME);
            grid->SetCellValue(r, 2, p.DOCNUMBER);
            grid->SetCellValue(r, 3, p.FLIGHTNUMBER);
            grid->SetCellValue(r, 4, p.BAGGAGE ? "1" : "0");
            grid->SetCellValue(r, 5, p.FOOD ? "1" : "0");
            grid->SetCellValue(r, 6, p.SEATNUMBER);
            grid->SetCellValue(r, 7, p.PHONENUMBER);
            grid->SetCellValue(r, 8, p.EMAIL);
        }
        auto* btns = new wxStdDialogButtonSizer();
        auto* ok = new wxButton(this, wxID_OK, "OK");
        auto* cancel = new wxButton(this, wxID_CANCEL, "Cancel");
        btns->AddButton(ok);
        btns->AddButton(cancel);
        btns->Realize();
        auto* s = new wxBoxSizer(wxVERTICAL);
        s->Add(top, 0, wxALL, 8);
        s->Add(grid, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
        s->Add(btns, 0, wxEXPAND | wxALL, 8);
        SetSizerAndFit(s);

        ok->Bind(wxEVT_BUTTON, &PassengersEditDialog::OnOK, this);
    }





    const std::vector<Passenger>& Result() const { return result_; }

private: 
    wxGrid* grid{};
    std::vector<Passenger> result_;
    bool requirePid_ = true;
    bool validate_ = true;

    void OnOK(wxCommandEvent&) {
        result_.clear();
        const int R = grid->GetNumberRows();

        if (!validate_) {
            for (int r = 0; r < R; ++r) {
                bool allEmpty = true;
                for (int c = 0; c < 9; ++c) {
                    if (!grid->GetCellValue(r, c).Strip(wxString::both).IsEmpty()) {
                        allEmpty = false;
                        break;
                    }
                }
                if (allEmpty) continue;

                Passenger p;

                wxString pid = grid->GetCellValue(r, 0).Strip(wxString::both);
                if (requirePid_ && pid.IsEmpty()) {
                    wxMessageBox(wxString::Format("Row %d: PassengerID must not be empty.", r + 1),
                        "Validation", wxOK | wxICON_WARNING, this);
                    result_.clear();
                    return;
                }
                if (!pid.IsEmpty())
                    p.PASSENGERID = std::string(pid.mb_str());

                p.FULLNAME = std::string(grid->GetCellValue(r, 1).mb_str());
                p.DOCNUMBER = std::string(grid->GetCellValue(r, 2).mb_str());
                p.FLIGHTNUMBER = std::string(grid->GetCellValue(r, 3).mb_str());
                p.BAGGAGE = ErrorHandler::CellToBool(grid->GetCellValue(r, 4));
                p.FOOD = ErrorHandler::CellToBool(grid->GetCellValue(r, 5));
                p.SEATNUMBER = std::string(grid->GetCellValue(r, 6).mb_str());
                p.PHONENUMBER = std::string(grid->GetCellValue(r, 7).mb_str());
                p.EMAIL = std::string(grid->GetCellValue(r, 8).mb_str());

                result_.push_back(std::move(p));
            }

            EndModal(wxID_OK);
            return;
        }

        using OccupiedMap = std::map<std::string, std::set<std::string>>;
        OccupiedMap occupiedSeats;
        try {
            auto st = Db::instance().createStatement();
            std::unique_ptr<sql::ResultSet> rs(
                st->executeQuery(
                    "SELECT FlightNumber, SeatNumber "
                    "FROM Passengers "
                    "WHERE SeatNumber IS NOT NULL AND SeatNumber <> ''"
                ));

            auto toUpper = [](std::string s) {
                for (char& ch : s)
                    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                return s;
                };

            while (rs->next()) {
                std::string fn = toUpper(rs->getString("FlightNumber"));
                std::string seat = toUpper(rs->getString("SeatNumber"));
                occupiedSeats[fn].insert(seat);
            }
        }
        catch (const std::exception& e) {
            wxMessageBox(wxString::Format("Error loading occupied seats: %s", e.what()),
                "Validation", wxOK | wxICON_ERROR, this);
            return;
        }

        std::set<std::pair<std::string, std::string>> seatsInFile;

        for (int r = 0; r < R; ++r) {
            int rowNo = r + 1;

            bool allEmpty = true;
            for (int c = 0; c < 9; ++c) {
                if (!grid->GetCellValue(r, c).Strip(wxString::both).IsEmpty()) {
                    allEmpty = false;
                    break;
                }
            }
            if (allEmpty) continue;

            Passenger p;

            wxString pid = grid->GetCellValue(r, 0).Strip(wxString::both);
            if (requirePid_ && pid.IsEmpty()) {
                wxMessageBox(wxString::Format("Row %d: PassengerID must not be empty.", rowNo),
                    "Validation", wxOK | wxICON_WARNING, this);
                result_.clear();
                return;
            }
            if (!pid.IsEmpty()) {
                p.PASSENGERID = std::string(pid.mb_str());
            }

            wxString full = grid->GetCellValue(r, 1).Strip(wxString::both);
            if (full.IsEmpty()) {
                wxMessageBox(wxString::Format("Row %d: FullName is required.", rowNo),
                    "Validation", wxOK | wxICON_WARNING, this);
                result_.clear();
                return;
            }
            if (!ErrorHandler::IsLettersAndSpaces(full)) {
                wxMessageBox(wxString::Format(
                    "Row %d: FullName must contain only letters and spaces.", rowNo),
                    "Validation", wxOK | wxICON_WARNING, this);
                result_.clear();
                return;
            }
            full = ErrorHandler::NormalizeFullName(full);
            p.FULLNAME = std::string(full.mb_str());

            wxString doc = grid->GetCellValue(r, 2).Strip(wxString::both);
            if (doc.IsEmpty()) {
                wxMessageBox(wxString::Format(
                    "Row %d: DocNumber is required and must contain only letters and digits, no spaces.", rowNo),
                    "Validation", wxOK | wxICON_WARNING, this);
                result_.clear();
                return;
            }
            if (!ErrorHandler::NormalizeDocNumber(doc)) {
                wxMessageBox(wxString::Format(
                    "Row %d: DocNumber is required and must contain only letters and digits, no spaces.", rowNo),
                    "Validation", wxOK | wxICON_WARNING, this);
                result_.clear();
                return;
            }
            p.DOCNUMBER = std::string(doc.mb_str());

            wxString flt = grid->GetCellValue(r, 3).Strip(wxString::both);
            if (flt.IsEmpty()) {
                wxMessageBox(wxString::Format("Row %d: FlightNumber is required.", rowNo),
                    "Validation", wxOK | wxICON_WARNING, this);
                result_.clear();
                return;
            }
            flt.MakeUpper();
            p.FLIGHTNUMBER = std::string(flt.mb_str());

            p.BAGGAGE = ErrorHandler::CellToBool(grid->GetCellValue(r, 4));
            p.FOOD = ErrorHandler::CellToBool(grid->GetCellValue(r, 5));

            wxString seat = grid->GetCellValue(r, 6).Strip(wxString::both);
            if (!ErrorHandler::IsValidSeat(seat)) {
                wxMessageBox(wxString::Format(
                    "Row %d: SeatNumber must have format NNX (two digits and one letter), e.g. 12B.", rowNo),
                    "Validation", wxOK | wxICON_WARNING, this);
                result_.clear();
                return;
            }
            seat.MakeUpper();
            p.SEATNUMBER = std::string(seat.mb_str());

            {
                std::string fnKey = p.FLIGHTNUMBER;
                std::string seatKey = p.SEATNUMBER;
                std::pair<std::string, std::string> key(fnKey, seatKey);

                if (!seatsInFile.insert(key).second) {
                    continue;
                }

                auto itFlight = occupiedSeats.find(fnKey);
                if (itFlight != occupiedSeats.end()) {
                    if (itFlight->second.find(seatKey) != itFlight->second.end()) {
                        wxMessageBox(wxString::Format(
                            "Row %d: Seat '%s' on flight '%s' is already occupied.",
                            rowNo,
                            seat,
                            wxString::FromUTF8(fnKey)
                        ),
                            "Validation", wxOK | wxICON_WARNING, this);
                        result_.clear();
                        return;
                    }
                }
            }

            wxString phone = grid->GetCellValue(r, 7).Strip(wxString::both);
            if (!ErrorHandler::IsValidPhone(phone)) {
                wxMessageBox(wxString::Format(
                    "Row %d: PhoneNumber must have format +7XXXXXXXXXX.", rowNo),
                    "Validation", wxOK | wxICON_WARNING, this);
                result_.clear();
                return;
            }
            p.PHONENUMBER = std::string(phone.mb_str());

            wxString email = grid->GetCellValue(r, 8).Strip(wxString::both);
            if (!ErrorHandler::IsValidEmail(email)) {
                wxMessageBox(wxString::Format(
                    "Row %d: Email must have format like user@host.domain.", rowNo),
                    "Validation", wxOK | wxICON_WARNING, this);
                result_.clear();
                return;
            }
            p.EMAIL = std::string(email.mb_str());

            result_.push_back(std::move(p));
        }

        EndModal(wxID_OK);
    }
};

class PassengerIEFrame : public wxFrame {
public:
    PassengerIEFrame(wxWindow* p = nullptr)
        : wxFrame(p, wxID_ANY, "Import / Export Passengers (.xlsx)",
            wxDefaultPosition, wxSize(520, 240))
    {
        auto* panel = new wxPanel(this);
        auto* colLbl = new wxStaticText(panel, wxID_ANY, "Filter by column:");
        m_col = new wxChoice(panel, wxID_ANY);
        m_col->Append("no filter");
        m_col->Append("FullName");
        m_col->Append("DocNumber");
        m_col->Append("FlightNumber");
        m_col->Append("Baggage");
        m_col->Append("Food");
        m_col->Append("SeatNumber");
        m_col->Append("PhoneNumber");
        m_col->Append("Email");
        m_col->SetSelection(0);
        m_val = new wxTextCtrl(panel, wxID_ANY);
        auto* importBtn = new wxButton(panel, wxID_ANY, "Import from .xlsx");
        auto* exportBtn = new wxButton(panel, wxID_ANY, "Export to .xlsx");
        auto* s = new wxBoxSizer(wxVERTICAL);
        auto* row = new wxFlexGridSizer(2, 8, 8);
            row->AddGrowableCol(1, 1);
            row->Add(colLbl, 0, wxALIGN_CENTER_VERTICAL);
            row->Add(m_col, 1, wxEXPAND);
            row->Add(new wxStaticText(panel, wxID_ANY, "Value:"), 0, wxALIGN_CENTER_VERTICAL);
             row->Add(m_val, 1, wxEXPAND);
        s->AddSpacer(10);
        s->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
        s->AddSpacer(10);
        s->Add(importBtn, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
        s->Add(exportBtn, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
        panel->SetSizer(s);
        importBtn->Bind(wxEVT_BUTTON, &PassengerIEFrame::OnImport, this);
        exportBtn->Bind(wxEVT_BUTTON, &PassengerIEFrame::OnExport, this);
    }

private:
    wxChoice* m_col{};
    wxTextCtrl* m_val{};

    Passenger::Filter MakeFilter() {
        Passenger::Filter f;
        int sel = m_col->GetSelection();
        wxString v = m_val->GetValue();
        if (sel <= 0 || v.IsEmpty()) return f;

        auto setS = [&](std::optional<std::string>& dst) { dst = std::string(v.mb_str()); };
        auto setI = [&](std::optional<int>& dst) { long x = 0; v.ToLong(&x); dst = int(x); };

        wxString name = m_col->GetString(sel);
        if (name == "FullName")          setS(f.FullName);
        else if (name == "DocNumber")    setS(f.DocNumber);
        else if (name == "FlightNumber") setS(f.FlightNumber);
        else if (name == "SeatNumber")   setS(f.SeatNumber);
        else if (name == "PhoneNumber")  setS(f.PhoneNumber);
        else if (name == "Email")        setS(f.Email);
        else if (name == "Baggage")      setI(f.Baggage);
        else if (name == "Food")         setI(f.Food);
        return f;
    }

    void OnExport(wxCommandEvent&) {
        Passenger::Filter f = MakeFilter();

        wxFileDialog dlg(this, "Save Excel file", "", "passengers.xlsx", "Excel files (*.xlsx)|*.xlsx", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg.ShowModal() != wxID_OK) return;

        try {
            auto passengers = Db::instance().QueryPassengers(f);

            if (passengers.empty()) {
                auto all = Db::instance().QueryPassengers(Passenger::Filter{});

                if (all.empty()) {
                    wxMessageBox(
                        "The database returned 0 rows with no filter applied.\n\n"
                        "Please check:\n"
                        "• The correct schema is set in Db::init(...)\n"
                        "• The 'passengers' table is not empty",
                        "Nothing to export", wxOK | wxICON_INFORMATION, this);
                    return;
                }

                wxMessageBox(
                    "No records found for the specified filter.\n"
                    "Try clearing the 'Value' field or selecting '-- no filter --'.",
                    "Nothing matched", wxOK | wxICON_INFORMATION, this);
                return;
            }

            {
                PassengersEditDialog prev(this, passengers, "Preview export", true, false );
                if (prev.ShowModal() != wxID_OK) return;
                passengers = prev.Result();
                if (passengers.empty()) {
                    wxMessageBox("There are no rows to export.", "Export", wxOK | wxICON_INFORMATION, this);
                    return;
                }
            }

            WritePassengersToXlsx(std::string(dlg.GetPath().mb_str()), passengers);
            wxMessageBox(wxString::Format("Export completed! Rows: %zu", passengers.size()),"Export", wxOK | wxICON_INFORMATION, this);
        }
        catch (const std::exception& e) {
            wxMessageBox(wxString::Format("Error: %s", e.what()), "Export Error", wxOK | wxICON_ERROR, this);
        }
    }

    void OnImport(wxCommandEvent&) {
        Passenger::Filter f = MakeFilter();
        wxFileDialog dlg(this, "Open Excel file", "", "","Excel files (*.xlsx)|*.xlsx", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() != wxID_OK) return;

        try {
            auto imported = ReadPassengersFromXlsx(std::string(dlg.GetPath().mb_str()));
            auto filtered = FilterPassengers(imported, f);

            if (filtered.empty()) {
                wxMessageBox("The file has been read, but no rows match the current filter.","Import", wxOK | wxICON_INFORMATION, this);
                return;
            }

            {
                PassengersEditDialog prev(this, filtered, "Preview import", false, true );
                if (prev.ShowModal() != wxID_OK) return;
                filtered = prev.Result();

               /* if (filtered.empty()) {
                    wxMessageBox("There are no rows to import.", "Import", wxOK | wxICON_INFORMATION, this); //Бесполезная проверка но все же, нужна что бы пользователь не накуралесил ничего после того как программа считала данные из дб
                    return;
                }*/
            }   

            int nextNum = GetNextPassengerNumberFromDb();
            for (auto& p : filtered) {
                if (p.PASSENGERID.empty()) {
                    p.PASSENGERID = MakePassengerId(nextNum++);
                }
            }

            Db::instance().InsertOrUpdatePassengers(filtered);

            wxMessageBox(wxString::Format("Imported %zu passengers", filtered.size()),
                "Import", wxOK | wxICON_INFORMATION, this);
        }
        catch (const std::exception& e) {
            wxMessageBox(wxString::Format("Error: %s", e.what()),
                "Import Error", wxOK | wxICON_ERROR, this);
        }
    }
};

void ShowPassengerImportExport(wxWindow* parent)
{
    (new PassengerIEFrame(parent))->Show();
}
