#include "Header.h"
#include <stdexcept>
#include <sstream>
#include <iomanip>    
#include <set>  
#include <sstream>   
// Вспомогательные функции для WHERE 
static std::string BuildWhere(const Passenger::Filter& f,
                              std::vector<std::string>& bindOrder)
{
    if (f.empty()) return "";
    std::ostringstream w;
    w << " WHERE ";
    bool first = true;
    auto add = [&](const std::string& col) {
        if (!first) w << " AND ";
        first = false;
        w << col << " = ?";
        bindOrder.push_back(col);
    };

    if (f.PassengerID)  add("PassengerID");
    if (f.FullName)     add("FullName");
    if (f.DocNumber)    add("DocNumber");
    if (f.FlightNumber) add("FlightNumber");
    if (f.SeatNumber)   add("SeatNumber");
    if (f.Email)        add("Email");
    if (f.PhoneNumber)  add("PhoneNumber");
    if (f.Baggage)      add("Baggage");
    if (f.Food)         add("Food");
    for (auto& kv : f.extraEquals) add(kv.first);

    return w.str();
}

static void BindWhere(std::unique_ptr<sql::PreparedStatement>& ps,
                      const Passenger::Filter& f,
                      const std::vector<std::string>& order)
{
    int idx = 1;
    for (const auto& col : order) {
        if      (col=="PassengerID")  ps->setString(idx++, *f.PassengerID);
        else if (col=="FullName")     ps->setString(idx++, *f.FullName);
        else if (col=="DocNumber")    ps->setString(idx++, *f.DocNumber);
        else if (col=="FlightNumber") ps->setString(idx++, *f.FlightNumber);
        else if (col=="SeatNumber")   ps->setString(idx++, *f.SeatNumber);
        else if (col=="Email")        ps->setString(idx++, *f.Email);
        else if (col=="PhoneNumber")  ps->setString(idx++, *f.PhoneNumber);
        else if (col=="Baggage")      ps->setInt(idx++,  *(f.Baggage));
        else if (col=="Food")         ps->setInt(idx++,  *(f.Food));
        else                          ps->setString(idx++, f.extraEquals.at(col));
    }
}

static std::string BuildWhere(const Flight::Filter& f,
    std::vector<std::string>& bindOrder)
{
    if (f.empty()) return "";
    std::ostringstream w;
    w << " WHERE ";
    bool first = true;
    auto add = [&](const std::string& col) {
        if (!first) w << " AND ";
        first = false;
        w << col << " = ?";
        bindOrder.push_back(col);
        };

    if (f.FlightNumber)      add("FlightNumber");
    if (f.DepartureAirport)  add("DepartureAirport");
    if (f.ArrivalAirport)    add("ArrivalAirport");
    if (f.AircraftType)      add("AircraftType");
    if (f.Status)            add("Status");

    return w.str();
}

static void BindWhere(std::unique_ptr<sql::PreparedStatement>& ps,
    const Flight::Filter& f,
    const std::vector<std::string>& order)
{
    int idx = 1;
    for (const auto& col : order) {
        if (col == "FlightNumber")      ps->setString(idx++, *f.FlightNumber);
        else if (col == "DepartureAirport")  ps->setString(idx++, *f.DepartureAirport);
        else if (col == "ArrivalAirport")    ps->setString(idx++, *f.ArrivalAirport);
        else if (col == "AircraftType")      ps->setString(idx++, *f.AircraftType);
        else if (col == "Status")            ps->setString(idx++, *f.Status);
    }
}


Db* Db::instance_ = nullptr;

//Общедоступные статические
void Db::init(const std::string& url,
              const std::string& user,
              const std::string& password,
              const std::string& schema)
{
    if (instance_) return;
    instance_ = new Db(url, user, password, schema);
}

Db& Db::instance()
{
    if (!instance_) throw std::runtime_error("Db::init() was not called");
    return *instance_;
}

