#include <wx/wx.h>
#include <wx/sizer.h>
#include <wx/html/htmlwin.h>    
#include "Header.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include "Header.h"
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <thread>
#include <algorithm>   
#include "WeatherForecast.h"    

using json = nlohmann::json;


static size_t WriteCB(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    auto* s = static_cast<std::string*>(userp);
    s->append(static_cast<char*>(contents), total);
    return total;
}

static bool HttpGet(const std::string& url, std::string& out, std::string& err) {
    CURL* curl = curl_easy_init();
    if (!curl) { err = "curl init failed"; return false; }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCB);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) { err = curl_easy_strerror(rc); curl_easy_cleanup(curl); return false; }
    curl_easy_cleanup(curl);
    return true;
}


template<class T>
T jget(const json& j, const std::string& key, T def = T()) {
    if (j.contains(key) && !j[key].is_null()) return j[key].get<T>();
    return def;
}

static std::string degToCompass(int deg) {
    static const char* dir16[] = { "N","NNE","NE","ENE","E","ESE","SE","SSE",
                                   "S","SSW","SW","WSW","W","WNW","NW","NNW" };
    int idx = int(std::round(((deg % 360) / 22.5))) % 16;
    return dir16[idx];
}

static std::string fmtVis(double meters) {
    double km = meters / 1000.0;
    double sm = meters / 1609.344;
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << km << " km (" << sm << " sm)";
    return oss.str();
}

static std::string toLowerAscii(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}


static std::string BuildForecastUrl(double lat, double lon) {
    std::ostringstream u;
    u << "https://api.open-meteo.com/v1/forecast?latitude=" << lat
        << "&longitude=" << lon
        << "&current=temperature_2m,dew_point_2m,relative_humidity_2m,pressure_msl,"
        "wind_speed_10m,wind_gusts_10m,wind_direction_10m,weathercode,visibility"
        << "&hourly=temperature_2m,dew_point_2m,relative_humidity_2m,pressure_msl,"
        "wind_speed_10m,wind_gusts_10m,wind_direction_10m,weathercode,visibility,precipitation"
        << "&daily=sunrise,sunset,uv_index_max,temperature_2m_max,temperature_2m_min"
        << "&timezone=auto";
    return u.str();
}

static std::string WeatherCodeToStr(int code) {
    switch (code) {
    case 0:  return "Clear";
    case 1:  return "Mostly clear";
    case 2:  return "Partly cloudy";
    case 3:  return "Overcast";
    case 45: return "Fog/mist";
    case 48: return "Rime fog";
    case 51: return "Light drizzle";
    case 53: return "Moderate drizzle";
    case 55: return "Heavy drizzle";
    case 56: return "Light freezing drizzle";
    case 57: return "Heavy freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Moderate rain";
    case 65: return "Heavy rain";
    case 66: return "Light freezing rain";
    case 67: return "Heavy freezing rain";
    case 71: return "Light snow";
    case 73: return "Moderate snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: return "Light rain showers";
    case 81: return "Moderate rain showers";
    case 82: return "Heavy rain showers";
    case 85: return "Light snow showers";
    case 86: return "Heavy snow showers";
    case 95: return "Thunderstorm";
    case 96: return "TS + light hail";
    case 99: return "TS + heavy hail";
    default: return "Unknown";
    }
}


static std::string HtmlEscape(const std::string& s) {
    std::string r; r.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
        case '&': r += "&amp;"; break;
        case '<': r += "&lt;";  break;
        case '>': r += "&gt;";  break;
        case '"': r += "&quot;"; break;
        default:  r.push_back(c);
        }
    }
    return r;
}

