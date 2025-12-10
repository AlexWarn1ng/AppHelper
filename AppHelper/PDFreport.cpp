#include "Header.h"
#include <wx/wx.h>
#include <wx/datectrl.h>
#include <wx/dateevt.h>

#include "PDFreport.h"

class PdfReportFrame : public wxFrame {
public:
    explicit PdfReportFrame(wxWindow* parent = nullptr)
        : wxFrame(parent,  wxID_ANY, "Passenger PDF report",wxDefaultPosition, wxSize(420, 220))
    {
        auto* panel = new wxPanel(this);
        auto* fromLabel = new wxStaticText(panel, wxID_ANY, "From date:");
        auto* toLabel = new wxStaticText(panel, wxID_ANY, "To date:");
        fromDate_ = new wxDatePickerCtrl( panel, wxID_ANY, wxDefaultDateTime, wxDefaultPosition, wxDefaultSize, wxDP_DROPDOWN | wxDP_SHOWCENTURY
        );
        toDate = new wxDatePickerCtrl(
            panel, wxID_ANY, wxDefaultDateTime,  wxDefaultPosition, wxDefaultSize, wxDP_DROPDOWN | wxDP_SHOWCENTURY
        );
        auto* createBtn = new wxButton(panel, wxID_OK, "Create report");
        auto* closeBtn = new wxButton(panel, wxID_CLOSE, "Close");
        auto* grid = new wxFlexGridSizer(2, 2, 8, 8);
        grid->AddGrowableCol(1, 1);
        grid->Add(fromLabel, 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(fromDate_, 1, wxEXPAND);
        grid->Add(toLabel, 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(toDate, 1, wxEXPAND);
        auto* btnRow = new wxBoxSizer(wxHORIZONTAL);
        btnRow->Add(createBtn, 0, wxRIGHT, 8);
        btnRow->AddStretchSpacer(1);
        btnRow->Add(closeBtn, 0);
        auto* root = new wxBoxSizer(wxVERTICAL);
        root->AddSpacer(10);
        root->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
        root->AddSpacer(15);
        root->Add(btnRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
        panel->SetSizer(root);
        CentreOnScreen();
        createBtn->Bind(wxEVT_BUTTON, &PdfReportFrame::OnCreateReport, this);
        closeBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); });
    }

private:
    wxDatePickerCtrl* fromDate_{};
    wxDatePickerCtrl* toDate{};

    PDFPrinter pdfPrinter{ "C:\\Windows\\Fonts\\arial.ttf" };

    void OnCreateReport(wxCommandEvent&)
    {
        wxDateTime dFrom = fromDate_->GetValue();
        wxDateTime dTo = toDate->GetValue();

        if (!dFrom.IsValid() || !dTo.IsValid()) {
            wxMessageBox("Please select both dates.",
                "Error", wxOK | wxICON_WARNING, this);
            return;
        }

        if (dFrom > dTo) {
            wxMessageBox("The \"from\" date cannot be later than the \"to\" date.",
                "Error", wxOK | wxICON_WARNING, this);
            return;
        }

        // выбор файла
        wxFileDialog dlg(
            this,
            "Save PDF report as...",
            "",
            "passenger_report.pdf",
            "PDF files (*.pdf)|*.pdf",
            wxFD_SAVE | wxFD_OVERWRITE_PROMPT
        );
        if (dlg.ShowModal() != wxID_OK)
            return;
        std::string path = std::string(dlg.GetPath().mb_str());
        std::string fromIso = std::string(dFrom.FormatISODate().mb_str()); // YYYY-MM-DD
        std::string toIso = std::string(dTo.FormatISODate().mb_str());
        std::string err;

        bool ok = ErrorHandler::Try([&] { //Для того что бы несколько действий передать и другие переменные взять вне функции 
            // генерация отчета
            pdfPrinter.PrintPassengersReport(fromIso, toIso, path);

            auto& s = Session::I();
            Logger::I().Write(
                "Passenger PDF report printed by " + std::string(s.userId.mb_str()) + " (" + std::string(s.position.mb_str()) + ") for period " + fromIso + " - " + toIso
            );

            wxMessageBox("PDF report has been created.",  "Passenger report", wxOK | wxICON_INFORMATION, this);

            wxLaunchDefaultApplication(dlg.GetPath());
            }, err);

        if (!ok) {
            wxMessageBox(wxString::FromUTF8(err.c_str()),  "Passenger report error", wxOK | wxICON_ERROR, this);
        }
    }

};

void ShowPDFreport(wxWindow* parent)
{
    (new PdfReportFrame(parent))->Show();
}
