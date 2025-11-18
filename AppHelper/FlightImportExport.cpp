#include "Header.h"
#include <xlnt/xlnt.hpp>
#include <wx/wx.h>
#include <wx/filedlg.h>
#include <wx/choice.h>
#include <wx/grid.h>
#include <sstream>
#include <iomanip>
#include <map>
#include <stdexcept>
#include "FlightImportExport.h"

// ====================== helpers: time <-> string =============================

// формат для Excel и для записи в БД: "YYYY-MM-DD HH:MM"
static std::string TimePointToString(const std::chrono::system_clock::time_point& tp)
{
    if (tp.time_since_epoch().count() == 0)
        return "";

    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[20];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

static bool IsExcelNumber(const std::string& s)
{
    if (s.empty()) return false;
    bool dot = false;
    for (char c : s)
    {
        if (c >= '0' && c <= '9') continue;
        if (c == '.' && !dot) { dot = true; continue; }
        return false;
    }
    return true;
}

// Excel: количество дней с 1899-12-30 (45717.375 и т.п.)
static std::chrono::system_clock::time_point FromExcelSerial(double serial)
{
    using namespace std::chrono;

    int days = (int)std::floor(serial);
    double frac = serial - days;           // доля суток

    // базовая дата 1899-12-30 00:00
    std::tm base{};
    base.tm_year = 1899 - 1900;            // годы с 1900
    base.tm_mon = 12 - 1;                 // месяцы с 0
    base.tm_mday = 30;

    std::time_t t0 = std::mktime(&base);   // time_t базовой даты
    // добавляем целые дни и дробную часть суток
    auto tp = system_clock::from_time_t(t0)
        + hours(24 * days)
        + duration_cast<system_clock::duration>(duration<double>(frac * 24.0 * 3600.0));
    return tp;
}

static std::chrono::system_clock::time_point StringToTimePoint(const std::string& s)
{
    if (s.empty())
        return std::chrono::system_clock::time_point{};

    // 1) Если это чистое число типа "45717.375" — считаем как Excel-дату
    if (IsExcelNumber(s))
    {
        double serial = std::stod(s);
        return FromExcelSerial(serial);
    }

    // 2) Иначе пытаемся распарсить как текст в формате "YYYY-MM-DD HH:MM"
    std::tm tm{};
    std::istringstream iss(s);
    iss >> std::get_time(&tm, "%Y-%m-%d %H:%M");
    if (iss.fail())
        throw std::runtime_error("Invalid datetime format: " + s +
            " (expected YYYY-MM-DD HH:MM or Excel serial)");

    std::time_t t = std::mktime(&tm);
    return std::chrono::system_clock::from_time_t(t);
}

// для записи в БД (можно тот же формат)
static std::string TimePointToSql(const std::chrono::system_clock::time_point& tp)
{
    return TimePointToString(tp); // достаточно
}

// ====================== XLSX helpers for Flight ==============================

// В Excel храним ТОЛЬКО данные, которые пользователь должен заполнять руками.
// Колонок про места тут нет: они считаются по таблицам aircrafts/passengers.
static void WriteFlightsToXlsx(const std::string& path,
    const std::vector<Flight>& flights)
{
    xlnt::workbook wb;
    {
        auto titles = wb.sheet_titles();
        for (const auto& t : titles)
            wb.remove_sheet(wb.sheet_by_title(t));
    }

    auto ws = wb.create_sheet();
    ws.title("Flights");

    const char* H[] = {
        "FlightNumber",
        "DepartureAirport",
        "ArrivalAirport",
        "DepartureTime",
        "ArrivalTime",
        "AircraftType",        
        "Status"
    };

    for (int i = 0; i < 7; ++i)
        ws.cell(1 + i, 1).value(H[i]);

    int row = 2;
    for (const auto& f : flights) {
        ws.cell(1, row).value(f.FLIGHTNUMBER);
        ws.cell(2, row).value(f.DEPARTURE_AIRPORT);
        ws.cell(3, row).value(f.ARRIVAL_AIRPORT);
        ws.cell(4, row).value(TimePointToString(f.DEPARTURE_TIME));
        ws.cell(5, row).value(TimePointToString(f.ARRIVAL_TIME));
        ws.cell(6, row).value(f.AIRCRAFT_TYPE);   // это RegistrationNumber
        ws.cell(7, row).value(f.STATUS);
        ++row;
    }

    for (int c = 1; c <= 7; ++c) {
        auto& cp = ws.column_properties(c);
        cp.hidden = false;
        cp.width = 18.0;
    }

    wb.save(path);
}

// чтение рейсов из Excel
static std::vector<Flight> ReadFlightsFromXlsx(const std::string& path)
{
    xlnt::workbook wb;
    wb.load(path);
    auto ws = wb.active_sheet();

    std::map<std::string, int> col;
    int lastCol = ws.highest_column().index;
    for (int c = 1; c <= lastCol; ++c) {
        auto v = ws.cell(c, 1).to_string();
        if (!v.empty())
            col[v] = c;
    }

    auto strAt = [&](const char* name, int r) -> std::string {
        auto it = col.find(name);
        if (it == col.end())
            return "";
        return ws.cell(it->second, r).to_string();
        };

    std::vector<Flight> out;
    int rLast = ws.highest_row();
    for (int r = 2; r <= rLast; ++r) {
        Flight f;
        f.FLIGHTNUMBER = strAt("FlightNumber", r);
        if (f.FLIGHTNUMBER.empty())
            continue; // пустая строка

        f.DEPARTURE_AIRPORT = strAt("DepartureAirport", r);
        f.ARRIVAL_AIRPORT = strAt("ArrivalAirport", r);

        auto depStr = strAt("DepartureTime", r);
        auto arrStr = strAt("ArrivalTime", r);
        if (!depStr.empty())
            f.DEPARTURE_TIME = StringToTimePoint(depStr);
        if (!arrStr.empty())
            f.ARRIVAL_TIME = StringToTimePoint(arrStr);

        // В Excel колонка называется AircraftRegistration,
        // но в классе поле AIRCRAFT_TYPE — туда и пишем регистрационный номер.
        f.AIRCRAFT_TYPE = strAt("AircraftType", r);
        f.STATUS = strAt("Status", r);

        out.push_back(std::move(f));
    }
    return out;
}

// =================== Загрузка информации о местах из БД ======================

// карта: RegistrationNumber -> TotalSeats
static std::map<std::string, int> LoadAircraftTotalSeats()
{
    std::map<std::string, int> res;

    auto st = Db::instance().createStatement();
    std::unique_ptr<sql::ResultSet> rs(
        st->executeQuery(
            "SELECT RegistrationNumber, TotalSeats FROM Aircrafts"));

    while (rs->next()) {
        std::string reg = rs->getString("RegistrationNumber");
        int total = rs->getInt("TotalSeats");
        res[reg] = total;
    }
    return res;
}

// карта: FlightNumber -> занято мест (по таблице passengers)
static std::map<std::string, int> LoadOccupiedSeatsByFlight()
{
    std::map<std::string, int> res;

    auto st = Db::instance().createStatement();
    std::unique_ptr<sql::ResultSet> rs(
        st->executeQuery(
            "SELECT FlightNumber, COUNT(*) AS Cnt "
            "FROM passengers "
            "WHERE SeatNumber IS NOT NULL AND SeatNumber <> '' "
            "GROUP BY FlightNumber"));

    while (rs->next()) {
        std::string fn = rs->getString("FlightNumber");
        int cnt = rs->getInt("Cnt");
        res[fn] = cnt;
    }
    return res;
}

// считает AVAILABLE_SEATS для каждого рейса и проверяет, что мест хватает
// если что-то не так — бросает std::runtime_error (импорт будет отменён)
static void ComputeAvailableSeatsAndValidate(std::vector<Flight>& flights)
{
    auto aircraftSeats = Aircraft::LoadTotalSeatsByRegistration();
    auto occupiedSeats = Passenger::LoadOccupiedSeatsByFlight();

    for (auto& f : flights) {
        auto itAir = aircraftSeats.find(f.AIRCRAFT_TYPE);
        if (itAir == aircraftSeats.end()) {
            throw std::runtime_error(
                "Aircraft with registration '" + f.AIRCRAFT_TYPE +
                "' not found in table 'Aircrafts'.");
        }

        int total = itAir->second;

        int occ = 0;
        auto itOcc = occupiedSeats.find(f.FLIGHTNUMBER);   // ← ИСПОЛЬЗУЕМ occupiedSeats
        if (itOcc != occupiedSeats.end())
            occ = itOcc->second;

        if (occ > total) {
            throw std::runtime_error(
                "Flight " + f.FLIGHTNUMBER +
                ": occupied seats (" + std::to_string(occ) +
                ") are greater than aircraft capacity (" +
                std::to_string(total) + ").");
        }

        f.AVAILABLE_SEATS = total - occ;
    }
}


// ======================== Диалог редактирования рейсов =======================

class FlightsEditDialog : public wxDialog {
public:
    FlightsEditDialog(wxWindow* parent,
        const std::vector<Flight>& data,
        const wxString& title)
        : wxDialog(parent, wxID_ANY, title,
            wxDefaultPosition, wxSize(980, 520),
            wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    {
        auto* top = new wxBoxSizer(wxHORIZONTAL);
        auto* addBtn = new wxButton(this, wxID_ADD, "Add row");
        auto* delBtn = new wxButton(this, wxID_DELETE, "Delete selected");
        top->Add(addBtn, 0, wxRIGHT, 8);
        top->Add(delBtn, 0, wxRIGHT, 8);

        grid_ = new wxGrid(this, wxID_ANY);
        grid_->CreateGrid(0, 7);
        const char* H[] = {
            "FlightNumber",
            "DepartureAirport",
            "ArrivalAirport",
            "DepartureTime",
            "ArrivalTime",
            "AircraftRegistration",
            "Status"
        };
        for (int c = 0; c < 7; ++c) {
            grid_->SetColLabelValue(c, H[c]);
            grid_->SetColSize(c, 130);
        }
        grid_->EnableEditing(true);
        grid_->EnableDragRowSize(false);
        grid_->EnableDragGridSize(false);

        grid_->AppendRows((int)data.size());
        for (int r = 0; r < (int)data.size(); ++r) {
            const auto& f = data[r];
            grid_->SetCellValue(r, 0, f.FLIGHTNUMBER);
            grid_->SetCellValue(r, 1, f.DEPARTURE_AIRPORT);
            grid_->SetCellValue(r, 2, f.ARRIVAL_AIRPORT);
            grid_->SetCellValue(r, 3, TimePointToString(f.DEPARTURE_TIME));
            grid_->SetCellValue(r, 4, TimePointToString(f.ARRIVAL_TIME));
            grid_->SetCellValue(r, 5, f.AIRCRAFT_TYPE); // регистрация
            grid_->SetCellValue(r, 6, f.STATUS);
        }

        auto* btns = new wxStdDialogButtonSizer();
        auto* ok = new wxButton(this, wxID_OK, "OK");
        auto* cancel = new wxButton(this, wxID_CANCEL, "Cancel");
        btns->AddButton(ok);
        btns->AddButton(cancel);
        btns->Realize();

        auto* s = new wxBoxSizer(wxVERTICAL);
        s->Add(top, 0, wxALL, 8);
        s->Add(grid_, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
        s->Add(btns, 0, wxEXPAND | wxALL, 8);
        SetSizerAndFit(s);

        addBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            grid_->AppendRows(1);
            int r = grid_->GetNumberRows() - 1;
            for (int c = 0; c < 7; ++c)
                grid_->SetCellValue(r, c, "");
            grid_->MakeCellVisible(r, 0);
            grid_->SetGridCursor(r, 0);
            });

        delBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            wxArrayInt rows = grid_->GetSelectedRows();
            if (rows.empty()) {
                wxMessageBox("Select at least one row to delete.",
                    "Delete", wxOK | wxICON_INFORMATION, this);
                return;
            }
            for (int i = (int)rows.size() - 1; i >= 0; --i)
                grid_->DeleteRows(rows[i], 1);
            });

        ok->Bind(wxEVT_BUTTON, &FlightsEditDialog::OnOK, this);
    }

    const std::vector<Flight>& Result() const { return result_; }

