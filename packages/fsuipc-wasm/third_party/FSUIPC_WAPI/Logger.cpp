///////////////////////////////////////////////////////////////////////////////
// @File Name:     Logger.cpp                                                //
// @Author:        Pankaj Choudhary                                          //
// @Version:       0.0.1                                                     //
// @L.M.D:         13th April 2015                                           //
// @Description:   For Logging into file                                     //
//                                                                           // 
// Detail Description:                                                       //
// Implemented complete logging mechanism, Supporting multiple logging type  //
// like as file based logging, console base logging etc. It also supported   //
// for different log type.                                                   //
//                                                                           //
// Thread Safe logging mechanism. Compatible with VC++ (Windows platform)    //
// as well as G++ (Linux platform)                                           //
//                                                                           //
// Supported Log Type: ERROR, ALARM, ALWAYS, INFO, BUFFER, TRACE, DEBUG      //
//                                                                           //
// No control for ERROR, ALRAM and ALWAYS messages. These type of messages   //
// should be always captured.                                                //
//                                                                           //
// BUFFER log type should be use while logging raw buffer or raw messages    //
//                                                                           //
// Having direct interface as well as C++ Singleton inface. can use          //
// whatever interface want.                                                  //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////
//  Updated 8th February 2021 by John L. Dowson:
//  Changes are
//    - possible to pass in a logging function om constructor to be used when logtype FILE is selected
//    - changed timestamp to get milisecond accuracy
//    - added BOTH logType to log to console and file (or function)
//    - changed log level order
// C++ Header File(s)
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <iomanip>
#include <iostream>


// Code Specific Header Files(s)
#include "Logger.h"

using namespace std;
using namespace CPlusPlusLogging;

Logger* Logger::m_Instance = 0;

Logger::Logger(void (*loggerFunction)(const char* fmt))
{

    this->loggerFunction = loggerFunction;
    m_LogLevel = LOG_LEVEL_INFO;
    m_LogType = FILE_LOG;

    // Initialize mutex
    InitializeCriticalSection(&m_Mutex);
}

Logger::Logger(const char* baseLogFileName)
{
    if (!baseLogFileName)
    {
        m_LogLevel = LOG_LEVEL_INFO;
        m_LogType = CONSOLE;
        loggerFunction = nullptr;
        return;
    }

    m_logFileName = std::string(baseLogFileName) + ".log";
    if (existsFile(m_logFileName)) {
        // Remove any existing previous file
        const string logFileNamePrev = std::string(baseLogFileName) + "_prev.log";
        if (existsFile(logFileNamePrev)) std::remove(logFileNamePrev.c_str());

        std::rename(m_logFileName.c_str(), logFileNamePrev.c_str());
    }
    m_File.open(m_logFileName.c_str(), ios::out|ios::app);
    m_LogLevel	= LOG_LEVEL_INFO;
    m_LogType	= FILE_LOG;
    loggerFunction = nullptr;
    // Initialize mutex
    InitializeCriticalSection(&m_Mutex);

}

Logger::~Logger()
{
   m_File.close();
   DeleteCriticalSection(&m_Mutex);
}

Logger* Logger::getInstance(const char* text) throw ()
{
   if (m_Instance == 0) 
   {
      m_Instance = new Logger(text);
   }
   return m_Instance;
}
Logger* Logger::getInstance(void (*loggerFunction)(const char* fmt)) throw ()
{
    if (m_Instance == 0)
    {
        m_Instance = new Logger(loggerFunction);
    }
    return m_Instance;
}
void Logger::setLoggerFunction(void (*loggerFunction)(const char* fmt))
{
    this->loggerFunction = loggerFunction;
}

Logger* Logger::getInstance() throw ()
{
    if (m_Instance == 0)
    {
        m_Instance = new Logger((char*)nullptr);
    }
    return m_Instance;
}
void Logger::lock()
{
    EnterCriticalSection(&m_Mutex);
}

void Logger::unlock()
{
    LeaveCriticalSection(&m_Mutex);
}

void Logger::logIntoFile(std::string& data)
{
    if (loggerFunction != nullptr) {
        lock();
        loggerFunction(data.c_str());
        unlock();
    }
    else {
        lock();
        m_File << getCurrentTime() << "  " << data << endl;
        unlock();
    }
}

void Logger::logOnConsole(std::string& data)
{
    cout << getCurrentTime() << "  " << data << endl;
//    cerr << getCurrentTime() << "  " << data << endl;
}

string Logger::getCurrentTime()
{
   // get a precise timestamp as a string
    const auto now = std::chrono::system_clock::now();
    const auto nowAsTimeT = std::chrono::system_clock::to_time_t(now);
    const auto nowMs = (std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())).count() % 1000;
    std::stringstream nowSs;
    nowSs
        << std::put_time(std::localtime(&nowAsTimeT), "%a %b %d %Y %T")
        << '.' << std::setfill('0') << std::setw(3) << nowMs;
    return nowSs.str();

}

// Interface for Error Log
void Logger::error(const char* text) throw()
{
   string data;
   data.append(" [ERROR]: ");
   data.append(text);

   // ERROR must be capture
   if(m_LogType == FILE_LOG || m_LogType == BOTH_LOG)
   {
      logIntoFile(data);
   }
   if(m_LogType == CONSOLE || m_LogType == BOTH_LOG)
   {
      logOnConsole(data);
   }
}

