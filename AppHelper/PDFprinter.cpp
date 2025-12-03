#define _CRT_SECURE_NO_WARNINGS
#include <hpdf.h>
#include "Header.h"
#include <stdexcept>
#include <string>
#include <cstdio>
#include <ctime> 

extern "C" {
#include <zint.h>
}

PDFPrinter::PDFPrinter(const std::string& ttfPath)
    : ttfPath_(ttfPath)
{
}

// Обработчик ошибок Haru
static void HaruErrorHandler(HPDF_STATUS code, HPDF_STATUS detail, void*)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "libharu error: 0x%04X, detail=%d",
        (unsigned)code, (int)detail);
    throw std::runtime_error(buf);
}

// Рисуем qr код в pdf
static void DrawQrOnPdf(HPDF_Page page, const std::string& textUtf8,
    float posX, float posY, float sizePt)
{
    zint_symbol* sym = ZBarcode_Create();
    if (!sym)
        throw std::runtime_error("Zint: cannot create symbol");

    sym->symbology = BARCODE_QRCODE;
    sym->input_mode = UNICODE_MODE;
    sym->eci = 26;
    sym->option_1 = 4;
    sym->scale = 1.0f;
    sym->border_width = 0;

    if (ZBarcode_Encode(sym,
        reinterpret_cast<const unsigned char*>(textUtf8.c_str()),
        static_cast<int>(textUtf8.size())) != 0)
    {
        std::string msg = "Zint encode fail: " + std::string(sym->errtxt);
        ZBarcode_Delete(sym);
        throw std::runtime_error(msg);
    }

    if (ZBarcode_Buffer_Vector(sym, 0) != 0 || !sym->vector)
    {
        std::string msg = "Zint vector fail: " + std::string(sym->errtxt);
        ZBarcode_Delete(sym);
        throw std::runtime_error(msg);
    }

    const zint_vector* v = sym->vector;
    float scale = sizePt / v->width;

    HPDF_Page_SetGrayFill(page, 0.0f);

    for (const zint_vector_rect* r = v->rectangles; r; r = r->next)
    {
        HPDF_Page_Rectangle(
            page,
            posX + r->x * scale,
            posY + r->y * scale,
            r->width * scale,
            r->height * scale
        );
        HPDF_Page_Fill(page);
    }

    ZBarcode_Delete(sym);
}


// "YYYY-MM-DD" -> time_point начала/конца дня
static std::chrono::system_clock::time_point ParseDateIso(const std::string& iso,
    bool endOfDay)
{
    if (iso.size() != 10 || iso[4] != '-' || iso[7] != '-') {
        throw std::runtime_error("Invalid date format, expected YYYY-MM-DD");
    }

    int year = std::stoi(iso.substr(0, 4));
    int month = std::stoi(iso.substr(5, 2));
    int day = std::stoi(iso.substr(8, 2));

    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;

    if (endOfDay) {
        tm.tm_hour = 23;
        tm.tm_min = 59;
        tm.tm_sec = 59;
    }
    else {
        tm.tm_hour = 0;
        tm.tm_min = 0;
        tm.tm_sec = 0;
    }

    std::time_t tt = std::mktime(&tm);
    return std::chrono::system_clock::from_time_t(tt);
}


// gечать билета 

