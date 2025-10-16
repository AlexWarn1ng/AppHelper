#pragma once
#include <stddef.h>

#ifdef _WIN32
#ifdef SERVICE_EXPORTS
#define SERVICE_API __declspec(dllexport)
#else
#define SERVICE_API __declspec(dllimport)
#endif
#else
#define SERVICE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct Pt {
        double lat;
        double lon;
        const char* pretty;  // govno udalit potom
    } Pt;

    SERVICE_API int    Service_FindAirport(const char* code, Pt* out_pt);
    SERVICE_API size_t Service_AirportCount(void);
    SERVICE_API int    Service_GetAirportAt(size_t index, const Pt** out_pt, const char** out_code);

#ifdef __cplusplus
}
#endif