void Logger::error(std::string& text) throw()
{
   error(text.data());
}

void Logger::error(std::ostringstream& stream) throw()
{
   string text = stream.str();
   error(text.data());
}

// Interface for Alarm Log 
void Logger::alarm(const char* text) throw()
{
   string data;
   data.append(" [ALARM]: ");
   data.append(text);

   // ALARM must be capture
   if(m_LogType == FILE_LOG || m_LogType == BOTH_LOG)
   {
      logIntoFile(data);
   }
   if(m_LogType == CONSOLE || m_LogType == BOTH_LOG)
   {
      logOnConsole(data);
   }
}

void Logger::alarm(std::string& text) throw()
{
   alarm(text.data());
}

void Logger::alarm(std::ostringstream& stream) throw()
{
   string text = stream.str();
   alarm(text.data());
}

// Interface for Always Log 
void Logger::always(const char* text) throw()
{
   string data;
   data.append("[ALWAYS]: ");
   data.append(text);

   // No check for ALWAYS logs
   if(m_LogType == FILE_LOG || m_LogType == BOTH_LOG)
   {
      logIntoFile(data);
   }
   if(m_LogType == CONSOLE || m_LogType == BOTH_LOG)
   {
      logOnConsole(data);
   }
}

void Logger::always(std::string& text) throw()
{
   always(text.data());
}

void Logger::always(std::ostringstream& stream) throw()
{
   string text = stream.str();
   always(text.data());
}

// Interface for Buffer Log 
void Logger::buffer(const char* text) throw()
{
   // Buffer is the special case. So don't add log level
   // and timestamp in the buffer message. Just log the raw bytes.
   if((m_LogType == FILE_LOG || m_LogType == BOTH_LOG) && (m_LogLevel >= LOG_LEVEL_BUFFER))
   {
      lock();
      m_File << text << endl;
      unlock();
   }
   if((m_LogType == CONSOLE || m_LogType == BOTH_LOG) && (m_LogLevel >= LOG_LEVEL_BUFFER))
   {
      cout << text << endl;
   }
}

void Logger::buffer(std::string& text) throw()
{
   buffer(text.data());
}

void Logger::buffer(std::ostringstream& stream) throw()
{
   string text = stream.str();
   buffer(text.data());
}

// Interface for Info Log
void Logger::info(const char* text) throw()
{
   string data;
   data.append("  [INFO]: ");
   data.append(text);

   if((m_LogType == FILE_LOG || m_LogType == BOTH_LOG) && (m_LogLevel >= LOG_LEVEL_INFO))
   {
      logIntoFile(data);
   }
   if((m_LogType == CONSOLE || m_LogType == BOTH_LOG) && (m_LogLevel >= LOG_LEVEL_INFO))
   {
      logOnConsole(data);
   }
}

void Logger::info(std::string& text) throw()
{
   info(text.data());
}

void Logger::info(std::ostringstream& stream) throw()
{
   string text = stream.str();
   info(text.data());
}

// Interface for Trace Log
void Logger::trace(const char* text) throw()
{
   string data;
   data.append(" [TRACE]: ");
   data.append(text);

   if((m_LogType == FILE_LOG || m_LogType == BOTH_LOG) && (m_LogLevel >= LOG_LEVEL_TRACE))
   {
      logIntoFile(data);
   }
   if((m_LogType == CONSOLE || m_LogType == BOTH_LOG) && (m_LogLevel >= LOG_LEVEL_TRACE))
   {
      logOnConsole(data);
   }
}

void Logger::trace(std::string& text) throw()
{
   trace(text.data());
}

void Logger::trace(std::ostringstream& stream) throw()
{
   string text = stream.str();
   trace(text.data());
}

// Interface for Debug Log
void Logger::debug(const char* text) throw()
{
   string data;
   data.append(" [DEBUG]: ");
   data.append(text);

   if((m_LogType == FILE_LOG || m_LogType == BOTH_LOG) && (m_LogLevel >= LOG_LEVEL_DEBUG))
   {
      logIntoFile(data);
   }
   if((m_LogType == CONSOLE || m_LogType == BOTH_LOG) && (m_LogLevel >= LOG_LEVEL_DEBUG))
   {
      logOnConsole(data);
   }
}

void Logger::debug(std::string& text) throw()
{
   debug(text.data());
}

void Logger::debug(std::ostringstream& stream) throw()
{
   string text = stream.str();
   debug(text.data());
}

// Interfaces to control log levels
void Logger::updateLogLevel(LogLevel logLevel)
{
   m_LogLevel = logLevel;
}

// Enable all log levels
void Logger::enaleLog()
{
   m_LogLevel = ENABLE_LOG; 
}

// Disable all log levels, except error and alarm
void Logger:: disableLog()
{
   m_LogLevel = DISABLE_LOG;
}

// Interfaces to control log Types
void Logger::updateLogType(LogType logType)
{
   m_LogType = logType;
}

void Logger::enableConsoleLogging()
{
   m_LogType = CONSOLE; 
}

void Logger::enableFileLogging()
{
   m_LogType = FILE_LOG ;
}

void Logger::enableBothLogging()
{
    m_LogType = BOTH_LOG;
}

bool Logger::existsFile(const std::string& name) {
    if (FILE* file = std::fopen(name.c_str(), "r")) {
        fclose(file);
        return true;
    }
    else {
        return false;
    }
}