static std::string FormatWeatherHtml(const std::string& titleUtf8,
    const std::string& bodyJson,
    std::string& err)
{
    json j;
    try { j = json::parse(bodyJson); }
    catch (const std::exception& e) { err = std::string("Forecast JSON error: ") + e.what(); return {}; }

    const char* css =
        "body{font-family:Segoe UI,Arial,sans-serif;margin:12px;color:#0a0a0a}"
        "h2{margin:0 0 8px 0;font-size:18px}"
        ".card{border:1px solid #e5e5e5;border-radius:10px;padding:12px;margin:0 0 12px 0;box-shadow:0 1px 2px rgba(0,0,0,.03)}"
        "table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}"
        "th,td{border-bottom:1px solid #eee;padding:6px 8px;text-align:left}"
        "th{background:#fafafa}"
        ".muted{color:#666}"
        ".bar{display:inline-block;height:10px;background:#5b8def;border-radius:5px;vertical-align:middle}"
        ".bar-wind{background:#20b27a}"
        ".chip{display:inline-block;background:#eef3ff;color:#2c5bd9;border-radius:8px;padding:2px 8px;margin-left:6px;font-size:12px}";

    std::ostringstream out;
    out << "<!doctype html><html><head>"
        << "<meta charset='utf-8'><style>" << css << "</style></head><body>";


    out << "<div class='card'><h2>" << HtmlEscape(titleUtf8) << "</h2>";
    out << "<div class='muted'>Lat: " << jget<double>(j, "latitude")
        << " &nbsp; Lon: " << jget<double>(j, "longitude")
        << " &nbsp; Elev: " << jget<double>(j, "elevation") << " m"
        << " &nbsp; <span class='chip'>"
        << HtmlEscape(jget<std::string>(j, "timezone")) << " ("
        << HtmlEscape(jget<std::string>(j, "timezone_abbreviation")) << ")</span></div>";

    if (j.contains("current")) {
        auto c = j["current"];
        int wdir = jget<int>(c, "wind_direction_10m");
        int wcode = jget<int>(c, "weathercode");

        out << "<div style='display:flex;gap:12px;flex-wrap:wrap;margin-top:8px'>"
            << "<div class='card' style='flex:1 1 260px'>"
            << "<div><b>Time:</b> " << HtmlEscape(jget<std::string>(c, "time")) << "</div>"
            << "<div><b>Temp:</b> " << jget<double>(c, "temperature_2m") << " °C"
            << " &nbsp; <b>Dew:</b> " << jget<double>(c, "dew_point_2m") << " °C"
            << " &nbsp; <b>RH:</b> " << jget<int>(c, "relative_humidity_2m") << " %</div>"
            << "<div><b>MSL:</b> " << jget<double>(c, "pressure_msl") << " hPa</div>"
            << "<div><b>Wind:</b> " << jget<double>(c, "wind_speed_10m") << " m/s"
            << " &nbsp; <b>Gust:</b> " << jget<double>(c, "wind_gusts_10m") << " m/s"
            << " &nbsp; <b>Dir:</b> " << wdir << "° (" << HtmlEscape(degToCompass(wdir)) << ")</div>"
            << "<div><b>Vis:</b> " << HtmlEscape(fmtVis(jget<double>(c, "visibility"))) << "</div>"
            << "<div><b>Wx:</b> " << HtmlEscape(WeatherCodeToStr(wcode)) << " (code " << wcode << ")</div>"
            << "</div>";
    }

    if (j.contains("hourly")) {
        auto h = j["hourly"];
        const auto& time = h["time"], & t2m = h["temperature_2m"], & dpt = h["dew_point_2m"],
            & rh = h["relative_humidity_2m"], & pmsl = h["pressure_msl"],
            & wspd = h["wind_speed_10m"], & wgst = h["wind_gusts_10m"],
            & wdir = h["wind_direction_10m"], & wcod = h["weathercode"],
            & vis = h["visibility"], & prcp = h["precipitation"];

        size_t n = std::min<size_t>(12, time.size()); 

        double tmax = -1e9, wsmax = -1e9;
        for (size_t i = 0; i < n; ++i) { tmax = std::max(tmax, t2m[i].get<double>()); wsmax = std::max(wsmax, wspd[i].get<double>()); }
        if (tmax <= 0) tmax = 1; if (wsmax <= 0) wsmax = 1;

        out << "<div class='card' style='flex:4 1 460px'>"
            << "<div style='font-weight:600;margin:2px 0 6px 0'>Hourly (next 12 h)</div>"
            << "<table><thead><tr>"
            << "<th style='width:120px'>Time</th><th>T (°C)</th><th>Td (°C)</th><th>RH%</th>"
            << "<th>P (hPa)</th><th>Wind</th><th>Gust</th><th>Dir</th><th>Vis (km)</th><th>Prcp</th><th>Wx</th>"
            << "</tr></thead><tbody>";

        for (size_t i = 0; i < n; ++i) {
            double t = t2m[i].get<double>();
            double ws = wspd[i].get<double>();
            double g = wgst[i].get<double>();
            double viskm = vis[i].get<double>() / 1000.0;
            int dir = wdir[i].get<int>();
            int wxc = wcod[i].get<int>();
            int barT = int((std::max(0.0, t) / tmax) * 120);   
            int barWs = int((std::max(0.0, ws) / wsmax) * 120);  

            out << "<tr>"
                << "<td>" << HtmlEscape(time[i].get<std::string>()) << "</td>"
                << "<td>" << t << " <span class='bar' style='width:" << barT << "px'></span></td>"
                << "<td>" << dpt[i].get<double>() << "</td>"
                << "<td>" << rh[i].get<int>() << "</td>"
                << "<td>" << pmsl[i].get<double>() << "</td>"
                << "<td>" << ws << " <span class='bar bar-wind' style='width:" << barWs << "px'></span></td>"
                << "<td>" << g << "</td>"
                << "<td>" << dir << "°</td>"
                << "<td>" << std::fixed << std::setprecision(1) << viskm << "</td>"
                << "<td>" << prcp[i].get<double>() << "</td>"
                << "<td>" << HtmlEscape(WeatherCodeToStr(wxc)) << "</td>"
                << "</tr>";
        }
        out << "</tbody></table></div>";
        out << "</div>";
    }

    out << "</div>";
    out << "</body></html>";
    return out.str();
}


