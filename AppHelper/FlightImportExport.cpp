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

// строго парсим только формат "YYYY-MM-DD HH:MM"
static std::chrono::system_clock::time_point StringToTimePoint(const std::string& s)
{
    if (s.empty())
        return std::chrono::system_clock::time_point{};

    std::tm tm{};
    std::istringstream iss(s);
    iss >> std::get_time(&tm, "%Y-%m-%d %H:%M");
    if (iss.fail())
        throw std::runtime_error(
            "Invalid datetime format: '" + s + "'. Expected YYYY-MM-DD HH:MM");

    std::time_t t = std::mktime(&tm);
    return std::chrono::system_clock::from_time_t(t);
}

// для записи в БД (можно тот же формат)
static std::string TimePointToSql(const std::chrono::system_clock::time_point& tp)
{
    return ErrorHandler::TimePointToString(tp); // достаточно
}


// ====================== XLSX helpers for Flight ==============================

// Фильтрация набора рейсов по Flight::Filter (как у пассажиров)
static std::vector<Flight> FilterFlights(const std::vector<Flight>& all,
    const Flight::Filter& f)
{
    if (f.empty()) return all;

    std::vector<Flight> out;
    out.reserve(all.size());

    auto match = [&](const std::optional<std::string>& opt, const std::string& val) {
        return !opt || (*opt == val);
        };

    for (const auto& fl : all) {
        if (match(f.FlightNumber, fl.FLIGHTNUMBER) &&
            match(f.DepartureAirport, fl.DEPARTURE_AIRPORT) &&
            match(f.ArrivalAirport, fl.ARRIVAL_AIRPORT) &&
            match(f.Status, fl.STATUS)) {
            out.push_back(fl);
        }
    }
    return out;
}


// В Excel храним ТОЛЬКО данные, которые пользователь заполняет руками.
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
        ws.cell(4, row).value(ErrorHandler::TimePointToString(f.DEPARTURE_TIME));
        ws.cell(5, row).value(ErrorHandler::TimePointToString(f.ARRIVAL_TIME));
        ws.cell(6, row).value(f.AIRCRAFT_TYPE);   // RegistrationNumber
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

// чтение рейсов из Excel (без валидации, просто парсинг)
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

        f.AIRCRAFT_TYPE = strAt("AircraftType", r); // RegistrationNumber
        f.STATUS = strAt("Status", r);

        out.push_back(std::move(f));
    }
    return out;
}

// ======================== Диалог ПРЕДПРОСМОТРА рейсов =======================
// НИЧЕГО менять нельзя: ни редактировать, ни добавлять/удалять, ни выделять.

class FlightsPreviewDialog : public wxDialog {
public:
    FlightsPreviewDialog(wxWindow* parent,
        const std::vector<Flight>& data,
        const wxString& title)
        : wxDialog(parent, wxID_ANY, title,
            wxDefaultPosition, wxSize(980, 520),
            wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        flights_(data)
    {
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

        // делаем таблицу полностью read-only и без выделения
        grid_->EnableEditing(false);
        grid_->SetSelectionMode(wxGrid::wxGridSelectNone);
        grid_->EnableDragRowSize(false);
        grid_->EnableDragGridSize(false);

        grid_->AppendRows((int)flights_.size());
        for (int r = 0; r < (int)flights_.size(); ++r) {
            const auto& f = flights_[r];
            grid_->SetCellValue(r, 0, f.FLIGHTNUMBER);
            grid_->SetCellValue(r, 1, f.DEPARTURE_AIRPORT);
            grid_->SetCellValue(r, 2, f.ARRIVAL_AIRPORT);
            grid_->SetCellValue(r, 3, ErrorHandler::TimePointToString(f.DEPARTURE_TIME));
            grid_->SetCellValue(r, 4, ErrorHandler::TimePointToString(f.ARRIVAL_TIME));
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
        s->Add(grid_, 1, wxEXPAND | wxALL, 8);
        s->Add(btns, 0, wxEXPAND | wxALL, 8);
        SetSizerAndFit(s);

        ok->Bind(wxEVT_BUTTON, &FlightsPreviewDialog::OnOK, this);
        cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });
    }

    const std::vector<Flight>& Result() const { return flights_; }

private:
    wxGrid* grid_{};
    std::vector<Flight> flights_;   // данные не меняются

    void OnOK(wxCommandEvent&)
    {
        // Ничего не пересчитываем, ничего не читаем из грида — он read-only.
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

    Flight::Filter MakeFilter()
    {
        Flight::Filter f;
        int sel = m_col->GetSelection();
        wxString v = m_val->GetValue();

        // 0 — "no filter"
        if (sel <= 0 || v.IsEmpty())
            return f;

        std::string s = std::string(v.mb_str());
        wxString name = m_col->GetString(sel);

        if (name == "FlightNumber")      f.FlightNumber = s;
        else if (name == "DepartureAirport")  f.DepartureAirport = s;
        else if (name == "ArrivalAirport")    f.ArrivalAirport = s;
        else if (name == "AircraftType")      f.AircraftType = s;
        else if (name == "Status") f.Status = s;

        return f;
    }

    // ------------------------ EXPORT (БЕЗ проверок) ------------------------
    void OnExport(wxCommandEvent&)
    {
        Flight::Filter f = MakeFilter();

        wxFileDialog dlg(this, "Save Excel file", "", "flights.xlsx",
            "Excel files (*.xlsx)|*.xlsx",
            wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg.ShowModal() != wxID_OK)
            return;

        try {
            auto flights = Db::instance().QueryFlights(f);
            if (flights.empty()) {
                // проверяем, пустая ли вообще таблица
                auto all = Db::instance().QueryFlights(Flight::Filter{});
                if (all.empty()) {
                    wxMessageBox(
                        "The database returned 0 rows with no filter applied.\n\n"
                        "Please check:\n"
                        "• The correct schema is set in Db::init(...)\n"
                        "• The 'Flights' table is not empty",
                        "Nothing to export", wxOK | wxICON_INFORMATION, this);
                    return;
                }

                wxMessageBox(
                    "No records found for the specified filter.\n"
                    "Try clearing the 'Value' field or selecting 'no filter'.",
                    "Nothing matched", wxOK | wxICON_INFORMATION, this);
                return;
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


    // ------------------------ IMPORT (С проверками из классов) --------------
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

            // 1) Валидация по ТЗ (FlightNumber, аэропорты, время, AircraftType)
            Flight::ValidateFlightsForImport(flights);

            // 2) Расчёт мест + проверка соответствия вместимости самолёта
            Flight::ComputeAvailableSeats(flights);

            // 3) Предпросмотр ТОЛЬКО для просмотра, без правок
            {
                FlightsPreviewDialog prev(this, flights, "Preview import");
                if (prev.ShowModal() != wxID_OK)
                    return;
                // данные не меняются
            }

            // 4) Если всё ок — пишем в БД
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