void PDFPrinter::PrintTicketForPassenger(const Passenger& p,
    const std::string& pdfPath) const
{
    HPDF_Doc pdf = HPDF_New(HaruErrorHandler, nullptr);
    if (!pdf)
        throw std::runtime_error("Cannot create PDF");

    try
    {
        HPDF_UseUTFEncodings(pdf);
        HPDF_SetCurrentEncoder(pdf, "UTF-8");

        HPDF_Font font =
            HPDF_GetFont(pdf,
                HPDF_LoadTTFontFromFile(pdf, ttfPath_.c_str(), HPDF_TRUE),
                "UTF-8");

        HPDF_Page page = HPDF_AddPage(pdf);
        HPDF_Page_SetWidth(page, 842);
        HPDF_Page_SetHeight(page, 300);

        //читаем рейс из таблицы Flights
        bool hasFlight = false;
        std::string fromAirport = "-";
        std::string toAirport = "-";
        std::string depDT = "-";
        std::string arrDT = "-";

        try {
            auto ps = Db::instance().prepare(
                "SELECT DepartureAirport, ArrivalAirport, "
                "       DepartureTime, ArrivalTime "
                "FROM flights WHERE FlightNumber = ?"
            );
            ps->setString(1, p.FLIGHTNUMBER);

            std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());
            if (rs->next()) {
                hasFlight = true;

                fromAirport = rs->getString("DepartureAirport");
                toAirport = rs->getString("ArrivalAirport");

                depDT = rs->getString("DepartureTime");
                arrDT = rs->getString("ArrivalTime");
            }
        }
        catch (...) {}

        //верхняя синяя полоса
        HPDF_Page_SetRGBFill(page, 0.1f, 0.3f, 0.7f);
        HPDF_Page_Rectangle(page, 0, 280, 842, 20);
        HPDF_Page_Fill(page);

        // Заголовок
        HPDF_Page_BeginText(page);
        HPDF_Page_SetFontAndSize(page, font, 24);
        HPDF_Page_TextOut(page, 30, 250, u8"АВИАКОМПАНИЯ ИМЯ");
        HPDF_Page_EndText(page);

        // Рейс вылет прибытие
        HPDF_Page_BeginText(page);
        HPDF_Page_SetFontAndSize(page, font, 14);

        HPDF_Page_TextOut(page, 30, 220, (std::string(u8"Рейс: ") + p.FLIGHTNUMBER).c_str());
        HPDF_Page_TextOut(page, 230, 220, (std::string(u8"Вылет: ") + depDT).c_str());
        HPDF_Page_TextOut(page, 480, 220, (std::string(u8"Прибытие: ") + arrDT).c_str());

        HPDF_Page_EndText(page);

        //Пассажир
        HPDF_Page_BeginText(page);
        HPDF_Page_SetFontAndSize(page, font, 16);

        HPDF_Page_TextOut(page, 30, 190, (std::string(u8"Пассажир: ") + p.FULLNAME).c_str());
        HPDF_Page_TextOut(page, 30, 165, (std::string(u8"Откуда: ") + fromAirport).c_str());
        HPDF_Page_TextOut(page, 300, 165, (std::string(u8"Куда: ") + toAirport).c_str());

        // место, багаж, питание
        HPDF_Page_TextOut(page, 30, 140, (std::string(u8"Место: ") + p.SEATNUMBER).c_str());
        HPDF_Page_TextOut(page, 30, 120, (std::string(u8"Багаж: ") + (p.BAGGAGE ? u8"есть" : u8"нет")).c_str());
        HPDF_Page_TextOut(page, 30, 100, (std::string(u8"Питание: ") + (p.FOOD ? u8"заказано" : u8"нет")).c_str());

        HPDF_Page_EndText(page);

        //QR
        std::string route = hasFlight ?
            (fromAirport + "-" + toAirport) : "UNKNOWN";

        std::string qrPayload =
            p.FLIGHTNUMBER + "|" + depDT + "|" + arrDT + "|" + route +
            "|" + p.FULLNAME + "|SEAT " + p.SEATNUMBER;

        DrawQrOnPdf(page, qrPayload, 820 - 160 - 20, 20, 160);

        // подпись
        HPDF_Page_BeginText(page);
        HPDF_Page_SetFontAndSize(page, font, 10);
        HPDF_Page_TextOut(page, 820 - 160 - 20, 8, u8"Покажите QR при посадке");
        HPDF_Page_EndText(page);

        HPDF_SaveToFile(pdf, pdfPath.c_str());
        HPDF_Free(pdf);
    }
    catch (...) {
        HPDF_Free(pdf);
        throw;
    }
}

