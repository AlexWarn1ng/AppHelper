#include "Header.h"
#include <wx/wx.h>
#include <wx/choice.h>
#include <wx/grid.h>
#include <wx/sizer.h>
#include <wx/utils.h>
#include <ctime>
#include <map>
#include <set> 
#include <wx/filedlg.h>   // v principe ne nado no bez nego ne rabotaet
#include <algorithm>


static std::string MakeAuditId() {
    std::time_t t = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "AUD-%Y%m%d-%H%M%S", std::localtime(&t));
    return std::string(buf);
}

Employee::PassengerEditAudit Employee::PreparePassengerEditAudit(
    const std::vector<Passenger>& before,
    const std::vector<Passenger>& after) const
{
    PassengerEditAudit a;
    a.auditId = MakeAuditId();
    a.employeeId = id_;
    a.position = position_;
    a.rowsBefore = before.size();
    a.rowsAfter = after.size();

    std::map<std::string, Passenger> mapBefore, mapAfter;
    for (const auto& p : before) mapBefore[p.PASSENGERID] = p;
    for (const auto& p : after)  mapAfter[p.PASSENGERID] = p;

    for (auto& kv : mapAfter) if (!mapBefore.count(kv.first)) ++a.rowsAdded;
    for (auto& kv : mapBefore) if (!mapAfter.count(kv.first)) ++a.rowsDeleted;

    auto same = [](const Passenger& x, const Passenger& y)->bool {
        return x.FULLNAME == y.FULLNAME
            && x.DOCNUMBER == y.DOCNUMBER
            && x.FLIGHTNUMBER == y.FLIGHTNUMBER
            && x.BAGGAGE == y.BAGGAGE
            && x.FOOD == y.FOOD
            && x.SEATNUMBER == y.SEATNUMBER
            && x.PHONENUMBER == y.PHONENUMBER
            && x.EMAIL == y.EMAIL;
        };
    for (auto& kv : mapAfter) {
        auto it = mapBefore.find(kv.first);
        if (it != mapBefore.end() && !same(it->second, kv.second)) ++a.rowsUpdated;
    }
    return a;
}

// okno osnovnoje

