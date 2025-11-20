#include "Header.h"
#include <xlnt/xlnt.hpp>
#include <wx/wx.h>
#include <wx/filedlg.h>
#include <wx/choice.h>
#include <wx/grid.h>            // for preview/edit grid
#include "PassengerImportExport.h"
#include <sstream>
#include <iomanip>
#include <set>

// =====================================================
//  ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ (такие же, как в EditFindPassenger)
// =====================================================

static bool IsLettersAndSpaces(const wxString& s) {
    for (wxUniChar c : s) {
        if (wxIsspace(c)) continue;
        if (!wxIsalpha(c)) return false;
    }
    return true;
}

static wxString NormalizeFullName(wxString s) {
    s.Trim(true).Trim(false); // удалить пробелы по краям
    s.MakeLower();
    bool newWord = true;
    for (size_t i = 0; i < s.length(); ++i) {
        wxUniChar c = s[i];
        if (wxIsspace(c)) {
            newWord = true;
        }
        else if (newWord) {
            s[i] = wxToupper(c);
            newWord = false;
        }
    }
    return s;
}

static bool IsValidSeat(wxString s) {
    s.Trim(true).Trim(false);
    if (s.length() != 3) return false;
    wxUniChar c0 = s[0], c1 = s[1], c2 = s[2];
    if (!wxIsdigit(c0) || !wxIsdigit(c1) || !wxIsalpha(c2))
        return false;
    return true;
}

static bool IsValidPhone(const wxString& s) {
    wxString p = s;
    p.Trim(true).Trim(false);
    if (!p.StartsWith("+7")) return false;
    if (p.length() != 12) return false; // +7 и 10 цифр
    for (size_t i = 2; i < p.length(); ++i) {
        if (!wxIsdigit(p[i])) return false;
    }
    return true;
}

static bool IsValidEmail(const wxString& s) {
    wxString e = s;
    e.Trim(true).Trim(false);
    if (e.IsEmpty()) return false;
    if (e.Find(' ') != wxNOT_FOUND) return false;

    int atPos = e.Find('@');
    if (atPos <= 0) return false; // @ не в начале и вообще есть

    int dotPos = e.Find('.', true); // true = искать с конца
    if (dotPos == wxNOT_FOUND) return false;
    if (dotPos <= atPos + 1) return false;           // точка сразу после @ — плохо
    if (dotPos >= (int)e.length() - 1) return false; // точка последняя — плохо

    return true;
}

// =====================================================
//  АВТОИНКРЕМЕНТ ID (формат P001, P002, ...)
// =====================================================

// Возвращает следующий свободный номер (цифровую часть после 'P')
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

// =====================================================
//  XLSX HELPERS
// =====================================================