void PDFPrinter::PrintPassengersReport(const std::string& dateFromIso,
    const std::string& dateToIso,
    const std::string& pdfPath) const
{
    auto tpFrom = ParseDateIso(dateFromIso, false);
    auto tpTo = ParseDateIso(dateToIso, true);

    auto allFlights = Db::instance().QueryFlights();
    std::vector<Flight> flights;
    flights.reserve(allFlights.size());

    for (const auto& f : allFlights) {
        if (f.DEPARTURE_TIME >= tpFrom && f.DEPARTURE_TIME <= tpTo) {
            flights.push_back(f);
        }
    }
    std::sort(flights.begin(), flights.end(),
        [](const Flight& a, const Flight& b) {
            if (a.DEPARTURE_TIME != b.DEPARTURE_TIME)
                return a.DEPARTURE_TIME < b.DEPARTURE_TIME;
            return a.FLIGHTNUMBER < b.FLIGHTNUMBER;
        });

    // Создаём pdf
    HPDF_Doc pdf = HPDF_New(HaruErrorHandler, nullptr);
    if (!pdf)
        throw std::runtime_error("Cannot create PDF");

    try
    {
        HPDF_UseUTFEncodings(pdf);
        HPDF_SetCurrentEncoder(pdf, "UTF-8");

        HPDF_Font font =
            HPDF_GetFont(pdf,
                HPDF_LoadTTFontFromFile(pdf, ttfPath_.c_str(), HPDF_TRUE),
                "UTF-8");

        // Формат страницы — A4 портрет
        const float PAGE_W = 595.0f;
        const float PAGE_H = 842.0f;
        const float LEFT = 40.0f;
        const float RIGHT = PAGE_W - 40.0f;
        const float TOP = PAGE_H - 60.0f;
        const float BOTTOM = 40.0f;
        const float LINE_H = 18.0f;

        HPDF_Page page = nullptr;
        float y = 0.0f;
        int pageNo = 0;

        auto NewPage = [&]() {
            page = HPDF_AddPage(pdf);
            ++pageNo;
            HPDF_Page_SetWidth(page, PAGE_W);
            HPDF_Page_SetHeight(page, PAGE_H);

            // Верхняя синяя полоса
            HPDF_Page_SetRGBFill(page, 0.1f, 0.3f, 0.7f);
            HPDF_Page_Rectangle(page, 0, PAGE_H - 40, PAGE_W, 40);
            HPDF_Page_Fill(page);

            HPDF_Page_SetRGBFill(page, 0.0f, 0.0f, 0.0f);

            // Заголовок
            HPDF_Page_BeginText(page);
            HPDF_Page_SetFontAndSize(page, font, 18);
            HPDF_Page_TextOut(page, LEFT, PAGE_H - 28, u8"Отчёт по пассажажирам");

            // Период
            HPDF_Page_SetFontAndSize(page, font, 11);
            float periodY = PAGE_H - 60.0f;     
            std::string period = u8"Период: " + dateFromIso + " — " + dateToIso;
            HPDF_Page_TextOut(page, LEFT, periodY, period.c_str());

            HPDF_Page_EndText(page);

 
            y = periodY - 25.0f;   
            };


        auto EnsureSpace = [&](float need) {
            if (!page || y - need < BOTTOM) {
                NewPage();
            }
            };
        auto DrawPassengersHeader = [&](const Flight& flt) {

            HPDF_Page_BeginText(page);
            HPDF_Page_SetFontAndSize(page, font, 14);

            std::string line1 =
                std::string(u8"Рейс ") + flt.FLIGHTNUMBER +
                "  " + flt.DEPARTURE_AIRPORT + " -> " + flt.ARRIVAL_AIRPORT;
            HPDF_Page_TextOut(page, LEFT, y, line1.c_str());
            y -= LINE_H * 1.3f;

            HPDF_Page_SetFontAndSize(page, font, 10);
            std::string line2 =
                std::string(u8"Вылет: ") + ErrorHandler::TimePointToString(flt.DEPARTURE_TIME) +
                std::string(u8"   Прибытие: ") + ErrorHandler::TimePointToString(flt.ARRIVAL_TIME) +
                std::string(u8"   Борт: ") + flt.AIRCRAFT_TYPE +
                std::string(u8"   Статус: ") + flt.STATUS;

            HPDF_Page_TextOut(page, LEFT, y, line2.c_str());
            HPDF_Page_EndText(page);

            y -= LINE_H * 1.5f;

            HPDF_Page_BeginText(page);
            HPDF_Page_SetFontAndSize(page, font, 9);

            HPDF_Page_TextOut(page, LEFT, y, u8"ID");
            HPDF_Page_TextOut(page, LEFT + 50, y, u8"ФИО");
            HPDF_Page_TextOut(page, LEFT + 230, y, u8"Документ");
            HPDF_Page_TextOut(page, LEFT + 340, y, u8"Место");
            HPDF_Page_TextOut(page, LEFT + 390, y, u8"Багаж");
            HPDF_Page_TextOut(page, LEFT + 440, y, u8"Питание");

            HPDF_Page_EndText(page);

            y -= LINE_H * 1.0f;

            HPDF_Page_SetLineWidth(page, 0.5f);
            HPDF_Page_MoveTo(page, LEFT, y);
            HPDF_Page_LineTo(page, RIGHT, y);
            HPDF_Page_Stroke(page);

            y -= LINE_H * 0.8f;
            };

        if (flights.empty()) {
            NewPage();
            HPDF_Page_BeginText(page);
            HPDF_Page_SetFontAndSize(page, font, 12);
            HPDF_Page_TextOut(page, LEFT, y,
                u8"В указанном периоде рейсов не найдено.");
            HPDF_Page_EndText(page);

            HPDF_SaveToFile(pdf, pdfPath.c_str());
            HPDF_Free(pdf);
            return;
        }

        for (const auto& flt : flights) {

            Passenger::Filter pf;
            pf.FlightNumber = flt.FLIGHTNUMBER;
            auto passengers = Db::instance().QueryPassengers(pf);

            // Сортировка пассажиров по месту а затем по имени
            std::sort(passengers.begin(), passengers.end(),
                [](const Passenger& a, const Passenger& b) {
                    if (a.SEATNUMBER != b.SEATNUMBER)
                        return a.SEATNUMBER < b.SEATNUMBER;
                    return a.FULLNAME < b.FULLNAME;
                });

            EnsureSpace(LINE_H * 6.0f);

            DrawPassengersHeader(flt);

            if (passengers.empty()) {
                EnsureSpace(LINE_H * 2.0f);
                HPDF_Page_BeginText(page);
                HPDF_Page_SetFontAndSize(page, font, 10);
                HPDF_Page_TextOut(page, LEFT, y,
                    u8"Нет пассажиров на этом рейсе.");
                HPDF_Page_EndText(page);
                y -= LINE_H * 1.5f;
                continue;
            }

            // Строки по пассажирам
            for (const auto& p : passengers) {
                int prevPageNo = pageNo;

                EnsureSpace(LINE_H * 3.0f);

                if (pageNo != prevPageNo) {
                    DrawPassengersHeader(flt);
                }

                HPDF_Page_BeginText(page);
                HPDF_Page_SetFontAndSize(page, font, 9);

                std::string id = p.PASSENGERID;
                std::string fio = p.FULLNAME;
                std::string doc = p.DOCNUMBER;
                std::string seat = p.SEATNUMBER;
                std::string bag = p.BAGGAGE ? u8"Да" : u8"Нет";
                std::string food = p.FOOD ? u8"Да" : u8"Нет";

                HPDF_Page_TextOut(page, LEFT, y, id.c_str());
                HPDF_Page_TextOut(page, LEFT + 50, y, fio.c_str());
                HPDF_Page_TextOut(page, LEFT + 230, y, doc.c_str());
                HPDF_Page_TextOut(page, LEFT + 340, y, seat.c_str());
                HPDF_Page_TextOut(page, LEFT + 390, y, bag.c_str());
                HPDF_Page_TextOut(page, LEFT + 440, y, food.c_str());

                HPDF_Page_EndText(page);

                y -= LINE_H * 1.3f;
            }

            // отступ между рейсами
            y -= LINE_H * 1.6f;
        }


        HPDF_SaveToFile(pdf, pdfPath.c_str());
        HPDF_Free(pdf);
    }
    catch (...)
    {
        HPDF_Free(pdf);
        throw;
    }
}