class EditFindPassengerFrame : public wxFrame {
public:
    EditFindPassengerFrame(wxWindow* parent = nullptr)
        : wxFrame(parent, wxID_ANY, "Find & Edit Passengers", wxDefaultPosition, wxSize(1120, 640))
        , pdfPrinter_("C:\\Windows\\Fonts\\arial.ttf")
    {
        auto* panel = new wxPanel(this);

        pid_ = new wxTextCtrl(panel, wxID_ANY);
        fname_ = new wxTextCtrl(panel, wxID_ANY);
        doc_ = new wxTextCtrl(panel, wxID_ANY);
        flt_ = new wxTextCtrl(panel, wxID_ANY);
        seat_ = new wxTextCtrl(panel, wxID_ANY);
        phone_ = new wxTextCtrl(panel, wxID_ANY);
        email_ = new wxTextCtrl(panel, wxID_ANY);

        baggage_ = new wxChoice(panel, wxID_ANY);
        baggage_->Append("any");
        baggage_->Append("0");
        baggage_->Append("1");
        baggage_->SetSelection(0);

        food_ = new wxChoice(panel, wxID_ANY);
        food_->Append("any");
        food_->Append("0");
        food_->Append("1");
        food_->SetSelection(0);

        grid_ = new wxGrid(panel, wxID_ANY);
        grid_->CreateGrid(0, 9);
        const char* H[] = {
            "PassengerID","FullName","DocNumber",
            "FlightNumber","Baggage","Food",
            "SeatNumber","PhoneNumber","Email"
        };
        for (int c = 0; c < 9; ++c) {
            grid_->SetColLabelValue(c, H[c]);
            grid_->SetColSize(c, c < 2 ? 160 : 130);
        }
        grid_->SetColFormatBool(4);
        grid_->SetColFormatBool(5);
        grid_->EnableEditing(true);
        grid_->EnableDragRowSize(false);
        grid_->EnableDragGridSize(false);

        {
            auto* attr = new wxGridCellAttr();
            attr->SetReadOnly(true);
            attr->SetBackgroundColour(*wxLIGHT_GREY);
            grid_->SetColAttr(0, attr);
        }

        auto* findBtn = new wxButton(panel, wxID_FIND, "Find");
        auto* clearBtn = new wxButton(panel, wxID_CLEAR, "Clear filter");
        saveBtn_ = new wxButton(panel, wxID_SAVE, "Save changes");
        deleteBtn_ = new wxButton(panel, wxID_DELETE, "Delete selected");
        auto* reloadBtn = new wxButton(panel, wxID_REFRESH, "Reload from DB");
        printBtn_ = new wxButton(panel, wxID_ANY, "Print ticket");
        auto* closeBtn = new wxButton(panel, wxID_CLOSE, "Close");

        saveBtn_->Enable(Session::I().IsEmployee());
        deleteBtn_->Enable(Session::I().IsEmployee());

        auto* fg = new wxFlexGridSizer(3, 10, 6, 6);
        fg->AddGrowableCol(1, 1);
        fg->AddGrowableCol(3, 1);
        fg->AddGrowableCol(5, 1);
        fg->AddGrowableCol(7, 1);
        fg->AddGrowableCol(9, 1);

        auto L = [&](const char* t) { return new wxStaticText(panel, wxID_ANY, t); };

        fg->Add(L("PassengerID"));    fg->Add(pid_, 1, wxEXPAND);
        fg->Add(L("FullName"));       fg->Add(fname_, 1, wxEXPAND);
        fg->Add(L("DocNumber"));      fg->Add(doc_, 1, wxEXPAND);
        fg->Add(L("FlightNumber"));   fg->Add(flt_, 1, wxEXPAND);
        fg->Add(L("SeatNumber"));     fg->Add(seat_, 1, wxEXPAND);
        fg->Add(L("PhoneNumber"));    fg->Add(phone_, 1, wxEXPAND);
        fg->Add(L("Email"));          fg->Add(email_, 1, wxEXPAND);
        fg->Add(L("Baggage (0/1)"));  fg->Add(baggage_, 1, wxEXPAND);
        fg->Add(L("Food (0/1)"));     fg->Add(food_, 1, wxEXPAND);

        auto* btnRow = new wxBoxSizer(wxHORIZONTAL);
        btnRow->Add(findBtn, 0, wxRIGHT, 6);
        btnRow->Add(clearBtn, 0, wxRIGHT, 6);
        btnRow->Add(saveBtn_, 0, wxRIGHT, 6);
        btnRow->Add(deleteBtn_, 0, wxRIGHT, 6);
        btnRow->Add(reloadBtn, 0, wxRIGHT, 6);
        btnRow->Add(printBtn_, 0, wxRIGHT, 6);
        btnRow->AddStretchSpacer(1);
        btnRow->Add(closeBtn, 0);

        auto* root = new wxBoxSizer(wxVERTICAL);
        root->AddSpacer(6);
        root->Add(fg, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
        root->AddSpacer(6);
        root->Add(btnRow, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
        root->AddSpacer(6);
        root->Add(grid_, 1, wxEXPAND | wxALL, 8);

        panel->SetSizerAndFit(root);

        // биндим кнопки
        findBtn->Bind(wxEVT_BUTTON, &EditFindPassengerFrame::OnFind, this);
        clearBtn->Bind(wxEVT_BUTTON, &EditFindPassengerFrame::OnClear, this);
        saveBtn_->Bind(wxEVT_BUTTON, &EditFindPassengerFrame::OnSave, this);
        deleteBtn_->Bind(wxEVT_BUTTON, &EditFindPassengerFrame::OnDelete, this);
        reloadBtn->Bind(wxEVT_BUTTON, &EditFindPassengerFrame::OnReload, this);
        printBtn_->Bind(wxEVT_BUTTON, &EditFindPassengerFrame::OnPrintTicket, this);
        closeBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); });

        DoReload();
    }

