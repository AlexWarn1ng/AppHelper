#define _CRT_SECURE_NO_WARNINGS
#include <hpdf.h>
#include "Header.h"    // чтобы увидеть struct Passenger
#include <stdexcept>
#include <string>
#include <cstdio>

extern "C" {
#include <zint.h>
}

// ---------- Реализация класса PDFPrinter ----------

PDFPrinter::PDFPrinter(const std::string& ttfPath)
    : ttfPath_(ttfPath)
{
}

// Статический обработчик ошибок libharu
static void HaruErrorHandler(HPDF_STATUS code, HPDF_STATUS detail, void*)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "libharu error: 0x%04X, detail=%d",
        (unsigned)code, (int)detail);
    throw std::runtime_error(buf);
}

// Нарисовать QR из Zint прямо в PDF
static void DrawQrOnPdf(HPDF_Page page, const std::string& textUtf8,
    float posX, float posY, float sizePt)
{
    zint_symbol* sym = ZBarcode_Create();
    if (!sym)
        throw std::runtime_error("Zint: cannot create symbol");

    sym->symbology = BARCODE_QRCODE;
    sym->input_mode = UNICODE_MODE;
    sym->eci = 26;
    sym->option_1 = 4;   // ECC H
    sym->scale = 1.0f;
    sym->border_width = 0;

    int rc = ZBarcode_Encode(sym,
        reinterpret_cast<const unsigned char*>(textUtf8.c_str()),
        static_cast<int>(textUtf8.size()));
    if (rc != 0) {
        std::string msg = "Zint encode fail: " + std::to_string(rc) + " " + sym->errtxt;
        ZBarcode_Delete(sym);
        throw std::runtime_error(msg);
    }

    rc = ZBarcode_Buffer_Vector(sym, 0);
    if (rc != 0 || !sym->vector) {
        std::string msg = "Zint vector fail: " + std::to_string(rc) + " " + sym->errtxt;
        ZBarcode_Delete(sym);
        throw std::runtime_error(msg);
    }

    const zint_vector* v = sym->vector;
    float scale = sizePt / v->width;

    HPDF_Page_SetGrayFill(page, 0.0f);
    for (const zint_vector_rect* r = v->rectangles; r; r = r->next) {
        float rx = posX + r->x * scale;
        float ry = posY + r->y * scale;
        float rw = r->width * scale;
        float rh = r->height * scale;
        HPDF_Page_Rectangle(page, rx, ry, rw, rh);
        HPDF_Page_Fill(page);
    }

    ZBarcode_Delete(sym);
}

void PDFPrinter::PrintTicketForPassenger(const Passenger& p,
    const std::string& pdfPath) const
{
    // создаём PDF-документ
    HPDF_Doc pdf = HPDF_New(HaruErrorHandler, nullptr);  // БЕЗ PDFPrinter:: и &

    if (!pdf)
        throw std::runtime_error("Cannot create PDF");

    try {
        HPDF_UseUTFEncodings(pdf);
        HPDF_SetCurrentEncoder(pdf, "UTF-8");

        const char* fontName =
            HPDF_LoadTTFontFromFile(pdf, ttfPath_.c_str(), HPDF_TRUE);
        HPDF_Font font = HPDF_GetFont(pdf, fontName, "UTF-8");

        HPDF_Page page = HPDF_AddPage(pdf);
        HPDF_Page_SetWidth(page, 842);
        HPDF_Page_SetHeight(page, 300);

        // ---- Верхняя полоса ----
        HPDF_Page_SetRGBFill(page, 0.1f, 0.3f, 0.7f);
        HPDF_Page_Rectangle(page, 0, 280, 842, 20);
        HPDF_Page_Fill(page);

        // Заголовок
        HPDF_Page_BeginText(page);
        HPDF_Page_SetFontAndSize(page, font, 24);
        HPDF_Page_TextOut(page, 30, 250, u8"АВИАКОМПАНИЯ ИМЯ");
        HPDF_Page_EndText(page);

        // Основные данные
        HPDF_Page_BeginText(page);
        HPDF_Page_SetFontAndSize(page, font, 14);

        std::string flt = p.FLIGHTNUMBER;
        std::string date = "03 Окт 2025";   // TODO
        std::string boarding = "14:25";         // TODO

        std::string lineFlight = std::string(u8"Рейс: ") + flt;
        std::string lineDate = std::string(u8"Дата: ") + date;
        std::string lineBoarding = std::string(u8"Посадка: ") + boarding;

        HPDF_Page_TextOut(page, 30, 220, lineFlight.c_str());
        HPDF_Page_TextOut(page, 200, 220, lineDate.c_str());
        HPDF_Page_TextOut(page, 400, 220, lineBoarding.c_str());
        HPDF_Page_EndText(page);

        // Пассажир, откуда/куда
        HPDF_Page_BeginText(page);
        HPDF_Page_SetFontAndSize(page, font, 16);

        std::string name = p.FULLNAME;
        std::string seat = p.SEATNUMBER;

        std::string fromCity = "Москва (SVO)";
        std::string toCity = "Санкт-Петербург (LED)";

        std::string lineName = std::string(u8"Пассажир: ") + name;
        std::string lineFrom = std::string(u8"Откуда: ") + fromCity;
        std::string lineTo = std::string(u8"Куда: ") + toCity;

        HPDF_Page_TextOut(page, 30, 190, lineName.c_str());
        HPDF_Page_TextOut(page, 30, 165, lineFrom.c_str());
        HPDF_Page_TextOut(page, 300, 165, lineTo.c_str());
        HPDF_Page_EndText(page);

        // Место и выход
        HPDF_Page_BeginText(page);
        HPDF_Page_SetFontAndSize(page, font, 16);

        std::string gate = "B12"; // TODO
        std::string lineSeat = std::string(u8"Место: ") + seat;
        std::string lineGate = std::string(u8"Выход: ") + gate;

        HPDF_Page_TextOut(page, 30, 140, lineSeat.c_str());
        HPDF_Page_TextOut(page, 200, 140, lineGate.c_str());
        HPDF_Page_EndText(page);

        // QR снизу справа
        std::string qrPayload =
            flt + "|" + date + "|SVO-LED|" + name + "|SEAT " + seat + "|GATE " + gate;

        float qrSize = 160.0f;
        float qrX = 820 - qrSize - 20;
        float qrY = 20.0f;

        DrawQrOnPdf(page, qrPayload, qrX, qrY, qrSize);

        // подпись под QR
        HPDF_Page_BeginText(page);
        HPDF_Page_SetFontAndSize(page, font, 10);
        HPDF_Page_TextOut(page, qrX, qrY - 12, u8"Покажите QR при посадке");
        HPDF_Page_EndText(page);

        HPDF_SaveToFile(pdf, pdfPath.c_str());
        HPDF_Free(pdf);
    }
    catch (...) {
        HPDF_Free(pdf);
        throw;
    }
}