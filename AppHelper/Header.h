#pragma once
#include <string>
#include <chrono>
#include <wx/string.h>
#include <mysql/jdbc.h>
#include <memory>
#include <mutex>

class Employee {
public:
    wxString id;
    wxString position;
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