// Конструктор/инфраструктура
Db::Db(const std::string& url,
       const std::string& user,
       const std::string& password,
       const std::string& schema)
    : driver_(nullptr),
      conn_(nullptr),
      url_(url),
      user_(user),
      password_(password),
      schema_(schema)
{
    driver_ = get_driver_instance();
    conn_.reset(driver_->connect(url_, user_, password_));
    conn_->setSchema(schema_);
}

void Db::ensureConnected()
{
    if (!conn_) {
        conn_.reset(driver_->connect(url_, user_, password_));
        conn_->setSchema(schema_);
        return;
    }
    try {
        if (conn_->isClosed()) {
            conn_.reset(driver_->connect(url_, user_, password_));
            conn_->setSchema(schema_);
        }
    } catch (...) {
        // если драйвер бросил — переподключаемся
        conn_.reset(driver_->connect(url_, user_, password_));
        conn_->setSchema(schema_);
    }
}

std::unique_ptr<sql::Statement> Db::createStatement()
{
    std::lock_guard<std::mutex> lock(m_);
    ensureConnected();
    return std::unique_ptr<sql::Statement>(conn_->createStatement());
}

std::unique_ptr<sql::PreparedStatement> Db::prepare(const std::string& sql)
{
    std::lock_guard<std::mutex> lock(m_);
    ensureConnected();
    return std::unique_ptr<sql::PreparedStatement>(conn_->prepareStatement(sql));
}

// ПАССАЖИРЫ: SELECT / UPSERT
std::vector<Passenger> Db::QueryPassengers(const Passenger::Filter& f)
{
    std::lock_guard<std::mutex> lock(m_);
    ensureConnected();

    std::vector<std::string> order;
    std::string where = BuildWhere(f, order);

    std::string sql =
        "SELECT PassengerID, FullName, DocNumber, FlightNumber, "
        "Baggage, Food, SeatNumber, PhoneNumber, Email "
        "FROM passengers" + where + " ORDER BY PassengerID";

    auto ps = std::unique_ptr<sql::PreparedStatement>(conn_->prepareStatement(sql));
    BindWhere(ps, f, order);
    std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());

    std::vector<Passenger> out;
    while (rs->next()) {
        Passenger p;
        p.PASSENGERID  = rs->getString("PassengerID");
        p.FULLNAME     = rs->isNull("FullName") ? "" : rs->getString("FullName");
        p.DOCNUMBER    = rs->isNull("DocNumber") ? "" : rs->getString("DocNumber");
        p.FLIGHTNUMBER = rs->isNull("FlightNumber") ? "" : rs->getString("FlightNumber");
        p.BAGGAGE      = rs->getInt("Baggage") != 0;
        p.FOOD         = rs->getInt("Food") != 0;
        p.SEATNUMBER   = rs->isNull("SeatNumber") ? "" : rs->getString("SeatNumber");
        p.PHONENUMBER  = rs->isNull("PhoneNumber") ? "" : rs->getString("PhoneNumber");
        p.EMAIL        = rs->isNull("Email") ? "" : rs->getString("Email");
        out.push_back(std::move(p));
    }
    return out;
}

