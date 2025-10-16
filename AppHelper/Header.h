#pragma once
#include <string>
#include <chrono>
#include <wx/string.h>
#include <mysql/jdbc.h>
#include <memory>
#include <mutex>
#include <optional>  
#include <wx/stattext.h>

class Db {
public:
    static void init(const std::string& url,
        const std::string& user,
        const std::string& password,
        const std::string& schema);

    static Db& instance();

    std::unique_ptr<sql::Statement> createStatement();

    std::unique_ptr<sql::PreparedStatement> prepare(const std::string& sql);

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


class Employee {
public:
    Employee() = default;
    Employee(wxString id, wxString position)
        : id_(std::move(id)), position_(std::move(position)) {}

    const wxString& id() const { return id_; }
    const wxString& position() const { return position_; }

    static std::optional<Employee> Authenticate(const wxString& employeeId,
        const wxString& plainPassword) {
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
        return std::nullopt;
    }

private:
    wxString id_;
    wxString position_;
    wxString fullName_;
    wxString phoneNumber_;
    wxString email_;
};
class Passenger {
public:
    std::string PASSENGERID;
    std::string FULLNAME;
    std::string DOCNUMBER;
    std::string FLIGHTNUMBER;
    bool BAGGAGE;
    bool FOOD;
    std::string SEATNUMBER;
    std::string EMAIL;
    std::string PHONENUMBER;
};

class Flight {
public:
    std::string FLIGHTNUMBER;
    std::string DEPARTURE_AIRPORT;
    std::string ARRIVAL_AIRPORT;
    std::chrono::system_clock::time_point DEPARTURE_TIME;
    std::chrono::system_clock::time_point ARRIVAL_TIME;
    std::string AIRCRAFT_TYPE;
    int TOTAL_SEATS;
    int AVAILABLE_SEATS;
    std::string STATUS;
};

class Aircraft {
public:
    std::string REGISTRATION_NUMBER;
    std::string MODEL;
    int TOTAL_SEATS;
    int ECONOMY_SEATS;
    int BUSINESS_SEATS;
    int FIRST_CLASS_SEATS;
    std::string STATUS; // "Available", "Maintenance", "In Flight"
};

class Airport {
public:
    std::string CODE;
    std::string NAME;
    std::string CITY;
    std::string COUNTRY;
};

class Exception {
public:
    static std::optional<Employee> Auth(std::function<std::optional<Employee>()> func,
        wxStaticText* errorLabel = nullptr)
    {
        try {
            return func(); 
        }
            catch (const sql::SQLException& ex) {
            if (errorLabel)
            errorLabel->SetLabel(wxString::Format("Database error: %s", ex.what()));
        }
            catch (const std::exception& ex) {
            if (errorLabel)
            errorLabel->SetLabel(wxString::Format("Error: %s", ex.what()));
        }
             catch (...) {
            if (errorLabel)
            errorLabel->SetLabel("Unknown error.");
        }
        return std::nullopt; 
    }
};
