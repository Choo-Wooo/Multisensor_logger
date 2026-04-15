#ifndef INC_NMEA_EXPORT_H
#define INC_NMEA_EXPORT_H

#ifdef _WIN32
    #ifdef NMEA_BUILD_DLL
        /* Building the DLL */
        #define NMEA_API __declspec(dllexport)
    #elif defined(NMEA_DLL)
        /* Using the DLL */
        #define NMEA_API __declspec(dllimport)
    #else
        /* Static library */
        #define NMEA_API
    #endif
#else
    /* Non-Windows */
    #define NMEA_API
#endif

#endif  /* INC_NMEA_EXPORT_H */
