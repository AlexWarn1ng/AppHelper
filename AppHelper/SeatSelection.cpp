// SeatSelection.cpp
#include "Header.h"
#include "SeatSelection.h"

#include <wx/wx.h>
#include <wx/grid.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>

#include <set>
#include <vector>
#include <memory>
#include <cctype>
#include <cstdio>
#include <algorithm>

// ------------------------------------------------------
// Вспомогательные функции для загрузки данных из БД
// ------------------------------------------------------

// Находим рейс в таблице Flights (через уже существующий Db::QueryFlights)
static Flight LoadFlightByNumber(const std::string& flightNumber)
{
    auto flights = Db::instance().QueryFlights();
    for (const auto& f : flights) {
        if (f.FLIGHTNUMBER == flightNumber)
            return f;
    }
    throw AppException("Flight '" + flightNumber + "' not found in Flights table.");
}

// Читаем запись о самолёте из таблицы Aircrafts по RegistrationNumber
static Aircraft LoadAircraftByRegistration(const std::string& reg)
{
    Aircraft ac{};

    auto ps = Db::instance().prepare(
        "SELECT RegistrationNumber, Model, TotalSeats, "
        "EconomySeats, BusinessSeats, FirstClassSeats, Status "
        "FROM Aircrafts WHERE RegistrationNumber = ?"
    );
    ps->setString(1, reg);
    std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());

    if (!rs->next()) {
        throw AppException("Aircraft '" + reg + "' not found in Aircrafts table.");
    }

    ac.REGISTRATION_NUMBER = rs->getString("RegistrationNumber");
    ac.MODEL = rs->getString("Model");
    ac.TOTAL_SEATS = rs->getInt("TotalSeats");
    ac.ECONOMY_SEATS = rs->getInt("EconomySeats");
    ac.BUSINESS_SEATS = rs->getInt("BusinessSeats");
    ac.FIRST_CLASS_SEATS = rs->getInt("FirstClassSeats");
    ac.STATUS = rs->getString("Status");

    return ac;
}

// Загружаем ЗАНЯТЫЕ места для данного рейса
static std::set<std::string> LoadOccupiedSeats(const std::string& flightNumber)
{
    Passenger::Filter filter;
    filter.FlightNumber = flightNumber;

    auto passengers = Db::instance().QueryPassengers(filter);
    std::set<std::string> occupied;

    for (const auto& p : passengers) {
        if (p.SEATNUMBER.empty())
            continue;

        std::string seat = p.SEATNUMBER;
        for (char& ch : seat)
            ch = (char)std::toupper((unsigned char)ch);

        occupied.insert(seat);
    }

    return occupied;
}

// ------------------------------------------------------
// Класс места по индексу
// ------------------------------------------------------

enum class SeatClassType { First, Business, Economy };

static SeatClassType SeatClassForIndex(int idx, const Aircraft& ac)
{
    int first = std::max(0, ac.FIRST_CLASS_SEATS);
    int business = std::max(0, ac.BUSINESS_SEATS);

    if (idx < first) return SeatClassType::First;
    if (idx < first + business) return SeatClassType::Business;
    return SeatClassType::Economy;
}

// ------------------------------------------------------
// Диалог выбора места
// ------------------------------------------------------