void Db::InsertOrUpdatePassenger(const Passenger& p)
{
    std::lock_guard<std::mutex> lock(m_);
    ensureConnected();

    auto up = std::unique_ptr<sql::PreparedStatement>(conn_->prepareStatement(
        "INSERT INTO passengers "
        "(PassengerID, FullName, DocNumber, FlightNumber, Baggage, Food, "
        " SeatNumber, PhoneNumber, Email) "
        "VALUES(?,?,?,?,?,?,?,?,?) "
        "ON DUPLICATE KEY UPDATE "
        " FullName=VALUES(FullName), DocNumber=VALUES(DocNumber), "
        " FlightNumber=VALUES(FlightNumber), Baggage=VALUES(Baggage), "
        " Food=VALUES(Food), SeatNumber=VALUES(SeatNumber), "
        " PhoneNumber=VALUES(PhoneNumber), Email=VALUES(Email)"
    ));

    int i=1;
    up->setString(i++, p.PASSENGERID);
    up->setString(i++, p.FULLNAME);
    up->setString(i++, p.DOCNUMBER);
    up->setString(i++, p.FLIGHTNUMBER);
    up->setInt   (i++, p.BAGGAGE ? 1 : 0);
    up->setInt   (i++, p.FOOD ? 1 : 0);
    up->setString(i++, p.SEATNUMBER);
    up->setString(i++, p.PHONENUMBER);
    up->setString(i++, p.EMAIL);
    up->execute();
}

void Db::InsertOrUpdatePassengers(const std::vector<Passenger>& passengers, bool skipIdentical) {
    std::lock_guard<std::mutex> lock(m_);
    ensureConnected();

    conn_->setAutoCommit(false);
    try {
        // Prepared: UPSERT
        auto up = std::unique_ptr<sql::PreparedStatement>(conn_->prepareStatement(
            "INSERT INTO passengers "
            "(PassengerID, FullName, DocNumber, FlightNumber, Baggage, Food, "
            " SeatNumber, PhoneNumber, Email) "
            "VALUES(?,?,?,?,?,?,?,?,?) "
            "ON DUPLICATE KEY UPDATE "
            " FullName=VALUES(FullName), DocNumber=VALUES(DocNumber), "
            " FlightNumber=VALUES(FlightNumber), Baggage=VALUES(Baggage), "
            " Food=VALUES(Food), SeatNumber=VALUES(SeatNumber), "
            " PhoneNumber=VALUES(PhoneNumber), Email=VALUES(Email)"
        ));

        // Prepared: чтение текущего состояния по ключу
        std::unique_ptr<sql::PreparedStatement> sel;
        if (skipIdentical) {
            sel.reset(conn_->prepareStatement(
                "SELECT PassengerID, FullName, DocNumber, FlightNumber, "
                "Baggage, Food, SeatNumber, PhoneNumber, Email "
                "FROM passengers WHERE PassengerID = ?"
            ));
        }

        for (const auto& p : passengers) {
            if (skipIdentical) {
                sel->setString(1, p.PASSENGERID);
                std::unique_ptr<sql::ResultSet> rs(sel->executeQuery());
                if (rs->next()) {
                    // сравним все поля 1-в-1
                    bool identical =
                        (rs->isNull("FullName") ? p.FULLNAME.empty() : p.FULLNAME == rs->getString("FullName")) &&
                        (rs->isNull("DocNumber") ? p.DOCNUMBER.empty() : p.DOCNUMBER == rs->getString("DocNumber")) &&
                        (rs->isNull("FlightNumber") ? p.FLIGHTNUMBER.empty() : p.FLIGHTNUMBER == rs->getString("FlightNumber")) &&
                        ((rs->getInt("Baggage") != 0) == p.BAGGAGE) &&
                        ((rs->getInt("Food") != 0) == p.FOOD) &&
                        (rs->isNull("SeatNumber") ? p.SEATNUMBER.empty() : p.SEATNUMBER == rs->getString("SeatNumber")) &&
                        (rs->isNull("PhoneNumber") ? p.PHONENUMBER.empty() : p.PHONENUMBER == rs->getString("PhoneNumber")) &&
                        (rs->isNull("Email") ? p.EMAIL.empty() : p.EMAIL == rs->getString("Email"));

                    if (identical) {
                        // полностью совпадает — ничего не делаем
                        continue;
                    }
                }
                // если записи нет — пойдём на UPSERT (он сделает INSERT)
            }

            int i = 1;
            up->setString(i++, p.PASSENGERID);
            up->setString(i++, p.FULLNAME);
            up->setString(i++, p.DOCNUMBER);
            up->setString(i++, p.FLIGHTNUMBER);
            up->setInt(i++, p.BAGGAGE ? 1 : 0);
            up->setInt(i++, p.FOOD ? 1 : 0);
            up->setString(i++, p.SEATNUMBER);
            up->setString(i++, p.PHONENUMBER);
            up->setString(i++, p.EMAIL);
            up->execute();
        }

        conn_->commit();
    }
    catch (...) {
        conn_->rollback();
        conn_->setAutoCommit(true);
        throw;
    }
    conn_->setAutoCommit(true);
}

