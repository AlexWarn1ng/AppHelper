#include "Header.h"
#include <stdexcept>
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

// ------------------- Passenger / Aircraft helpers -------------------

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

std::map<std::string, int> Aircraft::LoadTotalSeatsByRegistration()
{
    std::map<std::string, int> result;

    auto st = Db::instance().createStatement();
    std::unique_ptr<sql::ResultSet> rs(
        st->executeQuery(
            "SELECT RegistrationNumber, TotalSeats FROM Aircrafts"));

    while (rs->next()) {
        std::string reg = rs->getString("RegistrationNumber");
        int         t = rs->getInt("TotalSeats");
        result[reg] = t;
    }

    return result;
}
