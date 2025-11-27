#pragma once
#include <string>
#include <chrono>
#include <wx/string.h>
#include <mysql/jdbc.h>
#include <memory>
#include <mutex>
#include <optional>  
#include <wx/stattext.h>
#pragma once
#include <vector>
#include <optional>
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <ctime>

#include <wx/wx.h>



class Logger {
public:
    static Logger& I() {
        static Logger inst;
        return inst;
    }

    void Write(const std::string& msg) {
        file_ << Timestamp() << " : " << msg << "\n";
        file_.flush();
    }

private:
    std::ofstream file_;
    Logger() : file_("app.log", std::ios::app) {}

    std::string Timestamp() {
        char buf[32];
        std::time_t t = std::time(nullptr);
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
        return buf;
    }
};

class AppException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class DatabaseException : public AppException {
public:
    using AppException::AppException;
};

class AuthException : public AppException {
public:
    using AppException::AppException;
};

enum class Role {
    Pilot,
    CoPilot,
    Receptionist,
    Trainee,
    Unknown,
    Admin
};
inline Role ParseRole(const wxString& s) {
    if (s == "Pilot")                          return Role::Pilot;
    if (s == "Co-Pilot" || s == "CoPilot")     return Role::CoPilot;
    if (s == "Receptionist")               return Role::Receptionist;
    if (s == "TRAINEE" || s == "Trainee")      return Role::Trainee;
    if (s == "ADMIN" || s == "Admin")      return Role::Admin;

    return Role::Unknown;
}
struct Session {
    wxString userId;
    wxString position;
    Role role{ Role::Unknown };
    static Session& I() { static Session s; return s; }
    bool IsEmployee() const {
        switch (role) {
        case Role::Pilot:
        case Role::CoPilot:
        case Role::Receptionist:
        case Role::Trainee:
        case Role::Admin:
            return true;
        default:
            return false;
        }
    }
};

class Person {
public:
    std::string FULLNAME;
    std::string EMAIL;
    std::string PHONENUMBER;

    Person() = default;

    Person(std::string fullName,
        std::string email,
        std::string phone)
        : FULLNAME(std::move(fullName)),
        EMAIL(std::move(email)),
        PHONENUMBER(std::move(phone))
    {}
};


class Passenger : public Person {
public:
    std::string PASSENGERID;
    std::string DOCNUMBER;
    std::string FLIGHTNUMBER;
    bool        BAGGAGE{};
    bool        FOOD{};
    std::string SEATNUMBER;

    struct Filter {
        std::optional<std::string> PassengerID;
        std::optional<std::string> FullName;
        std::optional<std::string> DocNumber;
        std::optional<std::string> FlightNumber;
        std::optional<std::string> SeatNumber;
        std::optional<std::string> Email;
        std::optional<std::string> PhoneNumber;
        std::optional<int>         Baggage;
        std::optional<int>         Food;
        std::map<std::string, std::string> extraEquals;

        bool empty() const {
            return !PassengerID && !FullName && !DocNumber && !FlightNumber &&
                !SeatNumber && !Email && !PhoneNumber && !Baggage && !Food &&
                extraEquals.empty();
        }
    };

    // эти три метода у тебя и так были объявлены
    static std::vector<Passenger> ReadFromXlsx(const std::string& path);
    static void WriteToXlsx(const std::string& path,
        const std::vector<Passenger>& passengers);
    static std::vector<Passenger> FilterPassengers(const std::vector<Passenger>& all,
        const Filter& filter);

    // ВАЖНО: только объявление, без тела!
    static std::map<std::string, int> LoadOccupiedSeatsByFlight();
};

class Aircraft {
public:
    std::string REGISTRATION_NUMBER;
    std::string MODEL;
    int TOTAL_SEATS{};
    int ECONOMY_SEATS{};
    int BUSINESS_SEATS{};
    int FIRST_CLASS_SEATS{};
    std::string STATUS; // "Available", "Maintenance", "In Flight"