void Db::DeletePassengers(const std::vector<std::string>& passengerIds)
{
    if (passengerIds.empty())
        return;

    std::lock_guard<std::mutex> lock(m_);
    ensureConnected();

    conn_->setAutoCommit(false);
    try {
        auto ps = std::unique_ptr<sql::PreparedStatement>(
            conn_->prepareStatement(
                "DELETE FROM passengers WHERE PassengerID = ?"
            )
        );

        for (const auto& id : passengerIds) {
            ps->setString(1, id);
            ps->execute();
        }

        conn_->commit();
    }
    catch (...) {
        conn_->rollback();
        conn_->setAutoCommit(true);
        throw;
    }
    conn_->setAutoCommit(true);
}

// ------------------- Passenger / Aircraft helpers -------------------

void Db::InsertOrUpdateFlights(const std::vector<Flight>& flights, bool skipIdentical)
{
    std::lock_guard<std::mutex> lock(m_);
    ensureConnected();

    conn_->setAutoCommit(false);
    try {
        // UPSERT по FlightNumber
        auto up = std::unique_ptr<sql::PreparedStatement>(conn_->prepareStatement(
            "INSERT INTO Flights "
            "(FlightNumber, DepartureAirport, ArrivalAirport, "
            " DepartureTime, ArrivalTime, AircraftType, AvailableSeats, Status) "
            "VALUES (?,?,?,?,?,?,?,?) "
            "ON DUPLICATE KEY UPDATE "
            " DepartureAirport = VALUES(DepartureAirport), "
            " ArrivalAirport  = VALUES(ArrivalAirport), "
            " DepartureTime   = VALUES(DepartureTime), "
            " ArrivalTime     = VALUES(ArrivalTime), "
            " AircraftType    = VALUES(AircraftType), "
            " AvailableSeats  = VALUES(AvailableSeats), "
            " Status          = VALUES(Status)"
        ));

        // Если просили skipIdentical — подготовим селект для сравнения
        std::unique_ptr<sql::PreparedStatement> sel;
        if (skipIdentical) {
            sel.reset(conn_->prepareStatement(
                "SELECT FlightNumber, DepartureAirport, ArrivalAirport, "
                "       DepartureTime, ArrivalTime, AircraftType, "
                "       AvailableSeats, Status "
                "FROM Flights WHERE FlightNumber = ?"
            ));
        }

        for (const auto& f : flights) {
            if (skipIdentical) {
                sel->setString(1, f.FLIGHTNUMBER);
                std::unique_ptr<sql::ResultSet> rs(sel->executeQuery());
                if (rs->next()) {
                    // сравниваем все поля 1-в-1
                    bool identical =
                        (rs->isNull("DepartureAirport")
                            ? f.DEPARTURE_AIRPORT.empty()
                            : f.DEPARTURE_AIRPORT == rs->getString("DepartureAirport")) &&
                        (rs->isNull("ArrivalAirport")
                            ? f.ARRIVAL_AIRPORT.empty()
                            : f.ARRIVAL_AIRPORT == rs->getString("ArrivalAirport")) &&
                        (ErrorHandler::TimePointToString(f.DEPARTURE_TIME)
                            == rs->getString("DepartureTime")) &&
                        (ErrorHandler::TimePointToString(f.ARRIVAL_TIME)
                            == rs->getString("ArrivalTime")) &&
                        (rs->isNull("AircraftType")
                            ? f.AIRCRAFT_TYPE.empty()
                            : f.AIRCRAFT_TYPE == rs->getString("AircraftType")) &&
                        (rs->getInt("AvailableSeats") == f.AVAILABLE_SEATS) &&
                        (rs->isNull("Status")
                            ? f.STATUS.empty()
                            : f.STATUS == rs->getString("Status"));

                    if (identical) {
                        // ничего не изменилось — пропускаем UPSERT
                        continue;
                    }
                }
                // если записи нет — просто пойдём на UPSERT (он сделает INSERT)
            }

            int i = 1;
            up->setString(i++, f.FLIGHTNUMBER);
            up->setString(i++, f.DEPARTURE_AIRPORT);
            up->setString(i++, f.ARRIVAL_AIRPORT);
            up->setString(i++, ErrorHandler::TimePointToString(f.DEPARTURE_TIME));
            up->setString(i++, ErrorHandler::TimePointToString(f.ARRIVAL_TIME));
            up->setString(i++, f.AIRCRAFT_TYPE);
            up->setInt(i++, f.AVAILABLE_SEATS);
            up->setString(i++, f.STATUS);
            up->execute();
        }

        conn_->commit();
    }
    catch (...) {
        conn_->rollback();
        conn_->setAutoCommit(true);
        throw;
    }
    conn_->setAutoCommit(true);
}


