#pragma once
#include <string>
#include <chrono>
#include <wx/string.h>

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