static void WritePassengersToXlsx(const std::string& path,
    const std::vector<Passenger>& passengers)
{
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
    for (const auto& p : passengers) {
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

    wb.save(path);

    try {
        xlnt::workbook check;
        check.load(path);
        auto cws = check.active_sheet();
        const auto a1 = cws.cell(1, 1).to_string();
        if (a1 != "PassengerID") {
            // optional sanity check
        }
    }
    catch (...) {
    }
}

std::vector<Passenger> ReadPassengersFromXlsx(const std::string& path)
{
    xlnt::workbook wb;
    wb.load(path);
    auto ws = wb.active_sheet();

    // Сопоставляем названия колонок с номерами
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

    // Колонка PassengerID может отсутствовать
    int cPid = 0;
    auto itPid = col.find("PassengerID");
    if (itPid != col.end())
        cPid = itPid->second;

    for (int r = 2; r <= rLast; ++r) {
        Passenger p;

        // Если колонка есть — читаем; если нет — оставляем пустым
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

    for (const auto& p : all) {
        auto match = [&](const std::optional<std::string>& opt, const std::string& val) {
            return !opt || (*opt == val);
            };
        auto matchi = [&](const std::optional<int>& opt, int val) {
            return !opt || (*opt == val);
            };

        if (match(f.PassengerID, p.PASSENGERID)
            && match(f.FullName, p.FULLNAME)
            && match(f.DocNumber, p.DOCNUMBER)
            && match(f.FlightNumber, p.FLIGHTNUMBER)
            && match(f.SeatNumber, p.SEATNUMBER)
            && match(f.Email, p.EMAIL)
            && match(f.PhoneNumber, p.PHONENUMBER)
            && matchi(f.Baggage, p.BAGGAGE ? 1 : 0)
            && matchi(f.Food, p.FOOD ? 1 : 0)) {
            out.push_back(p);
        }
    }
    return out;
}

// =====================================================
//  Preview/Edit dialog
// =====================================================
class PassengersEditDialog : public wxDialog {
public:
    PassengersEditDialog(wxWindow* parent,
        const std::vector<Passenger>& data,
        const wxString& title,
        bool requirePassengerId,
        bool validate)                // <<< новый флаг
        : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxSize(980, 520),
            wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        requirePid_(requirePassengerId),
        validate_(validate)
    {
        // Top toolbar
        auto* top = new wxBoxSizer(wxHORIZONTAL);
        auto* addBtn = new wxButton(this, wxID_ADD, "Add row");
        auto* delBtn = new wxButton(this, wxID_DELETE, "Delete selected");
        top->Add(addBtn, 0, wxRIGHT, 8);
        top->Add(delBtn, 0, wxRIGHT, 8);

        // --- делаем кнопки неактивными, чтобы ничего не меняли ---
        addBtn->Disable();
        delBtn->Disable();

        // Grid
        grid_ = new wxGrid(this, wxID_ANY);
        grid_->CreateGrid(0, 9);
        const char* H[] = {
            "PassengerID","FullName","DocNumber","FlightNumber",
            "Baggage","Food","SeatNumber","PhoneNumber","Email"
        };
        for (int c = 0; c < 9; ++c) {
            grid_->SetColLabelValue(c, H[c]);
            grid_->SetColSize(c, 130);
        }
        grid_->SetColFormatBool(4);
        grid_->SetColFormatBool(5);

        // ---------- ВАЖНО: полностью запрещаем редактирование ----------
        grid_->EnableEditing(false);

        // запрещаем изменение размера, перетаскивания и т.п.
        grid_->EnableDragRowSize(false);
        grid_->EnableDragColSize(false);
        grid_->EnableDragGridSize(false);

        // ---------- Запрещаем выделение строк/ячеек ----------
        grid_->SetSelectionMode(wxGrid::wxGridSelectCells);
        grid_->Bind(wxEVT_GRID_RANGE_SELECT,
            [](wxGridRangeSelectEvent& e) { e.Veto(); });
        grid_->Bind(wxEVT_GRID_SELECT_CELL,
            [](wxGridEvent& e) { e.Veto(); });

        // Если ID обязателен (экспорт) — делаем колонку только для чтения, как в EditFindPassenger
        if (requirePid_) {
            auto* attr = new wxGridCellAttr();
            attr->SetReadOnly(true);
            attr->SetBackgroundColour(*wxLIGHT_GREY);
            grid_->SetColAttr(0, attr);
        }
        else {
            // для импорта колонку PassengerID прячем
            grid_->HideCol(0);
        }

        // Fill grid
        grid_->AppendRows((int)data.size());
        for (int r = 0; r < (int)data.size(); ++r) {
            const auto& p = data[r];
            grid_->SetCellValue(r, 0, p.PASSENGERID);
            grid_->SetCellValue(r, 1, p.FULLNAME);
            grid_->SetCellValue(r, 2, p.DOCNUMBER);
            grid_->SetCellValue(r, 3, p.FLIGHTNUMBER);
            grid_->SetCellValue(r, 4, p.BAGGAGE ? "1" : "0");
            grid_->SetCellValue(r, 5, p.FOOD ? "1" : "0");
            grid_->SetCellValue(r, 6, p.SEATNUMBER);
            grid_->SetCellValue(r, 7, p.PHONENUMBER);
            grid_->SetCellValue(r, 8, p.EMAIL);
        }

        // Buttons
        auto* btns = new wxStdDialogButtonSizer();
        auto* ok = new wxButton(this, wxID_OK, "OK");
        auto* cancel = new wxButton(this, wxID_CANCEL, "Cancel");
        btns->AddButton(ok);
        btns->AddButton(cancel);
        btns->Realize();

        // Layout
        auto* s = new wxBoxSizer(wxVERTICAL);
        s->Add(top, 0, wxALL, 8);
        s->Add(grid_, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
        s->Add(btns, 0, wxEXPAND | wxALL, 8);
        SetSizerAndFit(s);

        // Events
        ok->Bind(wxEVT_BUTTON, &PassengersEditDialog::OnOK, this);
        // delBtn / addBtn уже отключены, бинды не нужны
    }

    const std::vector<Passenger>& Result() const { return result_; }

private:
    wxGrid* grid_{};
    std::vector<Passenger> result_;
    bool requirePid_ = true;
    bool validate_ = true;

    static bool ToBool(const wxString& s) {
        wxString ls = s.Lower().Strip(wxString::both);
        return (ls == "1" || ls == "true" || ls == "yes" || ls == "y" || ls == "on" || ls == "checked");
    }

    void OnOK(wxCommandEvent&) {
        result_.clear();
        const int R = grid_->GetNumberRows();

        // -------- РЕЖИМ БЕЗ ВАЛИДАЦИИ (экспорт) --------
        if (!validate_) {
            for (int r = 0; r < R; ++r) {
                bool allEmpty = true;
                for (int c = 0; c < 9; ++c) {
                    if (!grid_->GetCellValue(r, c).Strip(wxString::both).IsEmpty()) {
                        allEmpty = false;
                        break;
                    }
                }
                if (allEmpty) continue;

                Passenger p;

                wxString pid = grid_->GetCellValue(r, 0).Strip(wxString::both);
                if (requirePid_ && pid.IsEmpty()) {
                    // теоретически не должно случиться, ID read-only и уже есть в БД,
                    // но на всякий случай.
                    wxMessageBox(wxString::Format("Row %d: PassengerID must not be empty.", r + 1),
                        "Validation", wxOK | wxICON_WARNING, this);
                    result_.clear();
                    return;
                }
                if (!pid.IsEmpty())
                    p.PASSENGERID = std::string(pid.mb_str());

                p.FULLNAME = std::string(grid_->GetCellValue(r, 1).mb_str());
                p.DOCNUMBER = std::string(grid_->GetCellValue(r, 2).mb_str());
                p.FLIGHTNUMBER = std::string(grid_->GetCellValue(r, 3).mb_str());
                p.BAGGAGE = ToBool(grid_->GetCellValue(r, 4));
                p.FOOD = ToBool(grid_->GetCellValue(r, 5));
                p.SEATNUMBER = std::string(grid_->GetCellValue(r, 6).mb_str());
                p.PHONENUMBER = std::string(grid_->GetCellValue(r, 7).mb_str());
                p.EMAIL = std::string(grid_->GetCellValue(r, 8).mb_str());

                result_.push_back(std::move(p));
            }

            EndModal(wxID_OK);
            return;
        }

        // -------- РЕЖИМ С ВАЛИДАЦИЕЙ (импорт) --------

        // Загружаем список валидных рейсов, как в CollectFromGrid
        std::set<std::string> validFlights;
        try {
            auto flights = Db::instance().QueryFlights();
            for (const auto& f : flights) {
                validFlights.insert(f.FLIGHTNUMBER);
            }
        }
        catch (const std::exception& e) {
            wxMessageBox(wxString::Format("Error loading flights: %s", e.what()),
                "Validation", wxOK | wxICON_ERROR, this);
            return;
        }

        for (int r = 0; r < R; ++r) {
            int rowNo = r + 1;

            // Проверяем, не пустая ли вообще строка
            bool allEmpty = true;
            for (int c = 0; c < 9; ++c) {
                if (!grid_->GetCellValue(r, c).Strip(wxString::both).IsEmpty()) {
                    allEmpty = false;
                    break;
                }
            }
            if (allEmpty) continue;

            Passenger p;

            // 1) PassengerID — обязателен, если requirePid_ == true
            wxString pid = grid_->GetCellValue(r, 0).Strip(wxString::both);
            if (requirePid_ && pid.IsEmpty()) {
                wxMessageBox(wxString::Format("Row %d: PassengerID must not be empty.", rowNo),
                    "Validation", wxOK | wxICON_WARNING, this);
                result_.clear();
                return;
            }
            if (!pid.IsEmpty()) {
                p.PASSENGERID = std::string(pid.mb_str());
            }

            // 2) FullName — только буквы и пробелы, каждое слово с заглавной
            wxString full = grid_->GetCellValue(r, 1).Strip(wxString::both);
            if (full.IsEmpty()) {
                wxMessageBox(wxString::Format("Row %d: FullName is required.", rowNo),
                    "Validation", wxOK | wxICON_WARNING, this);
                result_.clear();
                return;
            }
            if (!IsLettersAndSpaces(full)) {
                wxMessageBox(wxString::Format("Row %d: FullName must contain only letters and spaces.", rowNo),
                    "Validation", wxOK | wxICON_WARNING, this);
                result_.clear();
                return;
            }
            full = NormalizeFullName(full);
            p.FULLNAME = std::string(full.mb_str());

            // 3) DocNumber — только буквы и цифры, без пробелов, верхний регистр
            wxString doc = grid_->GetCellValue(r, 2).Strip(wxString::both);
            if (doc.IsEmpty()) {
                wxMessageBox(wxString::Format("Row %d: DocNumber is required.", rowNo),
                    "Validation", wxOK | wxICON_WARNING, this);
                result_.clear();
                return;
            }
            for (wxUniChar c : doc) {
                if (!wxIsalnum(c)) {
                    wxMessageBox(wxString::Format(
                        "Row %d: DocNumber must contain only letters and digits, no spaces.", rowNo),
                        "Validation", wxOK | wxICON_WARNING, this);
                    result_.clear();
                    return;
                }
            }
            doc.MakeUpper();
            p.DOCNUMBER = std::string(doc.mb_str());

            // 4) FlightNumber — должен существовать в таблице flights
            wxString flt = grid_->GetCellValue(r, 3).Strip(wxString::both);
            if (flt.IsEmpty()) {
                wxMessageBox(wxString::Format("Row %d: FlightNumber is required.", rowNo),
                    "Validation", wxOK | wxICON_WARNING, this);
                result_.clear();
                return;
            }
            std::string fltStd = std::string(flt.mb_str());
            if (!validFlights.count(fltStd)) {
                wxMessageBox(wxString::Format(
                    "Row %d: FlightNumber '%s' does not exist in flights table.", rowNo, flt),
                    "Validation", wxOK | wxICON_WARNING, this);
                result_.clear();
                return;
            }
            p.FLIGHTNUMBER = fltStd;

            // 5) Baggage / Food — как раньше
            p.BAGGAGE = ToBool(grid_->GetCellValue(r, 4));
            p.FOOD = ToBool(grid_->GetCellValue(r, 5));

            // 6) SeatNumber — 2 цифры + 1 буква
            wxString seat = grid_->GetCellValue(r, 6).Strip(wxString::both);
            if (!IsValidSeat(seat)) {
                wxMessageBox(wxString::Format(
                    "Row %d: SeatNumber must have format NNX (two digits and one letter), e.g. 12B.", rowNo),
                    "Validation", wxOK | wxICON_WARNING, this);
                result_.clear();
                return;
            }
            seat.MakeUpper();
            p.SEATNUMBER = std::string(seat.mb_str());

            // 7) PhoneNumber — обязателен, +7XXXXXXXXXX
            wxString phone = grid_->GetCellValue(r, 7).Strip(wxString::both);
            if (!IsValidPhone(phone)) {
                wxMessageBox(wxString::Format(
                    "Row %d: PhoneNumber must have format +7XXXXXXXXXX.", rowNo),
                    "Validation", wxOK | wxICON_WARNING, this);
                result_.clear();
                return;
            }
            p.PHONENUMBER = std::string(phone.mb_str());

            // 8) Email — формат user@host.domain
            wxString email = grid_->GetCellValue(r, 8).Strip(wxString::both);
            if (!IsValidEmail(email)) {
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

// =====================================================
//  GUI FRAME
// =====================================================
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


    // Excel export — preview before saving XLSX
    void OnExport(wxCommandEvent&) {
        Passenger::Filter f = MakeFilter();

        wxFileDialog dlg(this, "Save Excel file", "", "passengers.xlsx",
            "Excel files (*.xlsx)|*.xlsx",
            wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
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

            // Preview / Edit (для экспорта: ID обязателен, НО без валидации)
            {
                PassengersEditDialog prev(this, passengers, "Preview export",
                    true,   // requirePassengerId
                    false   // validate = false (просто собрать данные)
                );
                if (prev.ShowModal() != wxID_OK) return; // cancelled
                passengers = prev.Result();
                if (passengers.empty()) {
                    wxMessageBox("There are no rows to export.",
                        "Export", wxOK | wxICON_INFORMATION, this);
                    return;
                }
            }

            WritePassengersToXlsx(std::string(dlg.GetPath().mb_str()), passengers);
            wxMessageBox(wxString::Format("Export completed! Rows: %zu", passengers.size()),
                "Export", wxOK | wxICON_INFORMATION, this);
        }
        catch (const std::exception& e) {
            wxMessageBox(wxString::Format("Error: %s", e.what()),
                "Export Error", wxOK | wxICON_ERROR, this);
        }
    }

    // Excel import — preview before writing to DB
    void OnImport(wxCommandEvent&) {
        Passenger::Filter f = MakeFilter();
        wxFileDialog dlg(this, "Open Excel file", "", "",
            "Excel files (*.xlsx)|*.xlsx",
            wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() != wxID_OK) return;

        try {
            // читаем файл
            auto imported = ReadPassengersFromXlsx(std::string(dlg.GetPath().mb_str()));
            auto filtered = FilterPassengers(imported, f);

            if (filtered.empty()) {
                wxMessageBox("The file has been read, but no rows match the current filter.",
                    "Import", wxOK | wxICON_INFORMATION, this);
                return;
            }

            // предварительный просмотр (импорт: ID не обязателен, НО с валидацией)
            {
                PassengersEditDialog prev(this, filtered, "Preview import",
                    false,  // requirePassengerId
                    true    // validate = true (строгие проверки)
                );
                if (prev.ShowModal() != wxID_OK) return; // cancelled
                filtered = prev.Result();

                if (filtered.empty()) {
                    wxMessageBox("There are no rows to import.",
                        "Import", wxOK | wxICON_INFORMATION, this);
                    return;
                }
            }

            // ГЕНЕРАЦИЯ ID: P001, P002, ... для тех, у кого PASSENGERID пустой
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