std::vector<Flight> Db::QueryFlights(const Flight::Filter& filter)
{
    std::lock_guard<std::mutex> lock(m_);
    ensureConnected();

    std::vector<Flight> out;
    std::vector<std::string> order;
    std::string where = BuildWhere(filter, order);

    std::string sql =
        "SELECT FlightNumber, DepartureAirport, ArrivalAirport, "
        "DepartureTime, ArrivalTime, AircraftType, AvailableSeats, Status "
        "FROM Flights" + where + " ORDER BY FlightNumber";

    auto ps = std::unique_ptr<sql::PreparedStatement>(
        conn_->prepareStatement(sql));
    BindWhere(ps, filter, order);
    std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());

    while (rs->next()) {
        Flight f;

        f.FLIGHTNUMBER = rs->getString("FlightNumber");
        f.DEPARTURE_AIRPORT = rs->getString("DepartureAirport");
        f.ARRIVAL_AIRPORT = rs->getString("ArrivalAirport");
        f.AIRCRAFT_TYPE = rs->getString("AircraftType");
        f.AVAILABLE_SEATS = rs->getInt("AvailableSeats");
        f.STATUS = rs->getString("Status");

        auto parseDT = [](const std::string& s) {
            std::tm tm{};
            std::istringstream ss(s);
            ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
            return std::chrono::system_clock::from_time_t(std::mktime(&tm));
            };

        f.DEPARTURE_TIME = parseDT(rs->getString("DepartureTime"));
        f.ARRIVAL_TIME = parseDT(rs->getString("ArrivalTime"));

        out.push_back(std::move(f));
    }
    return out;
}



std::map<std::string, int> Passenger::LoadOccupiedSeatsByFlight()
{
    std::map<std::string, int> result;

    auto st = Db::instance().createStatement();
    std::unique_ptr<sql::ResultSet> rs(
        st->executeQuery(
            "SELECT FlightNumber, COUNT(*) AS Cnt "
            "FROM Passengers "
            "WHERE SeatNumber IS NOT NULL AND SeatNumber <> '' "
            "GROUP BY FlightNumber"
        ));

    while (rs->next()) {
        std::string fn = rs->getString("FlightNumber");
        int count = rs->getInt("Cnt");
        result[fn] = count;
    }

    return result;
}

//FLIGHT CLASS