    // Только ОБЪЯВЛЕНИЯ методов (как ты просил)
    static std::map<std::string, int> LoadTotalSeatsByRegistration();
    static bool Exists(const std::string& registration);
    static int GetTotalSeats(const std::string& registration);
};

class Flight {
public:
    std::string FLIGHTNUMBER;
    std::string DEPARTURE_AIRPORT;
    std::string ARRIVAL_AIRPORT;
    std::chrono::system_clock::time_point DEPARTURE_TIME{};
    std::chrono::system_clock::time_point ARRIVAL_TIME{};
    std::string AIRCRAFT_TYPE;
    int AVAILABLE_SEATS{};
    std::string STATUS;

    struct Filter {
        std::optional<std::string> FlightNumber;
        std::optional<std::string> DepartureAirport;
        std::optional<std::string> ArrivalAirport;
        std::optional<std::string> AircraftType;
        std::optional<std::string> Status;  

        bool empty() const {
            return !FlightNumber &&
                !DepartureAirport &&
                !ArrivalAirport &&
                !AircraftType &&
                !Status;
        }
    };

    void Validate() const;
    void ComputeAvailableSeats();

    static void ValidateFlightsForImport(std::vector<Flight>& flights);
    static void ComputeAvailableSeats(std::vector<Flight>& flights);

    static bool IsValidFlightNumberFormat(const std::string& fn);
    static bool IsValidAirportCode(const std::string& code);

    bool IsValidRoute() const;
    bool IsValidTimes() const;
    bool IsValidAircraft() const;

};

class Db {
public:
    static void init(const std::string& url,
        const std::string& user,
        const std::string& password,
        const std::string& schema);
    static Db& instance();

    std::unique_ptr<sql::Statement> createStatement();
    std::unique_ptr<sql::PreparedStatement> prepare(const std::string& sql);

    std::vector<Passenger> QueryPassengers(const Passenger::Filter& filter = {});
    void InsertOrUpdatePassenger(const Passenger& p);
    void InsertOrUpdatePassengers(const std::vector<Passenger>& passengers, bool skipIdentical = true);
    std::vector<Flight> QueryFlights(const Flight::Filter& filter = {});
    void InsertOrUpdateFlights(const std::vector<Flight>& flights, bool skipIdentical = true);
    void DeletePassengers(const std::vector<std::string>& passengerIds);


private:
    Db(const std::string& url,
        const std::string& user,
        const std::string& password,
        const std::string& schema);
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;

    void ensureConnected();

    static Db* instance_;
    sql::Driver* driver_;
    std::unique_ptr<sql::Connection> conn_;
    std::string url_, user_, password_, schema_;
    std::mutex m_;
};

class Employee : public Person {
public:
    Employee() = default;
    Employee(wxString id, wxString position)
        : id_(std::move(id)), position_(std::move(position)) {}

    const wxString& id() const { return id_; }
    const wxString& position() const { return position_; }

    static std::optional<Employee> Authenticate(const wxString& employeeId, const wxString& plainPassword) {
        try {
            auto ps = Db::instance().prepare(
                "SELECT EmployeeID, Position "
                "FROM employees "
                "WHERE EmployeeID = ? AND Password = ? "
                "LIMIT 1"
            );
            ps->setString(1, std::string(employeeId.mb_str()));
            ps->setString(2, std::string(plainPassword.mb_str()));
            std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());
            if (rs->next()) {
                wxString id = wxString::FromUTF8(rs->getString("EmployeeID"));
                wxString pos = wxString::FromUTF8(rs->getString("Position"));
                return Employee{ id, pos };
            }
            return std::nullopt; // esli ne nashel to ne iskluchenija
        }
        catch (const sql::SQLException& e) {
            throw DatabaseException(e.what()); // error v sql stanovitsa svoim iskluchenijem
        }
    }

    struct PassengerEditAudit {
        std::string auditId;     
        wxString    employeeId;  
        wxString    position;
        size_t rowsBefore = 0;
        size_t rowsAfter = 0;
        size_t rowsAdded = 0;
        size_t rowsUpdated = 0;
        size_t rowsDeleted = 0;
        wxString ToString() const {
            return wxString::Format(
                "Audit: %s\nEmployee: %s (%s)\nBefore: %zu, After: %zu\nAdded: %zu, Updated: %zu, Deleted: %zu",
                wxString::FromUTF8(auditId), employeeId, position,
                rowsBefore, rowsAfter, rowsAdded, rowsUpdated, rowsDeleted
            );
        }
    };
    PassengerEditAudit PreparePassengerEditAudit(const std::vector<Passenger>& before,
        const std::vector<Passenger>& after) const;