private:
    wxGrid* grid_{};
    std::vector<Flight> result_;

    void OnOK(wxCommandEvent&)
    {
        result_.clear();
        const int R = grid_->GetNumberRows();

        for (int r = 0; r < R; ++r) {
            bool allEmpty = true;
            for (int c = 0; c < 7; ++c) {
                if (!grid_->GetCellValue(r, c).IsEmpty()) {
                    allEmpty = false; break;
                }
            }
            if (allEmpty)
                continue;

            Flight f;
            f.FLIGHTNUMBER = std::string(grid_->GetCellValue(r, 0).mb_str());
            if (f.FLIGHTNUMBER.empty()) {
                wxMessageBox(wxString::Format("Row %d: FlightNumber is required.", r + 1),
                    "Validation", wxOK | wxICON_WARNING, this);
                result_.clear();
                return;
            }

            f.DEPARTURE_AIRPORT = std::string(grid_->GetCellValue(r, 1).mb_str());
            f.ARRIVAL_AIRPORT = std::string(grid_->GetCellValue(r, 2).mb_str());

            try {
                auto depStr = std::string(grid_->GetCellValue(r, 3).mb_str());
                auto arrStr = std::string(grid_->GetCellValue(r, 4).mb_str());
                if (!depStr.empty())
                    f.DEPARTURE_TIME = StringToTimePoint(depStr);
                if (!arrStr.empty())
                    f.ARRIVAL_TIME = StringToTimePoint(arrStr);
            }
            catch (const std::exception& e) {
                wxMessageBox(wxString::Format("Row %d: %s", r + 1, e.what()),
                    "Validation", wxOK | wxICON_WARNING, this);
                result_.clear();
                return;
            }

            f.AIRCRAFT_TYPE = std::string(grid_->GetCellValue(r, 5).mb_str());
            f.STATUS = std::string(grid_->GetCellValue(r, 6).mb_str());

            result_.push_back(std::move(f));
        }
        EndModal(wxID_OK);
    }
};