class SeatSelectionDialog : public wxDialog {
public:
    SeatSelectionDialog(wxWindow* parent,
        const Flight& flight,
        const Aircraft& aircraft,
        const std::set<std::string>& occupied,
        const wxString& currentSeat)
        : wxDialog(parent, wxID_ANY, "Select a seat",
            wxDefaultPosition, wxSize(500, 750)),
        flight_(flight),
        aircraft_(aircraft),
        occupied_(occupied)
    {
        // Кол-во мест по классам
        int firstSeats = std::max(0, aircraft_.FIRST_CLASS_SEATS);
        int businessSeats = std::max(0, aircraft_.BUSINESS_SEATS);
        int economySeats = std::max(0, aircraft_.ECONOMY_SEATS);

        // если в БД не заполнены поля классов – считаем всё экономом
        if (firstSeats + businessSeats + economySeats == 0 &&
            aircraft_.TOTAL_SEATS > 0)
        {
            economySeats = aircraft_.TOTAL_SEATS;
        }

        totalSeats_ = firstSeats + businessSeats + economySeats;
        if (totalSeats_ <= 0)
            totalSeats_ = 1;

        // 6 логических мест в ряду (A..F), визуально ***-*** (7 столбцов, col=3 – проход)
        cols_ = 7;
        seatsPerRow_ = 6;

        int rowsFirst = (firstSeats + 2 - 1) / 2; // *-----* -> 2 места в ряду
        int rowsBusiness = (businessSeats + 4 - 1) / 4; // *-*-*-* -> 4 места в ряду
        int rowsEconomy = (economySeats + 6 - 1) / 6; // ***-*** -> 6 мест в ряду

        rows_ = 0;
        if (firstSeats > 0) {
            rows_ += rowsFirst;
        }
        if (businessSeats > 0) {
            if (rows_ > 0) rows_ += 1;   // разделитель между классами
            rows_ += rowsBusiness;
        }
        if (economySeats > 0) {
            if (rows_ > 0) rows_ += 1;   // разделитель между классами
            rows_ += rowsEconomy;
        }
        if (rows_ <= 0) rows_ = 1;

        // массивы с информацией по ячейкам
        hasSeat_.assign(rows_ * cols_, false);
        seatCode_.assign(rows_ * cols_, std::string());

        auto* root = new wxBoxSizer(wxVERTICAL);

        // Инфо по рейсу и борту
        wxString info = wxString::Format(
            "Flight: %s   Aircraft: %s (%s)\n"
            "Total seats: %d   First: %d   Business: %d   Economy: %d",
            wxString::FromUTF8(flight_.FLIGHTNUMBER),
            wxString::FromUTF8(aircraft_.REGISTRATION_NUMBER),
            wxString::FromUTF8(aircraft_.MODEL),
            aircraft_.TOTAL_SEATS,
            aircraft_.FIRST_CLASS_SEATS,
            aircraft_.BUSINESS_SEATS,
            aircraft_.ECONOMY_SEATS
        );
        auto* lblInfo = new wxStaticText(this, wxID_ANY, info);
        root->Add(lblInfo, 0, wxALL | wxEXPAND, 5);

        // "Фюзеляж"
        auto* fuselageBox = new wxStaticBox(this, wxID_ANY, "Cabin");
        auto* fuselageSizer = new wxStaticBoxSizer(fuselageBox, wxVERTICAL);

        grid_ = new wxGrid(fuselageBox, wxID_ANY);
        grid_->CreateGrid(rows_, cols_);

        // Прячем разметку сетки
        grid_->HideRowLabels();
        grid_->HideColLabels();
        grid_->EnableGridLines(false);

        for (int c = 0; c < cols_; ++c) {
            if (c == 3)
                grid_->SetColSize(c, 30);  // проход
            else
                grid_->SetColSize(c, 42);
        }
        for (int r = 0; r < rows_; ++r) {
            grid_->SetRowSize(r, 28);
        }

        grid_->EnableEditing(false);
        grid_->DisableDragColSize();
        grid_->DisableDragRowSize();
        grid_->DisableDragGridSize();
        grid_->SetSelectionMode(wxGrid::wxGridSelectCells);

        // Цвета
        wxColour colFirst(255, 180, 180);    // красный/розовый
        wxColour colBusiness(255, 220, 180); // оранжевый
        wxColour colEconomy(200, 255, 200);  // зелёный
        wxColour colOccupied(160, 160, 160); // серый
        wxColour colEmpty(220, 220, 220);    // пусто
        wxColour colAisle(210, 210, 210);    // проход

        // начальная заливка
        for (int r = 0; r < rows_; ++r) {
            for (int c = 0; c < cols_; ++c) {
                grid_->SetCellValue(r, c, "");
                grid_->SetCellBackgroundColour(r, c, (c == 3) ? colAisle : colEmpty);
            }
        }

        int gridRow = 0;       // индекс строки в grid
        int seatRowNumber = 1; // номер ряда в кодах (01,02,...)

        auto fillClass = [&](SeatClassType cl,
            int seatCount,
            const std::vector<int>& seatPositions,
            const wxColour& classColor)
            {
                if (seatCount <= 0)
                    return;

                // визуальный пробел перед блоком (если это не самый первый)
                if (gridRow > 0) {
                    // строка-разделитель уже залита colAisle/colEmpty
                    ++gridRow;
                }

                const char lettersAll[] = "ABCDEF";

                int remaining = seatCount;
                while (remaining > 0 && gridRow < rows_) {

                    for (size_t i = 0; i < seatPositions.size() && remaining > 0; ++i) {
                        int seatPos = seatPositions[i];   // 0..5 (A..F)
                        int col = (seatPos < 3) ? seatPos : (seatPos + 1); // с учётом прохода
                        if (col < 0 || col >= cols_) continue;

                        int r = gridRow;

                        char letter = lettersAll[seatPos];
                        char buf[4];
                        std::snprintf(buf, sizeof(buf), "%02d%c", seatRowNumber, letter);
                        std::string code(buf);

                        int idxCell = Index(r, col);
                        hasSeat_[idxCell] = true;
                        seatCode_[idxCell] = code;

                        grid_->SetCellValue(r, col, wxString::FromUTF8(buf));

                        bool isOcc = (occupied_.find(code) != occupied_.end());
                        if (isOcc) {
                            grid_->SetCellBackgroundColour(r, col, colOccupied);
                        }
                        else {
                            grid_->SetCellBackgroundColour(r, col, classColor);
                        }

                        --remaining;
                    }

                    ++gridRow;
                    ++seatRowNumber;
                }
            };

        // схемы по классам:
        // First:    *-----*  -> [0,5]
        // Business: *-*-*-*  -> [0,2,3,5]
        // Economy:  ***-***  -> [0,1,2,3,4,5]
        fillClass(SeatClassType::First,
            firstSeats,
            { 0, 5 },
            colFirst);

        fillClass(SeatClassType::Business,
            businessSeats,
            { 0, 2, 3, 5 },
            colBusiness);

        fillClass(SeatClassType::Economy,
            economySeats,
            { 0, 1, 2, 3, 4, 5 },
            colEconomy);

        // Предвыбор текущего места (если оно существует и свободно)
        wxString cur = currentSeat;
        cur.Strip(wxString::both);
        if (!cur.IsEmpty()) {
            cur.MakeUpper();
            for (int r = 0; r < rows_; ++r) {
                for (int c = 0; c < cols_; ++c) {
                    if (!IsExistingSeatCell(r, c))
                        continue;

                    wxString cell = grid_->GetCellValue(r, c);
                    if (cell.CmpNoCase(cur) == 0 &&
                        occupied_.find(std::string(cur.mb_str())) == occupied_.end())
                    {
                        grid_->SetGridCursor(r, c);
                        grid_->SelectBlock(r, c, r, c);
                        selectedSeat_ = cell;
                        break;
                    }
                }
            }
        }

        grid_->Bind(wxEVT_GRID_CELL_LEFT_CLICK,
            &SeatSelectionDialog::OnCellClick, this);
        grid_->Bind(wxEVT_GRID_CELL_LEFT_DCLICK,
            &SeatSelectionDialog::OnCellDClick, this);

        fuselageSizer->Add(grid_, 1, wxALL | wxEXPAND, 8);
        root->Add(fuselageSizer, 1, wxALL | wxEXPAND, 5);

        // Кнопки OK / Cancel
        auto* btnSizer = new wxBoxSizer(wxHORIZONTAL);
        auto* okBtn = new wxButton(this, wxID_OK, "OK");
        auto* cancelBtn = new wxButton(this, wxID_CANCEL, "Cancel");
        btnSizer->AddStretchSpacer(1);
        btnSizer->Add(okBtn, 0, wxRIGHT, 5);
        btnSizer->Add(cancelBtn, 0);

        root->Add(btnSizer, 0, wxALL | wxEXPAND, 5);

        Bind(wxEVT_BUTTON, &SeatSelectionDialog::OnOk, this, wxID_OK);

        SetSizerAndFit(root);
        CentreOnParent();
    }