private:
    wxString id_;
    wxString position_;
    wxString fullName_;
    wxString phoneNumber_;
    wxString email_;
};

class ErrorHandler {
public:
    template<typename F>
    static auto Exec(F&& func, wxStaticText* errorLabel = nullptr) -> decltype(func()) {
        try { return func(); }
        catch (const sql::SQLException& ex) {
            if (errorLabel) errorLabel->SetLabel(wxString::Format("Database error: %s", ex.what()));
            throw DatabaseException(ex.what());
        }
        catch (const std::exception& ex) {
            if (errorLabel) errorLabel->SetLabel(wxString::Format("Error: %s", ex.what()));
            throw AppException(ex.what());
        }
        catch (...) {
            if (errorLabel) errorLabel->SetLabel("Unknown error.");
            throw AppException("Unknown error.");
        }
    }

    template<typename F>
    static auto Auth(F&& func, wxStaticText* errorLabel = nullptr) -> decltype(func()) {
        return Exec(std::forward<F>(func), errorLabel);
    }

    template<typename F,
        typename T = std::invoke_result_t<F>>
        static std::optional<T> ExecOrError(F&& func,
            std::string& err,
            wxStaticText* errorLabel = nullptr)
    {
        try {
            return std::optional<T>(std::forward<F>(func)());
        }
        catch (const sql::SQLException& ex) {
            if (errorLabel) errorLabel->SetLabel(wxString::Format("Database error: %s", ex.what()));
            err = ex.what();
            return std::nullopt;
        }
        catch (const std::exception& ex) {
            if (errorLabel) errorLabel->SetLabel(wxString::Format("Error: %s", ex.what()));
            err = ex.what();
            return std::nullopt;
        }
        catch (...) {
            if (errorLabel) errorLabel->SetLabel("Unknown error.");
            err = "Unknown error.";
            return std::nullopt;
        }
    }

    // Varik bez vozrashaemogo znachenija: true — ok, false —  oshubkaa v err
    template<typename F>
    static bool Try(F&& func, std::string& err, wxStaticText* errorLabel = nullptr)
    {
        try { std::forward<F>(func)(); return true; }
        catch (const sql::SQLException& ex) {
            if (errorLabel) errorLabel->SetLabel(wxString::Format("Database error: %s", ex.what()));
            err = ex.what(); return false;
        }
        catch (const std::exception& ex) {
            if (errorLabel) errorLabel->SetLabel(wxString::Format("Error: %s", ex.what()));
            err = ex.what(); return false;
        }
        catch (...) {
            if (errorLabel) errorLabel->SetLabel("Unknown error.");
            err = "Unknown error."; return false;
        }
    }
    // vspomagatelnoje
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
        if (atPos <= 0) return false;

        int dotPos = e.Find('.', true); 
        if (dotPos == wxNOT_FOUND) return false;
        if (dotPos <= atPos + 1) return false;
        if (dotPos >= (int)e.length() - 1) return false;

        return true;
    }

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

    static bool NormalizeDocNumber(wxString& doc) {
        doc = doc.Strip(wxString::both);
        if (doc.IsEmpty()) return false;
        for (wxUniChar c : doc) {
            if (!wxIsalnum(c)) return false;
        }
        doc.MakeUpper();
        return true;
    }

    static bool CellToBool(const wxString& s) {
        wxString v = s.Lower().Strip(wxString::both);
        return (v == "1" || v == "true" || v == "yes" || v == "y" || v == "on");
    }

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
};



