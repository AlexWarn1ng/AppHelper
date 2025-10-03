#include "Header.h"
#include <stdexcept>

Db* Db::instance_ = nullptr; 

Db::Db(const std::string& url,
    const std::string& user,
    const std::string& password,
    const std::string& schema)
    : driver_(get_driver_instance()),
    url_(url), user_(user), password_(password), schema_(schema)
{
    ensureConnected();
}

void Db::init(const std::string& url,
    const std::string& user,
    const std::string& password,
    const std::string& schema)
{
    if (!instance_) {
        instance_ = new Db(url, user, password, schema);
    }
}

Db& Db::instance() {
    if (!instance_) throw std::runtime_error("Db::init() not called");
    return *instance_;
}

void Db::ensureConnected() {
    if (conn_ && conn_->isValid()) return;
    conn_ = std::unique_ptr<sql::Connection>(
        driver_->connect(url_, user_, password_));
    conn_->setSchema(schema_);
}

std::unique_ptr<sql::Statement> Db::createStatement() {
    std::lock_guard<std::mutex> lock(m_);
    ensureConnected();
    return std::unique_ptr<sql::Statement>(conn_->createStatement());
}

std::unique_ptr<sql::PreparedStatement> Db::prepare(const std::string& sql) {
    std::lock_guard<std::mutex> lock(m_);
    ensureConnected();
    return std::unique_ptr<sql::PreparedStatement>(conn_->prepareStatement(sql));
}