bool Flight::IsValidFlightNumberFormat(const std::string& fn)
{
    if (fn.empty()) return false;

    for (char ch : fn) {
        if (!std::isdigit((unsigned char)ch) &&
            !(ch >= 'A' && ch <= 'Z'))
            return false;
    }
    return true;
}

bool Flight::IsValidAirportCode(const std::string& code)
{
    if (code.size() != 3 && code.size() != 4)
        return false;

    for (char ch : code) {
        if (!(ch >= 'A' && ch <= 'Z'))
            return false;
    }
    return true;
}

bool Flight::IsValidRoute() const
{
    return !DEPARTURE_AIRPORT.empty()
        && !ARRIVAL_AIRPORT.empty()
        && DEPARTURE_AIRPORT != ARRIVAL_AIRPORT;
}

bool Flight::IsValidTimes() const
{
    if (DEPARTURE_TIME.time_since_epoch().count() == 0 ||
        ARRIVAL_TIME.time_since_epoch().count() == 0)
        return false;

    return DEPARTURE_TIME != ARRIVAL_TIME;
}

bool Flight::IsValidAircraft() const
{
    return Aircraft::Exists(AIRCRAFT_TYPE);
}

void Flight::Validate() const
{
    if (!IsValidFlightNumberFormat(FLIGHTNUMBER))
        throw std::runtime_error("Invalid FlightNumber: " + FLIGHTNUMBER);

    if (!IsValidAirportCode(DEPARTURE_AIRPORT))
        throw std::runtime_error("Invalid DepartureAirport in flight " + FLIGHTNUMBER);

    if (!IsValidAirportCode(ARRIVAL_AIRPORT))
        throw std::runtime_error("Invalid ArrivalAirport in flight " + FLIGHTNUMBER);

    if (!IsValidRoute())
        throw std::runtime_error("Departure = Arrival in flight " + FLIGHTNUMBER);

    if (!IsValidTimes())
        throw std::runtime_error("DepartureTime == ArrivalTime in flight " + FLIGHTNUMBER);

    if (!IsValidAircraft())
        throw std::runtime_error("Aircraft '" + AIRCRAFT_TYPE + "' not found");
}

// одиночный перерасчёт мест (может ещё где-то использоваться)
void Flight::ComputeAvailableSeats()
{
    auto totalByReg = Aircraft::LoadTotalSeatsByRegistration();
    auto occ = Passenger::LoadOccupiedSeatsByFlight();

    int total = totalByReg[AIRCRAFT_TYPE];
    int occupied = occ.count(FLIGHTNUMBER) ? occ[FLIGHTNUMBER] : 0;

    if (occupied > total)
        throw std::runtime_error("Occupied seats > capacity in flight " + FLIGHTNUMBER);

    AVAILABLE_SEATS = total - occupied;
}

// --------- валидация всего набора рейсов при импорте ---------

static std::string TrimCopy(std::string s)
{
    auto is_space = [](unsigned char ch) {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
        };
    auto it1 = std::find_if_not(s.begin(), s.end(), is_space);
    if (it1 == s.end())
        return {};
    auto it2 = std::find_if_not(s.rbegin(), s.rend(), is_space).base();
    return std::string(it1, it2);
}