// =========================== Главное окно Flight IE ==========================

class FlightIEFrame : public wxFrame {
public:
    FlightIEFrame(wxWindow* p = nullptr)
        : wxFrame(p, wxID_ANY, "Import / Export Flights (.xlsx)",
            wxDefaultPosition, wxSize(520, 240))
    {
        auto* panel = new wxPanel(this);

        auto* colLbl = new wxStaticText(panel, wxID_ANY, "Filter by column:");
        m_col = new wxChoice(panel, wxID_ANY);
        m_col->Append("no filter");
        m_col->Append("FlightNumber");
        m_col->Append("Status");
        m_col->Append("DepartureAirport");
        m_col->Append("ArrivalAirport");
        m_col->Append("AircraftType");
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

        importBtn->Bind(wxEVT_BUTTON, &FlightIEFrame::OnImport, this);
        exportBtn->Bind(wxEVT_BUTTON, &FlightIEFrame::OnExport, this);
    }

private:
    wxChoice* m_col{};
    wxTextCtrl* m_val{};

    // ------------------------ EXPORT ------------------------
    void OnExport(wxCommandEvent&)
    {
        wxFileDialog dlg(this, "Save Excel file", "", "flights.xlsx",
            "Excel files (*.xlsx)|*.xlsx",
            wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg.ShowModal() != wxID_OK)
            return;

        try {
            // простая выборка всех рейсов из БД
            std::vector<Flight> flights;
            {
                auto st = Db::instance().createStatement();
                std::unique_ptr<sql::ResultSet> rs(
                    st->executeQuery(
                        "SELECT FlightNumber, DepartureAirport, ArrivalAirport, "
                        "DepartureTime, ArrivalTime, AircraftType, Status "
                        "FROM flights ORDER BY FlightNumber"));

                while (rs->next()) {
                    Flight f;
                    f.FLIGHTNUMBER = rs->getString("FlightNumber");
                    f.DEPARTURE_AIRPORT = rs->isNull("DepartureAirport") ? "" : rs->getString("DepartureAirport");
                    f.ARRIVAL_AIRPORT = rs->isNull("ArrivalAirport") ? "" : rs->getString("ArrivalAirport");

                    // В БД даты как строки — просто кладём их в TP через StringToTimePoint,
                    // если формат совпадает. Если нет — можно оставить "пустыми".
                    try {
                        if (!rs->isNull("DepartureTime"))
                            f.DEPARTURE_TIME = StringToTimePoint(rs->getString("DepartureTime"));
                        if (!rs->isNull("ArrivalTime"))
                            f.ARRIVAL_TIME = StringToTimePoint(rs->getString("ArrivalTime"));
                    }
                    catch (...) {
                        // если формат неожиданный — просто оставим 0-время
                    }

                    f.AIRCRAFT_TYPE = rs->isNull("AircraftType") ? "" : rs->getString("AircraftType");
                    f.STATUS = rs->isNull("Status") ? "" : rs->getString("Status");

                    flights.push_back(std::move(f));
                }
            }

            WriteFlightsToXlsx(std::string(dlg.GetPath().mb_str()), flights);
            wxMessageBox(wxString::Format("Export completed! Rows: %zu", flights.size()),
                "Export", wxOK | wxICON_INFORMATION, this);
        }
        catch (const std::exception& e) {
            wxMessageBox(wxString::Format("Error: %s", e.what()),
                "Export Error", wxOK | wxICON_ERROR, this);
        }
    }