class WeatherFrame : public wxFrame {
public:
    WeatherFrame(wxWindow* parent = nullptr)
        : wxFrame(parent, wxID_ANY, "Pilot Weather (Open-Meteo)", wxDefaultPosition, wxSize(920, 720)) {

        auto* panel = new wxPanel(this);

        depEdit = new wxTextCtrl(panel, wxID_ANY, "JFK");
        arrEdit = new wxTextCtrl(panel, wxID_ANY, "LGA");
        auto* btn = new wxButton(panel, wxID_ANY, "Get weather");

        output = new wxHtmlWindow(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize);
        output->SetMinSize(wxSize(-1, 560));

        auto* top = new wxBoxSizer(wxVERTICAL);
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(panel, wxID_ANY, "Departure:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        row->Add(depEdit, 1, wxRIGHT, 10);
        row->Add(new wxStaticText(panel, wxID_ANY, "Arrival:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        row->Add(arrEdit, 1, wxRIGHT, 10);
        row->Add(btn, 0);

        top->Add(row, 0, wxEXPAND | wxALL, 10);
        top->Add(output, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
        panel->SetSizer(top);

        btn->Bind(wxEVT_BUTTON, &WeatherFrame::OnGetWeather, this);

        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~WeatherFrame() override { curl_global_cleanup(); }

private:
    wxTextCtrl* depEdit{};
    wxTextCtrl* arrEdit{};
    wxHtmlWindow* output{};

    void OnGetWeather(wxCommandEvent&) {
        output->SetPage("<html><body style='font-family:Segoe UI,Arial,sans-serif;margin:12px'>"
            "<div style='color:#666'>Fetching...</div></body></html>");

        const std::string dep = depEdit->GetValue().ToStdString();
        const std::string arr = arrEdit->GetValue().ToStdString();

        std::thread([this, dep, arr]() {
            auto do_one = [this](const std::string& label,
                const std::string& q,
                std::string& block) -> std::string
                {
                    double lat = 0, lon = 0;
                    std::string pretty, err;

                    if (!ResolvePlace(q, lat, lon, pretty, err))
                        return label + ": " + err + "<br>";

                    std::string url = BuildForecastUrl(lat, lon);
                    std::string body;
                    if (!HttpGet(url, body, err))
                        return label + ": " + err + "<br>";

                    std::string ferr;
                    block = FormatWeatherHtml(label + u8" • " + pretty, body, ferr);
                    if (block.empty())
                        return label + ": " + ferr + "<br>";

                    return {};
                };

            std::string htmlAll, blk;

            if (auto e = do_one("DEPARTURE", dep, blk); !e.empty())
                htmlAll += "<div class='card'>" + e + "</div>";
            else
                htmlAll += blk;

            if (auto e = do_one("ARRIVAL", arr, blk); !e.empty())
                htmlAll += "<div class='card'>" + e + "</div>";
            else
                htmlAll += blk;

            CallAfter([this, htmlAll]() {
                output->SetPage(wxString::FromUTF8(htmlAll));
                });
            }).detach();
    }


    bool ChooseAirportFromDb(const std::string& query,
        Airport& chosen,
        std::string& err)
    {
        auto resOpt = ErrorHandler::ExecOrError([&] {
            return Airport::Search(query);       
            }, err );

        if (!resOpt) return false;             
        auto& res = *resOpt;

        if (res.empty()) { err = "No airports found."; return false; }
        if (res.size() == 1) { chosen = res.front(); return true; }

        wxArrayString items;
        items.reserve(res.size());
        for (const auto& a : res) {
            items.Add(wxString::Format("%s (%s/%s) - %s, %s",
                wxString::FromUTF8(a.name),
                wxString::FromUTF8(a.iata),
                wxString::FromUTF8(a.icao),
                wxString::FromUTF8(a.city),
                wxString::FromUTF8(a.country)));
        }

        wxSingleChoiceDialog dlg(this,
            "Select an airport",
            "Multiple matches",
            items);

        if (dlg.ShowModal() != wxID_OK) { err = "Selection cancelled."; return false; }
        chosen = res[dlg.GetSelection()];
        return true;
    }

    bool ResolvePlace(const std::string& query,
        double& lat, double& lon,
        std::string& pretty, std::string& err)
    {
        Airport a;
        if (!ChooseAirportFromDb(query, a, err))
            return false;

        lat = a.latitude;
        lon = a.longitude;
        pretty = a.name + " (" + a.iata + (a.icao.empty() ? "" : "/" + a.icao) + "), "
            + a.city + ", " + a.country;
        return true;
    }
};

void ShowWeatherFrame(wxWindow* parent) {
    (new WeatherFrame(parent))->Show();
}