class Airport {
public:
    std::string iata;
    std::string icao;
    std::string name;
    std::string city;
    std::string country;
    double      latitude = 0.0;
    double      longitude = 0.0;

    static std::vector<Airport> Search(const std::string& userInput, wxStaticText* errorLabel = nullptr) {
        return ErrorHandler::Exec([&] {
            const std::string input = trim(userInput);
            if (input.empty())
                throw AppException("Please enter airport code, name, city, or country.");

            const std::string upperInput = toUpper(input);
            bool isIATA = (upperInput.size() == 3 && isAlpha(upperInput));
            bool isICAO = (upperInput.size() == 4 && isAlpha(upperInput));

            std::string sql;
            if (isIATA) {
                sql = "SELECT iata, icao, name, city, country, latitude, longitude "
                    "FROM airports WHERE UPPER(iata) = ? LIMIT 1";
            }
            else if (isICAO) {
                sql = "SELECT iata, icao, name, city, country, latitude, longitude "
                    "FROM airports WHERE UPPER(icao) = ? LIMIT 1";
            }
            else {
                sql = "SELECT iata, icao, name, city, country, latitude, longitude "
                    "FROM airports "
                    "WHERE LOWER(name) = ? OR LOWER(city) = ? OR LOWER(country) = ? "
                    "   OR name LIKE ? OR city LIKE ? OR country LIKE ? "
                    "ORDER BY city, name";
            }

            auto ps = Db::instance().prepare(sql);

            if (isIATA || isICAO) {
                ps->setString(1, upperInput);
            }
            else {
                const std::string lowerInput = toLower(input);
                const std::string like = "%" + lowerInput + "%";
                ps->setString(1, lowerInput);
                ps->setString(2, lowerInput);
                ps->setString(3, lowerInput);
                ps->setString(4, like);
                ps->setString(5, like);
                ps->setString(6, like);
            }

            std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());
            std::vector<Airport> result;
            while (rs->next())
                result.push_back(fromRow(*rs));

            if (result.empty())
                throw AppException("No airports found for query: " + input);

            return result;
            }, errorLabel);
    }

private:
    static Airport fromRow(sql::ResultSet& rs) {
        Airport a;
        a.iata = rs.isNull("iata") ? "" : rs.getString("iata");
        a.icao = rs.isNull("icao") ? "" : rs.getString("icao");
        a.name = rs.getString("name");
        a.city = rs.getString("city");
        a.country = rs.getString("country");
        a.latitude = rs.getDouble("latitude");
        a.longitude = rs.getDouble("longitude");
        return a;
    }

    static std::string trim(const std::string& s) {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace((unsigned char)s[a])) ++a;
        while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
        return s.substr(a, b - a);
    }

    static std::string toUpper(std::string s) {
        for (auto& c : s)
            c = (char)std::toupper((unsigned char)c);
        return s;
    }

    static std::string toLower(std::string s) {
        for (auto& c : s)
            c = (char)std::tolower((unsigned char)c);
        return s;
    }

    static bool isAlpha(const std::string& s) {
        return std::all_of(s.begin(), s.end(),
            [](unsigned char c) { return std::isalpha(c); });
    }
};

class PDFPrinter
{
public:
    // Конструктор принимает путь к TTF-шрифту
    explicit PDFPrinter(const std::string& ttfPath);

    // Напечатать посадочный талон для конкретного пассажира
    // pdfPath — куда сохранить PDF
    void PrintTicketForPassenger(const Passenger& p,
        const std::string& pdfPath) const;

    // Напечатать отчёт по пассажирам за период дат (включительно).
    // Даты в формате "YYYY-MM-DD" (как wxDateTime::FormatISODate()).
    void PrintPassengersReport(const std::string& dateFromIso,
        const std::string& dateToIso,
        const std::string& pdfPath) const;

private:
    std::string ttfPath_;
    // НИКАКИХ HPDF_Page / HPDF_STATUS здесь больше нет
};