void Flight::ValidateFlightsForImport(std::vector<Flight>& flights)
{
    if (flights.empty())
        return;

    // 1. Берём все уже существующие FlightNumber из БД
    std::set<std::string> existing;
    {
        auto st = Db::instance().createStatement();
        std::unique_ptr<sql::ResultSet> rs(
            st->executeQuery("SELECT FlightNumber FROM flights"));
        while (rs->next()) {
            existing.insert(rs->getString("FlightNumber"));
        }
    }

    // 2. Для контроля дублей внутри файла
    std::set<std::string> inFile;

    for (size_t i = 0; i < flights.size(); ++i) {
        Flight& f = flights[i];
        int row = static_cast<int>(i) + 2; // строка в Excel (1 — заголовок)

        // --- нормализация строк ---
        f.FLIGHTNUMBER = TrimCopy(f.FLIGHTNUMBER);
        f.DEPARTURE_AIRPORT = TrimCopy(f.DEPARTURE_AIRPORT);
        f.ARRIVAL_AIRPORT = TrimCopy(f.ARRIVAL_AIRPORT);
        f.AIRCRAFT_TYPE = TrimCopy(f.AIRCRAFT_TYPE);
        f.STATUS = TrimCopy(f.STATUS);

        // коды и номера делаем заглавными (если есть)
        auto toUpper = [](std::string& s) {
            for (char& ch : s)
                ch = static_cast<char>(std::toupper((unsigned char)ch));
            };
        toUpper(f.FLIGHTNUMBER);
        toUpper(f.DEPARTURE_AIRPORT);
        toUpper(f.ARRIVAL_AIRPORT);
        toUpper(f.AIRCRAFT_TYPE);

        if (f.FLIGHTNUMBER.empty()) {
            throw std::runtime_error(
                "Row " + std::to_string(row) +
                ": FlightNumber is required.");
        }

        // --- дубликаты внутри файла ---
        if (!inFile.insert(f.FLIGHTNUMBER).second) {
            throw std::runtime_error(
                "Row " + std::to_string(row) +
                ": FlightNumber '" + f.FLIGHTNUMBER +
                "' is duplicated in import file.");
        }

        // --- уже существует в таблице flights ---
        if (existing.count(f.FLIGHTNUMBER)) {
            throw std::runtime_error(
                "Row " + std::to_string(row) +
                ": FlightNumber '" + f.FLIGHTNUMBER +
                "' already exists in table 'flights'.");
        }

        // --- остальные проверки по ТЗ (формат, коды, время, самолёт) ---
        f.Validate();
    }
}


void Flight::ComputeAvailableSeats(std::vector<Flight>& flights)
{
    if (flights.empty())
        return;

    auto totalByReg = Aircraft::LoadTotalSeatsByRegistration();
    auto occ = Passenger::LoadOccupiedSeatsByFlight();

    for (auto& f : flights) {
        auto itAir = totalByReg.find(f.AIRCRAFT_TYPE);
        if (itAir == totalByReg.end()) {
            throw std::runtime_error(
                "Aircraft '" + f.AIRCRAFT_TYPE +
                "' not found in table 'Aircrafts'.");
        }

        int total = itAir->second;
        int occupied = 0;
        auto itOcc = occ.find(f.FLIGHTNUMBER);
        if (itOcc != occ.end())
            occupied = itOcc->second;

        if (occupied > total) {
            throw std::runtime_error(
                "Occupied seats (" + std::to_string(occupied) +
                ") > capacity (" + std::to_string(total) +
                ") in flight " + f.FLIGHTNUMBER);
        }

        f.AVAILABLE_SEATS = total - occupied;
    }
}


//AIRCRAFT CLASS
std::map<std::string, int> Aircraft::LoadTotalSeatsByRegistration()
{
    std::map<std::string, int> result;

    auto st = Db::instance().createStatement();
    std::unique_ptr<sql::ResultSet> rs(
        st->executeQuery(
            "SELECT RegistrationNumber, TotalSeats FROM Aircrafts"));

    while (rs->next()) {
        std::string reg = rs->getString("RegistrationNumber");
        int t = rs->getInt("TotalSeats");
        result[reg] = t;
    }

    return result;
}

bool Aircraft::Exists(const std::string& registration)
{
    auto map = LoadTotalSeatsByRegistration();
    return map.find(registration) != map.end();
}

int Aircraft::GetTotalSeats(const std::string& registration)
{
    auto map = LoadTotalSeatsByRegistration();
    auto it = map.find(registration);

    if (it == map.end()) {
        throw std::runtime_error(
            "Aircraft '" + registration + "' not found in Aircrafts");
    }
    return it->second;
}