    wxString GetSelectedSeat() const { return selectedSeat_; }

private:
    wxGrid* grid_{};
    Flight   flight_;
    Aircraft aircraft_;
    std::set<std::string> occupied_;
    int totalSeats_{};
    int rows_{};
    int cols_{};
    int seatsPerRow_{};

    wxString selectedSeat_;

    // карта ячеек
    std::vector<bool>        hasSeat_;
    std::vector<std::string> seatCode_;

    int Index(int r, int c) const { return r * cols_ + c; }

    bool IsExistingSeatCell(int row, int col) const
    {
        if (row < 0 || row >= rows_ || col < 0 || col >= cols_)
            return false;
        return hasSeat_[Index(row, col)];
    }

    std::string SeatCodeForCell(int row, int col) const
    {
        int idx = Index(row, col);
        if (idx < 0 || idx >= (int)seatCode_.size())
            return std::string();
        return seatCode_[idx];
    }

    void OnCellClick(wxGridEvent& e)
    {
        int r = e.GetRow();
        int c = e.GetCol();

        if (!IsExistingSeatCell(r, c)) {
            e.Skip();
            return;
        }

        std::string seatCode = SeatCodeForCell(r, c);
        if (seatCode.empty()) {
            e.Skip();
            return;
        }
        if (occupied_.find(seatCode) != occupied_.end()) {
            wxBell();
            return;
        }

        grid_->ClearSelection();
        grid_->SetGridCursor(r, c);
        grid_->SelectBlock(r, c, r, c);
        selectedSeat_ = grid_->GetCellValue(r, c);
    }