    // ------------------------ IMPORT ------------------------
    void OnImport(wxCommandEvent&)
    {
        wxFileDialog dlg(this, "Open Excel file", "", "",
            "Excel files (*.xlsx)|*.xlsx",
            wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() != wxID_OK)
            return;

        try {
            auto flights = ReadFlightsFromXlsx(std::string(dlg.GetPath().mb_str()));
            if (flights.empty()) {
                wxMessageBox("File does not contain flights.",
                    "Import", wxOK | wxICON_INFORMATION, this);
                return;
            }

            // превью + возможность отредактировать
            {
                FlightsEditDialog prev(this, flights, "Preview import");
                if (prev.ShowModal() != wxID_OK)
                    return;
                flights = prev.Result();
                if (flights.empty()) {
                    wxMessageBox("There are no rows to import.",
                        "Import", wxOK | wxICON_INFORMATION, this);
                    return;
                }
            }

            // ВАЖНО: до первой записи в БД проверяем все рейсы на совпадение по местам
            ComputeAvailableSeatsAndValidate(flights);

            // Если сюда дошли — все рейсы валидны, AVAILABLE_SEATS посчитаны.
            // Теперь пишем в таблицу flights.
            auto up = Db::instance().prepare(
                "INSERT INTO flights "
                "(FlightNumber, DepartureAirport, ArrivalAirport, "
                " DepartureTime, ArrivalTime, AircraftType, AvailableSeats, Status) "
                "VALUES(?,?,?,?,?,?,?,?) "
                "ON DUPLICATE KEY UPDATE "
                " DepartureAirport=VALUES(DepartureAirport), "
                " ArrivalAirport=VALUES(ArrivalAirport), "
                " DepartureTime=VALUES(DepartureTime), "
                " ArrivalTime=VALUES(ArrivalTime), "
                " AircraftType=VALUES(AircraftType), "
                " AvailableSeats=VALUES(AvailableSeats), "
                " Status=VALUES(Status)"
            );

            for (const auto& f : flights) {
                int i = 1;
                up->setString(i++, f.FLIGHTNUMBER);
                up->setString(i++, f.DEPARTURE_AIRPORT);
                up->setString(i++, f.ARRIVAL_AIRPORT);
                up->setString(i++, TimePointToSql(f.DEPARTURE_TIME));
                up->setString(i++, TimePointToSql(f.ARRIVAL_TIME));
                up->setString(i++, f.AIRCRAFT_TYPE);
                up->setInt(i++, f.AVAILABLE_SEATS);
                up->setString(i++, f.STATUS);
                up->execute();
            }

            wxMessageBox(wxString::Format("Imported %zu flights", flights.size()),
                "Import", wxOK | wxICON_INFORMATION, this);
        }
        catch (const std::exception& e) {
            wxMessageBox(wxString::Format("Import error: %s", e.what()),
                "Import Error", wxOK | wxICON_ERROR, this);
        }
    }
};

void ShowFlightImportExport(wxWindow* parent)
{
    (new FlightIEFrame(parent))->Show();
}
