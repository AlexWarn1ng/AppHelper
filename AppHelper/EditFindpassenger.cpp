#include "Header.h"
#include <wx/wx.h>
#include <wx/choice.h>
#include <wx/grid.h>
#include <wx/sizer.h>
#include <ctime>
#include <map>

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

class EditFindPassengerFrame : public wxFrame {
public:
    EditFindPassengerFrame(wxWindow* parent = nullptr)
        : wxFrame(parent, wxID_ANY, "Find & Edit Passengers", wxDefaultPosition, wxSize(1120, 640))
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
        const char* H[] = { "PassengerID","FullName","DocNumber","FlightNumber","Baggage","Food","SeatNumber","PhoneNumber","Email" };
        for (int c = 0; c < 9; ++c) {
            grid_->SetColLabelValue(c, H[c]);
            grid_->SetColSize(c, c < 2 ? 160 : 130);
        }
        grid_->SetColFormatBool(4);
        grid_->SetColFormatBool(5);
        grid_->EnableEditing(true);
        grid_->EnableDragRowSize(false);
        grid_->EnableDragGridSize(false);

        auto* findBtn = new wxButton(panel, wxID_FIND, "Find");
        auto* clearBtn = new wxButton(panel, wxID_CLEAR, "Clear filter");
        saveBtn_ = new wxButton(panel, wxID_SAVE, "Save changes");
        auto* reloadBtn = new wxButton(panel, wxID_REFRESH, "Reload from DB");
        auto* closeBtn = new wxButton(panel, wxID_CLOSE, "Close");

        saveBtn_->Enable(Session::I().IsEmployee());

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
        btnRow->Add(reloadBtn, 0, wxRIGHT, 6);
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

        findBtn->Bind(wxEVT_BUTTON, &EditFindPassengerFrame::OnFind, this);
        clearBtn->Bind(wxEVT_BUTTON, &EditFindPassengerFrame::OnClear, this);
        saveBtn_->Bind(wxEVT_BUTTON, &EditFindPassengerFrame::OnSave, this);
        reloadBtn->Bind(wxEVT_BUTTON, &EditFindPassengerFrame::OnReload, this);
        closeBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); });

        DoReload();
    }

private:
    wxTextCtrl* pid_{}, * fname_{}, * doc_{}, * flt_{}, * seat_{}, * phone_{}, * email_{};
    wxChoice* baggage_{}, * food_{};
    wxGrid* grid_{};
    wxButton* saveBtn_{};
    std::vector<Passenger> snapshot_;

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
        for (int r = 0; r < R; ++r) {
            bool allEmpty = true;
            for (int c = 0; c < 9; ++c)
                if (!g->GetCellValue(r, c).IsEmpty()) { allEmpty = false; break; }
            if (allEmpty) continue;

            Passenger p;
            p.PASSENGERID = std::string(g->GetCellValue(r, 0).mb_str());
            if (p.PASSENGERID.empty()) continue;
            p.FULLNAME = std::string(g->GetCellValue(r, 1).mb_str());
            p.DOCNUMBER = std::string(g->GetCellValue(r, 2).mb_str());
            p.FLIGHTNUMBER = std::string(g->GetCellValue(r, 3).mb_str());
            auto toBool = [](const wxString& s) {
                wxString v = s.Lower().Strip(wxString::both);
                return (v == "1" || v == "true" || v == "yes" || v == "y" || v == "on");
                };
            p.BAGGAGE = toBool(g->GetCellValue(r, 4));
            p.FOOD = toBool(g->GetCellValue(r, 5));
            p.SEATNUMBER = std::string(g->GetCellValue(r, 6).mb_str());
            p.PHONENUMBER = std::string(g->GetCellValue(r, 7).mb_str());
            p.EMAIL = std::string(g->GetCellValue(r, 8).mb_str());
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

    void OnFind(wxCommandEvent&) {
        try {
            auto f = BuildFilter();
            auto rows = Db::instance().QueryPassengers(f);
            snapshot_ = rows;
            FillGrid(rows);
            wxMessageBox(wxString::Format("Found %zu row(s). You can now edit cells and press 'Save changes'.", rows.size()),
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
        auto edited = CollectFromGrid(grid_);
        if (edited.empty()) {
            wxMessageBox("There are no valid rows to save.",
                "Save", wxOK | wxICON_INFORMATION, this);
            return;
        }
        Employee emp(Session::I().userId, Session::I().position);
        auto audit = emp.PreparePassengerEditAudit(snapshot_, edited);
        int rc = wxMessageBox(audit.ToString() + "\n\nApply changes to the database?",
            "Confirm changes", wxYES_NO | wxICON_QUESTION, this);
        if (rc != wxYES) return;
        try {
            Db::instance().InsertOrUpdatePassengers(edited);
            snapshot_ = edited;
            wxMessageBox("Changes saved.\n\n" + audit.ToString(),
                "Save", wxOK | wxICON_INFORMATION, this);
        }
        catch (const std::exception& e) {
            wxMessageBox(wxString::Format("Error: %s", e.what()),
                "Save Error", wxOK | wxICON_ERROR, this);
        }
    }
};

void ShowEditFindPassenger(wxWindow* parent)
{
    (new EditFindPassengerFrame(parent))->Show();
}