    void OnCellDClick(wxGridEvent& e)
    {
        OnCellClick(e);
        if (!selectedSeat_.IsEmpty())
            EndModal(wxID_OK);
    }

    void OnOk(wxCommandEvent& e)
    {
        if (selectedSeat_.IsEmpty()) {
            wxMessageBox("Please select a seat.",
                "Choose seat",
                wxOK | wxICON_WARNING,
                this);
            return;
        }
        EndModal(wxID_OK);
    }
};

// ------------------------------------------------------
// Публичная функция, которую вызывает AddPassengerFrame
// ------------------------------------------------------

bool ChooseSeatForFlight(wxWindow* parent,
    const std::string& flightNumber,
    const wxString& currentSeat,
    wxString& outSeat)
{
    outSeat.clear();
    std::string err;

    bool ok = ErrorHandler::Try([&] {

        if (!Flight::IsValidFlightNumberFormat(flightNumber)) {
            throw AppException("Invalid FlightNumber format: " + flightNumber);
        }

        Flight   flight = LoadFlightByNumber(flightNumber);
        Aircraft aircraft = LoadAircraftByRegistration(flight.AIRCRAFT_TYPE);
        auto occupied = LoadOccupiedSeats(flightNumber);

        SeatSelectionDialog dlg(parent, flight, aircraft, occupied, currentSeat);
        if (dlg.ShowModal() == wxID_OK) {
            outSeat = dlg.GetSelectedSeat();
        }

        }, err);

    if (!ok) {
        wxMessageBox(wxString::FromUTF8(err.c_str()),
            "Choose seat",
            wxOK | wxICON_ERROR,
            parent);
        return false;
    }

    return !outSeat.IsEmpty();
}