private:
    wxTextCtrl* pid_{}, * fname_{}, * doc_{}, * flt_{}, * seat_{}, * phone_{}, * email_{};
    wxChoice* baggage_{}, * food_{};
    wxGrid* grid_{};
    wxButton* saveBtn_{};
    wxButton* deleteBtn_{};
    wxButton* printBtn_{};
    PDFPrinter  pdfPrinter_;
    std::vector<Passenger> snapshot_;

    // vsiakoje govno
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
        if (dotPos <= atPos + 1) return false;          // точка сразу после @ — плохо
        if (dotPos >= (int)e.length() - 1) return false; // точка последняя — плохо

        return true;
    }

    Passenger::Filter BuildFilter() const {
        Passenger::Filter f;
        auto setS = [&](wxTextCtrl* e, std::optional<std::string>& dst) {
            wxString v = e->GetValue().Strip(wxString::both);
            if (!v.IsEmpty()) dst = std::string(v.mb_str());
            };
        auto setI = [&](wxChoice* ch, std::optional<int>& dst) {
            int sel = ch->GetSelection();
            if (sel == 1) dst = 0;
            else if (sel == 2) dst = 1;
            };
        setS(pid_, f.PassengerID);
        setS(fname_, f.FullName);
        setS(doc_, f.DocNumber);
        setS(flt_, f.FlightNumber);
        setS(seat_, f.SeatNumber);
        setS(phone_, f.PhoneNumber);
        setS(email_, f.Email);
        setI(baggage_, f.Baggage);
        setI(food_, f.Food);
        return f;
    }

    static wxString BoolToCell(bool b) { return b ? "1" : "0"; }

    static std::vector<Passenger> CollectFromGrid(wxGrid* g) {
        std::vector<Passenger> out;
        int R = g->GetNumberRows();
        if (R == 0) return out;

        // Подгружаем список допустимых FlightNumber из таблицы flights
        std::set<std::string> validFlights;
        {
            auto flights = Db::instance().QueryFlights();
            for (const auto& f : flights) {
                validFlights.insert(f.FLIGHTNUMBER);
            }
        }

        auto toBool = [](const wxString& s) {
            wxString v = s.Lower().Strip(wxString::both);
            return (v == "1" || v == "true" || v == "yes" || v == "y" || v == "on");
            };

        for (int r = 0; r < R; ++r) {
            int rowNo = r + 1;

            // Проверяем, не пустая ли вообще строка
            bool allEmpty = true;
            for (int c = 0; c < 9; ++c) {
                if (!g->GetCellValue(r, c).Strip(wxString::both).IsEmpty()) {
                    allEmpty = false;
                    break;
                }
            }
            if (allEmpty) {
                // полностью пустые строки пропускаем
                continue;
            }

            Passenger p;

            // 1) PassengerID — обязателен
            wxString pid = g->GetCellValue(r, 0).Strip(wxString::both);
            if (pid.IsEmpty()) {
                throw AppException(
                    wxString::Format("Row %d: PassengerID must not be empty.", rowNo).ToStdString()
                );
            }
            p.PASSENGERID = std::string(pid.mb_str());

            // 2) FullName — только буквы и пробелы, каждое слово с заглавной
            wxString full = g->GetCellValue(r, 1).Strip(wxString::both);
            if (full.IsEmpty()) {
                throw AppException(
                    wxString::Format("Row %d: FullName is required.", rowNo).ToStdString()
                );
            }
            if (!IsLettersAndSpaces(full)) {
                throw AppException(
                    wxString::Format("Row %d: FullName must contain only letters and spaces.", rowNo).ToStdString()
                );
            }
            full = NormalizeFullName(full);
            p.FULLNAME = std::string(full.mb_str());

            // 3) DocNumber — только буквы и цифры, без пробелов, верхний регистр
            wxString doc = g->GetCellValue(r, 2).Strip(wxString::both);
            if (doc.IsEmpty()) {
                throw AppException(
                    wxString::Format("Row %d: DocNumber is required.", rowNo).ToStdString()
                );
            }
            for (wxUniChar c : doc) {
                if (!wxIsalnum(c)) {
                    throw AppException(
                        wxString::Format("Row %d: DocNumber must contain only letters and digits, no spaces.", rowNo).ToStdString()
                    );
                }
            }
            doc.MakeUpper();
            p.DOCNUMBER = std::string(doc.mb_str());

            // 4) FlightNumber — должен существовать в таблице flights
            wxString flt = g->GetCellValue(r, 3).Strip(wxString::both);
            if (flt.IsEmpty()) {
                throw AppException(
                    wxString::Format("Row %d: FlightNumber is required.", rowNo).ToStdString()
                );
            }
            std::string fltStd = std::string(flt.mb_str());
            if (!validFlights.count(fltStd)) {
                throw AppException(
                    wxString::Format(
                        "Row %d: FlightNumber '%s' does not exist in flights table.",
                        rowNo, flt
                    ).ToStdString()
                );
            }
            p.FLIGHTNUMBER = fltStd;

            // 5) Baggage / Food — как раньше
            p.BAGGAGE = toBool(g->GetCellValue(r, 4));
            p.FOOD = toBool(g->GetCellValue(r, 5));

            // 6) SeatNumber — 2 цифры + 1 буква
            wxString seat = g->GetCellValue(r, 6).Strip(wxString::both);
            if (!IsValidSeat(seat)) {
                throw AppException(
                    wxString::Format(
                        "Row %d: SeatNumber must have format NNX (two digits and one letter), e.g. 12B.",
                        rowNo
                    ).ToStdString()
                );
            }
            seat.MakeUpper();
            p.SEATNUMBER = std::string(seat.mb_str());

            // 7) PhoneNumber — обязателен, +7XXXXXXXXXX
            wxString phone = g->GetCellValue(r, 7).Strip(wxString::both);
            if (!IsValidPhone(phone)) {
                throw AppException(
                    wxString::Format(
                        "Row %d: PhoneNumber must have format +7XXXXXXXXXX.",
                        rowNo
                    ).ToStdString()
                );
            }
            p.PHONENUMBER = std::string(phone.mb_str());

            // 8) Email — формат user@host.domain
            wxString email = g->GetCellValue(r, 8).Strip(wxString::both);
            if (!IsValidEmail(email)) {
                throw AppException(
                    wxString::Format(
                        "Row %d: Email must have format like user@host.domain.",
                        rowNo
                    ).ToStdString()
                );
            }
            p.EMAIL = std::string(email.mb_str());

            out.push_back(std::move(p));
        }

        return out;
    }


    void FillGrid(const std::vector<Passenger>& rows) {
        grid_->ClearGrid();
        if (grid_->GetNumberRows() > 0) grid_->DeleteRows(0, grid_->GetNumberRows());
        if (grid_->GetNumberCols() != 9) {
            if (grid_->GetNumberCols() < 9) grid_->AppendCols(9 - grid_->GetNumberCols());
            else grid_->DeleteCols(0, grid_->GetNumberCols() - 9);
        }
        grid_->AppendRows((int)rows.size());
        for (int r = 0; r < (int)rows.size(); ++r) {
            const auto& p = rows[r];
            grid_->SetCellValue(r, 0, p.PASSENGERID);
            grid_->SetCellValue(r, 1, p.FULLNAME);
            grid_->SetCellValue(r, 2, p.DOCNUMBER);
            grid_->SetCellValue(r, 3, p.FLIGHTNUMBER);
            grid_->SetCellValue(r, 4, BoolToCell(p.BAGGAGE));
            grid_->SetCellValue(r, 5, BoolToCell(p.FOOD));
            grid_->SetCellValue(r, 6, p.SEATNUMBER);
            grid_->SetCellValue(r, 7, p.PHONENUMBER);
            grid_->SetCellValue(r, 8, p.EMAIL);
        }
    }

    void DoReload() {
        try {
            auto rows = Db::instance().QueryPassengers(Passenger::Filter{});
            snapshot_ = rows;
            FillGrid(rows);
        }
        catch (const std::exception& e) {
            wxMessageBox(wxString::Format("Error: %s", e.what()),
                "Reload Error", wxOK | wxICON_ERROR, this);
        }
    }

    // sama pechat

    Passenger GetPassengerFromRow(int r) const {
        Passenger p;
        p.PASSENGERID = std::string(grid_->GetCellValue(r, 0).mb_str());
        p.FULLNAME = std::string(grid_->GetCellValue(r, 1).mb_str());
        p.DOCNUMBER = std::string(grid_->GetCellValue(r, 2).mb_str());
        p.FLIGHTNUMBER = std::string(grid_->GetCellValue(r, 3).mb_str());

        auto toBool = [](const wxString& s) {
            wxString v = s.Lower().Strip(wxString::both);
            return (v == "1" || v == "true" || v == "yes" || v == "y" || v == "on");
            };

        p.BAGGAGE = toBool(grid_->GetCellValue(r, 4));
        p.FOOD = toBool(grid_->GetCellValue(r, 5));
        p.SEATNUMBER = std::string(grid_->GetCellValue(r, 6).mb_str());
        p.PHONENUMBER = std::string(grid_->GetCellValue(r, 7).mb_str());
        p.EMAIL = std::string(grid_->GetCellValue(r, 8).mb_str());
        return p;
    }

    void OnPrintTicket(wxCommandEvent&)
    {
        std::string err;

        bool ok = ErrorHandler::Try([&] {

            if (grid_->GetNumberRows() == 0) {
                throw AppException("There are no rows in the grid.");
            }

            // определяем выбранную строку
            int row = -1;
            // 1) если выделены целые строки — берём первую
            wxArrayInt selRows = grid_->GetSelectedRows();
            if (!selRows.IsEmpty()) {
                row = selRows[0];
            }
            else {
                // 2) иначе проверяем, выделена ли текущая ячейка
                int curRow = grid_->GetGridCursorRow();
                int curCol = grid_->GetGridCursorCol();
                if (grid_->IsInSelection(curRow, curCol)) {
                    row = curRow;
                }
            }

            // 3) вообще ничего не выделено
            if (row < 0 || row >= grid_->GetNumberRows()) {
                throw AppException("Please select a passenger row first.");
            }

            // 4) проверяем, что у строки есть PassengerID
            wxString pid = grid_->GetCellValue(row, 0).Strip(wxString::both);
            if (pid.IsEmpty()) {
                throw AppException("Selected row has no PassengerID.");
            }

            // ------------ собираем пассажира и печатаем ------------

            Passenger p = GetPassengerFromRow(row);

            wxString defaultName = wxString::Format("ticket_%s.pdf", pid);
            wxFileDialog dlg(this, "Save ticket as...",
                "", defaultName,
                "PDF files (*.pdf)|*.pdf",
                wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
            if (dlg.ShowModal() != wxID_OK) {
                // пользователь передумал — не ошибка
                return;
            }

            wxString path = dlg.GetPath();

            pdfPrinter_.PrintTicketForPassenger(p, std::string(path.mb_str()));

            auto& s = Session::I();
            std::string empId = std::string(s.userId.mb_str());
            std::string empPos = std::string(s.position.mb_str());
            Logger::I().Write(
                "Employee " + empId +
                " (" + empPos + ") printed ticket for passenger " +
                p.PASSENGERID
            );

            wxMessageBox("Ticket PDF has been created.",
                "Print ticket", wxOK | wxICON_INFORMATION, this);
            wxLaunchDefaultApplication(path);

            }, err);

        if (!ok) {
            wxMessageBox(wxString::FromUTF8(err.c_str()),
                "Print ticket", wxOK | wxICON_ERROR, this);
        }
    }

    void OnDelete(wxCommandEvent&)
    {
        if (!Session::I().IsEmployee()) {
            wxMessageBox("You are not allowed to modify passengers.",
                "Access", wxOK | wxICON_WARNING, this);
            return;
        }

        if (grid_->GetNumberRows() == 0) {
            wxMessageBox("There are no rows in the grid.",
                "Delete", wxOK | wxICON_INFORMATION, this);
            return;
        }

        wxArrayInt selRows = grid_->GetSelectedRows();
        if (selRows.IsEmpty()) {
            int curRow = grid_->GetGridCursorRow();
            int curCol = grid_->GetGridCursorCol();
            if (curRow >= 0 && curRow < grid_->GetNumberRows()
                && curCol >= 0 && curCol < grid_->GetNumberCols()
                && grid_->IsInSelection(curRow, curCol)) {
                selRows.Add(curRow);
            }
        }

        if (selRows.IsEmpty()) {
            wxMessageBox("Please select row(s) to delete.",
                "Delete", wxOK | wxICON_INFORMATION, this);
            return;
        }

        std::vector<int> rows;
        rows.reserve(selRows.GetCount());
        for (unsigned i = 0; i < selRows.GetCount(); ++i)
            rows.push_back(selRows[i]);

        std::sort(rows.begin(), rows.end());
        rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

        int rc = wxMessageBox(
            wxString::Format("Delete %zu selected row(s) from the grid?\nChanges will be applied after 'Save changes'.",
                rows.size()),
            "Confirm delete", wxYES_NO | wxICON_QUESTION, this);
        if (rc != wxYES) return;

        for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
            int r = *it;
            if (r >= 0 && r < grid_->GetNumberRows())
                grid_->DeleteRows(r, 1);
        }

        auto& s = Session::I();
        std::string empId = std::string(s.userId.mb_str());
        std::string empPos = std::string(s.position.mb_str());
        Logger::I().Write(
            "Employee " + empId +
            " (" + empPos + ") deleted " +
            std::to_string(rows.size()) +
            " row(s) from passenger grid (pending save)"
        );
    }

    //Find Clear Reload Save 

    void OnFind(wxCommandEvent&) {
        try {
            auto f = BuildFilter();
            auto rows = Db::instance().QueryPassengers(f);
            snapshot_ = rows;
            FillGrid(rows);
            wxMessageBox(wxString::Format(
                "Found %zu row(s). You can now edit cells and press 'Save changes'.",
                rows.size()),
                "Find", wxOK | wxICON_INFORMATION, this);
        }
        catch (const std::exception& e) {
            wxMessageBox(wxString::Format("Error: %s", e.what()),
                "Find Error", wxOK | wxICON_ERROR, this);
        }
    }

    void OnClear(wxCommandEvent&) {
        pid_->Clear(); fname_->Clear(); doc_->Clear(); flt_->Clear();
        seat_->Clear(); phone_->Clear(); email_->Clear();
        baggage_->SetSelection(0); food_->SetSelection(0);
    }

    void OnReload(wxCommandEvent&) { DoReload(); }

    void OnSave(wxCommandEvent&) {
        if (!Session::I().IsEmployee()) {
            wxMessageBox("You are not allowed to modify passengers.",
                "Access", wxOK | wxICON_WARNING, this);
            return;
        }

        std::string err;

        bool ok = ErrorHandler::Try([&] {

            auto edited = CollectFromGrid(grid_);
            if (edited.empty()) {
                throw AppException("There are no valid rows to save.");
            }

            Employee emp(Session::I().userId, Session::I().position);
            auto audit = emp.PreparePassengerEditAudit(snapshot_, edited);

            int rc = wxMessageBox(audit.ToString() + "\n\nApply changes to the database?",
                "Confirm changes", wxYES_NO | wxICON_QUESTION, this);
            if (rc != wxYES) return;

            Db::instance().InsertOrUpdatePassengers(edited);
            snapshot_ = edited;

            auto& s = Session::I();
            std::string empId = std::string(s.userId.mb_str());
            std::string empPos = std::string(s.position.mb_str());
            Logger::I().Write(
                "Employee " + empId +
                " (" + empPos + ") saved passengers: " +
                "before=" + std::to_string(audit.rowsBefore) +
                ", after=" + std::to_string(audit.rowsAfter) +
                ", added=" + std::to_string(audit.rowsAdded) +
                ", updated=" + std::to_string(audit.rowsUpdated) +
                ", deleted=" + std::to_string(audit.rowsDeleted)
            );

            wxMessageBox("Changes saved.\n\n" + audit.ToString(),
                "Save", wxOK | wxICON_INFORMATION, this);

            }, err);

        if (!ok) {
            wxMessageBox(wxString::FromUTF8(err.c_str()),
                "Save Error", wxOK | wxICON_ERROR, this);
        }
    }


};

void ShowEditFindPassenger(wxWindow* parent)
{
    (new EditFindPassengerFrame(parent))->Show();
}
