// Stream based logging mechanism.
//
// Written by Bernie Bright, 1998
//
// Copyright (C) 1998  Bernie Bright - bbright@c031.aone.net.au
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Library General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Library General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//
// $Id$

#include <simgear_config.h>

#include "logstream.hxx"

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

#include <boost/foreach.hpp>

#include <simgear/sg_inlines.h>
#include <simgear/threads/SGThread.hxx>
#include <simgear/threads/SGQueue.hxx>
#include <simgear/threads/SGGuard.hxx>

#include <simgear/io/iostreams/sgstream.hxx>
#include <simgear/misc/sg_path.hxx>
#include <simgear/timing/timestamp.hxx>

#if defined (SG_WINDOWS)
// for AllocConsole, OutputDebugString
    #include <windows.h>
    #include <fcntl.h>
    #include <io.h>
#endif



//////////////////////////////////////////////////////////////////////////////

namespace simgear
{

LogCallback::LogCallback(sgDebugClass c, sgDebugPriority p) :
	m_class(c),
	m_priority(p)
{
}

bool LogCallback::shouldLog(sgDebugClass c, sgDebugPriority p) const
{
    
    if ((c & m_class) != 0 && p >= m_priority)
        return true;
    if (c == SG_OSG) // always have OSG logging as it OSG logging is configured separately.
        return true;
    return false;
}

void LogCallback::setLogLevels( sgDebugClass c, sgDebugPriority p )
{
	m_priority = p;
	m_class = c;
}
const char* LogCallback::debugPriorityToString(sgDebugPriority p)
{
    switch (p) {
    case SG_ALERT: return "ALRT";
    case SG_BULK:  return "BULK";
    case SG_DEBUG: return "DBUG";
    case SG_INFO:  return "INFO";
    case SG_POPUP: return "POPU";
    case SG_WARN:  return "WARN";
    default: return "UNKN";
    }
}

const char* LogCallback::debugClassToString(sgDebugClass c)
{
    switch (c) {
        case SG_NONE:       return "none";
        case SG_TERRAIN:    return "terrain";
        case SG_ASTRO:      return "astro";
        case SG_FLIGHT:     return "flight";
        case SG_INPUT:      return "input";
        case SG_GL:         return "opengl";
        case SG_VIEW:       return "view";
        case SG_COCKPIT:    return "cockpit";
        case SG_GENERAL:    return "general";
        case SG_MATH:       return "math";
        case SG_EVENT:      return "event";
        case SG_AIRCRAFT:   return "aircraft";
        case SG_AUTOPILOT:  return "autopilot";
        case SG_IO:         return "io";
        case SG_CLIPPER:    return "clipper";
        case SG_NETWORK:    return "network";
        case SG_ATC:        return "atc";
        case SG_NASAL:      return "nasal";
        case SG_INSTR:      return "instruments";
        case SG_SYSTEMS:    return "systems";
        case SG_AI:         return "ai";
        case SG_ENVIRONMENT:return "environment";
        case SG_SOUND:      return "sound";
        case SG_NAVAID:     return "navaid";
        case SG_GUI:        return "gui";
        case SG_TERRASYNC:  return "terrasync";
        case SG_PARTICLES:  return "particles";
        case SG_HEADLESS:   return "headless";
        case SG_OSG:        return "OSG";
        default:            return "unknown";
    }
}

} // of namespace simgear

//////////////////////////////////////////////////////////////////////////////

class FileLogCallback : public simgear::LogCallback
{
public:
    SGTimeStamp logTimer;
    FileLogCallback(const SGPath& aPath, sgDebugClass c, sgDebugPriority p) :
	    simgear::LogCallback(c, p)
    {
        m_file.open(aPath, std::ios_base::out | std::ios_base::trunc);
        logTimer.stamp();
    }

    virtual void operator()(sgDebugClass c, sgDebugPriority p,
        const char* file, int line, const std::string& message)
    {
        if (!shouldLog(c, p)) return;


//        fprintf(stderr, "%7.2f [%.8s]:%-10s %s\n", logTimer.elapsedMSec() / 1000.0, debugPriorityToString(p), debugClassToString(c), aMessage.c_str());
        m_file
            << std::fixed
            << std::setprecision(2)
            << std::setw(8)
            << std::right
            << (logTimer.elapsedMSec() / 1000.0)
            << std::setw(8)
            << std::left
            << " ["+std::string(debugPriorityToString(p))+"]:"
            << std::setw(10)
            << std::left
            << debugClassToString(c)
            << " " 
            << file 
            << ":" 
            << line 
            << ":" 
            << message << std::endl;
        //m_file << debugClassToString(c) << ":" << (int)p
        //    << ":" << file << ":" << line << ":" << message << std::endl;
    }
private:
    sg_ofstream m_file;
};

class StderrLogCallback : public simgear::LogCallback
{
public:
    SGTimeStamp logTimer;

    StderrLogCallback(sgDebugClass c, sgDebugPriority p) :
		simgear::LogCallback(c, p)
    {
        logTimer.stamp();
    }

#if defined (SG_WINDOWS)
    ~StderrLogCallback()
    {
        FreeConsole();
    }
#endif

    virtual void operator()(sgDebugClass c, sgDebugPriority p,
        const char* file, int line, const std::string& aMessage)
    {
        if (!shouldLog(c, p)) return;
        //fprintf(stderr, "%s\n", aMessage.c_str());
        
        if (file && line != -1) {
            fprintf(stderr, "%8.2f %s:%i: [%.8s]:%-10s %s\n", logTimer.elapsedMSec()/1000.0, file, line, debugPriorityToString(p), debugClassToString(c), aMessage.c_str());
        }
        else {
            fprintf(stderr, "%8.2f [%.8s]:%-10s %s\n", logTimer.elapsedMSec()/1000.0, debugPriorityToString(p), debugClassToString(c), aMessage.c_str());
        }
        //    file, line, aMessage.c_str());
        //fprintf(stderr, "%s:%d:%s:%d:%s\n", debugClassToString(c), p,
        //    file, line, aMessage.c_str());
        fflush(stderr);
    }
};


#ifdef SG_WINDOWS

class WinDebugLogCallback : public simgear::LogCallback
{
public:
    WinDebugLogCallback(sgDebugClass c, sgDebugPriority p) :
		simgear::LogCallback(c, p)
    {
    }

    virtual void operator()(sgDebugClass c, sgDebugPriority p,
        const char* file, int line, const std::string& aMessage)
    {
        if (!shouldLog(c, p)) return;

        std::ostringstream os;
		os << debugClassToString(c) << ":" << aMessage << std::endl;
		OutputDebugStringA(os.str().c_str());
    }
};

#endif

class logstream::LogStreamPrivate : public SGThread
{
private:
    /**
     * storage of a single log entry. This is used to pass log entries from
     * the various threads to the logging thread, and also to store the startup
     * entries
     */
    class LogEntry
    {
    public:
        LogEntry(sgDebugClass c, sgDebugPriority p,
            const char* f, int l, const std::string& msg) :
            debugClass(c), debugPriority(p), file(f), line(l),
                message(msg)
        {
        }

        const sgDebugClass debugClass;
        const sgDebugPriority debugPriority;
        const char* file;
        const int line;
        const std::string message;
    };

    /**
     * RAII object to pause the logging thread if it's running, and restart it.
     * used to safely make configuration changes.
     */
    class PauseThread
    {
    public:
        PauseThread(LogStreamPrivate* parent)
            : m_parent(parent)
            , m_wasRunning(m_parent->stop())
        {
        }

        ~PauseThread()
        {
            if (m_wasRunning) {
                m_parent->startLog();
            }
        }
    private:
        LogStreamPrivate* m_parent;
        const bool m_wasRunning;
    };

public:
    LogStreamPrivate() :
        m_logClass(SG_ALL),
        m_logPriority(SG_ALERT)
    {
#if defined (SG_WINDOWS)
        /*
         * 2016-09-20(RJH) - Reworked console handling
         * 1) When started from the console use the console (when no --console)
         * 2) When started from the GUI (with --console) open a new console window
         * 3) When started from the GUI (without --console) don't open a new console
         *    window; stdout/stderr will not appear (except in logfiles as they do now)
         * 4) When started from the Console (with --console) open a new console window
         * 5) Ensure that IO redirection still works when started from the console
         * 
         * Notes:
         * 1) fgfs needs to be a GUI subsystem app - which it already is
         * 2) What can't be done is to make the cmd prompt run fgfs synchronously; 
         * this is only something that can be done via "start /wait fgfs".
         */

        int stderr_handle_type = GetFileType(GetStdHandle(STD_ERROR_HANDLE));
        int stdout_handle_type = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
        int stdout_isNull = 0;
        int stderr_isNull = 0;

        m_stderr_isRedirectedAlready = stderr_handle_type == FILE_TYPE_DISK || stderr_handle_type == FILE_TYPE_PIPE || stderr_handle_type == FILE_TYPE_CHAR;
        m_stdout_isRedirectedAlready = stdout_handle_type == FILE_TYPE_DISK || stdout_handle_type == FILE_TYPE_PIPE || stdout_handle_type == FILE_TYPE_CHAR;

        /*
         * We don't want to attach to the console if either stream has been redirected - so in this case ensure that both streams
         * are redirected as otherwise something will be lost (as Alloc or Attach Console will cause the handles that were bound
         * to disappear)
         */
        if (m_stdout_isRedirectedAlready){
			if (!m_stderr_isRedirectedAlready) {
				MessageBox(0, "Redirection only works when you use 2>&1 before using > or |\r\n(e.g. fgfs 2>&1 | more)", "Simgear Error", MB_OK | MB_ICONERROR);
				exit(3);
			}
        } else {
            /*
            * Attempt to attach to the console process of the parent process; when launched from cmd.exe this should be the console, 
            * when launched via the RUN menu explorer, or another GUI app that wasn't started from the console this will fail.
            * When it fails we will redirect to the NUL device. This is to ensure that we have valid streams.
            * Later on in the initialisation sequence the --console option will be processed and this will cause the requestConsole() to
            * always open a new console, except for streams that are redirected. The same rules apply there, if both streams are redirected
            * the console will be opened, and it will contain a message to indicate that no output will be present because the streams are redirected
            */
            if (AttachConsole(ATTACH_PARENT_PROCESS) == 0) {
                /*
                * attach failed - so ensure that the streams are bound to the null device - but only when not already redirected
                */
                if (!m_stdout_isRedirectedAlready)
                {
                    stdout_isNull = true;
                    freopen("NUL", "w", stdout);
                }

                if (!m_stderr_isRedirectedAlready)
                {
                    stderr_isNull = true;
                    freopen("NUL", "w", stderr);
                }
            }
            /*
            * providing that AttachConsole succeeded - we can then either reopen the stream onto the console, or use
            * _fdopen to attached to the currently redirected (and open stream)
            */
            if (!stdout_isNull){
                if (!m_stdout_isRedirectedAlready)
                    freopen("conout$", "w", stdout);
                else 
                    /*
                    * for already redirected streams we need to attach the stream to the OS handle that is open.
                    * - this comes from part of the answer http://stackoverflow.com/a/13841522
                    *   _open_osfhandle returns an FD for the Win32 Handle, which is then opened using fdopen and
                    *   hopefully safely assigned to the stream (although it does look wrong to me it works)
                    * Removing this bit will stop pipes and command line redirection (> 2> and 2>&1 from working)
                    */
                    *stdout = *_fdopen(_open_osfhandle((intptr_t) GetStdHandle(STD_OUTPUT_HANDLE), _O_WRONLY), "a");
            }

            if (!stderr_isNull){
                if (!m_stderr_isRedirectedAlready)
                    freopen("conout$", "w", stderr);
                else
                    *stderr = *_fdopen(_open_osfhandle((intptr_t) GetStdHandle(STD_ERROR_HANDLE), _O_WRONLY), "a");
            }
        }
        //http://stackoverflow.com/a/25927081
        //Clear the error state for each of the C++ standard stream objects. 
        std::wcout.clear();
        std::cout.clear();
        std::wcerr.clear();
        std::cerr.clear();
#endif

        m_callbacks.push_back(new StderrLogCallback(m_logClass, m_logPriority));
        m_consoleCallbacks.push_back(m_callbacks.back());
#if defined (SG_WINDOWS) && !defined(NDEBUG)
		m_callbacks.push_back(new WinDebugLogCallback(m_logClass, m_logPriority));
		m_consoleCallbacks.push_back(m_callbacks.back());
#endif
    }

    ~LogStreamPrivate()
    {
        removeCallbacks();
    }

    SGMutex m_lock;
    SGBlockingQueue<LogEntry> m_entries;

    // log entries posted during startup
    std::vector<LogEntry> m_startupEntries;
    bool m_startupLogging = false;

    typedef std::vector<simgear::LogCallback*> CallbackVec;
    CallbackVec m_callbacks;
    /// subset of callbacks which correspond to stdout / console,
	/// and hence should dynamically reflect console logging settings
	CallbackVec m_consoleCallbacks;

    sgDebugClass m_logClass;
    sgDebugPriority m_logPriority;
    bool m_isRunning = false;
#if defined (SG_WINDOWS)
    // track whether the console was redirected on launch (in the constructor, which is called early on)
    bool m_stderr_isRedirectedAlready = false;
    bool m_stdout_isRedirectedAlready = false;
#endif
    bool m_developerMode = false;
    bool m_fileLine = false;

    // test suite mode.
    bool m_testMode = false;

    void startLog()
    {
        SGGuard<SGMutex> g(m_lock);
        if (m_isRunning) return;
        m_isRunning = true;
        start();
    }

    void setStartupLoggingEnabled(bool on)
    {
        if (m_startupLogging == on) {
            return;
        }

        {
            SGGuard<SGMutex> g(m_lock);
            m_startupLogging = on;
            m_startupEntries.clear();
        }
    }

    virtual void run()
    {
        while (1) {
            LogEntry entry(m_entries.pop());
            // special marker entry detected, terminate the thread since we are
            // making a configuration change or quitting the app
            if ((entry.debugClass == SG_NONE) && entry.file && !strcmp(entry.file, "done")) {
                return;
            }
            {
                SGGuard<SGMutex> g(m_lock);
                if (m_startupLogging) {
                    // save to the startup list for not-yet-added callbacks to
                    // pull down on startup
                    m_startupEntries.push_back(entry);
                }
            }
            // submit to each installed callback in turn
            for (simgear::LogCallback* cb : m_callbacks) {
                (*cb)(entry.debugClass, entry.debugPriority,
                    entry.file, entry.line, entry.message);
            }
        } // of main thread loop
    }

    bool stop()
    {
        {
            SGGuard<SGMutex> g(m_lock);
            if (!m_isRunning) {
                return false;
            }

            // log a special marker value, which will cause the thread to wakeup,
            // and then exit
            log(SG_NONE, SG_ALERT, "done", -1, "");
        }
        join();

        m_isRunning = false;
        return true;
    }

    void addCallback(simgear::LogCallback* cb)
    {
        PauseThread pause(this);
        m_callbacks.push_back(cb);

        // we clear startup entries not using this, so always safe to run
        // this code, container will simply be empty
        for (auto entry : m_startupEntries) {
            (*cb)(entry.debugClass, entry.debugPriority,
               entry.file, entry.line, entry.message);
        }
    }

    void removeCallback(simgear::LogCallback* cb)
    {
        PauseThread pause(this);
        CallbackVec::iterator it = std::find(m_callbacks.begin(), m_callbacks.end(), cb);
        if (it != m_callbacks.end()) {
            m_callbacks.erase(it);
        }
    }

    void removeCallbacks()
    {
        PauseThread pause(this);
        for (simgear::LogCallback* cb : m_callbacks) {
            delete cb;
        }
        m_callbacks.clear();
        m_consoleCallbacks.clear();
    }

    void setLogLevels( sgDebugClass c, sgDebugPriority p )
    {
        PauseThread pause(this);
        m_logPriority = p;
        m_logClass = c;
		BOOST_FOREACH(simgear::LogCallback* cb, m_consoleCallbacks) {
	        cb->setLogLevels(c, p);
		}
    }

    bool would_log( sgDebugClass c, sgDebugPriority p ) const
    {
        // Testing mode, so always log.
        if (m_testMode) return true;

        // SG_OSG (OSG notify) - will always be displayed regardless of FG log settings as OSG log level is configured 
        // separately and thus it makes more sense to allow these message through.
        if (static_cast<unsigned>(p) == static_cast<unsigned>(SG_OSG)) return true;

        p = translatePriority(p);
        if (p >= SG_INFO) return true;
        return ((c & m_logClass) != 0 && p >= m_logPriority);
    }

    void log( sgDebugClass c, sgDebugPriority p,
            const char* fileName, int line, const std::string& msg)
    {
        p = translatePriority(p);
        if (!m_fileLine) {
            /* This prevents output of file:line. */
            line = -1;
        }
        LogEntry entry(c, p, fileName, line, msg);
        m_entries.push(entry);
    }

    sgDebugPriority translatePriority(sgDebugPriority in) const
    {
        if (in == SG_DEV_WARN) {
            return m_developerMode ? SG_WARN : SG_DEBUG;
        }

        if (in == SG_DEV_ALERT) {
            return m_developerMode ? SG_POPUP : SG_WARN;
        }

        return in;
    }
};

/////////////////////////////////////////////////////////////////////////////

static std::unique_ptr<logstream> global_logstream;
static SGMutex global_logStreamLock;

logstream::logstream()
{
    d.reset(new LogStreamPrivate);
    d->startLog();
}

logstream::~logstream()
{
    popup_msgs.clear();
    d->stop();
}

void
logstream::setLogLevels( sgDebugClass c, sgDebugPriority p )
{
    d->setLogLevels(c, p);
}

void logstream::setDeveloperMode(bool devMode)
{
    d->m_developerMode = devMode;
}

void logstream::setFileLine(bool fileLine)
{
    d->m_fileLine = fileLine;
}

void
logstream::addCallback(simgear::LogCallback* cb)
{
    d->addCallback(cb);
}

void
logstream::removeCallback(simgear::LogCallback* cb)
{
    d->removeCallback(cb);
}

void
logstream::log( sgDebugClass c, sgDebugPriority p,
        const char* fileName, int line, const std::string& msg)
{
    d->log(c, p, fileName, line, msg);
}


void logstream::hexdump(sgDebugClass c, sgDebugPriority p, const char* fileName, int line, const void *mem, unsigned int len, unsigned int columns)
{
    unsigned int i, j;
    char temp[3000], temp1[3000];
    *temp = 0;

    for (i = 0; i < len + ((len % columns) ? (columns - len % columns) : 0); i++)
    {
        if (strlen(temp) > 500) return;
        /* print offset */
        if (i % columns == 0)
        {
            sprintf(temp1, "0x%06x: ", i);
            strcat(temp, temp1);
        }

        /* print hex data */
        if (i < len)
        {
            sprintf(temp1, "%02x ", 0xFF & ((char*)mem)[i]);
            strcat(temp, temp1);
        }
        else /* end of block, just aligning for ASCII dump */
        {
            strcat(temp, "   ");
        }

        /* print ASCII dump */
        if (i % columns == (columns - 1))
        {
            for (j = i - (columns - 1); j <= i; j++)
            {
                if (j >= len) /* end of block, not really printing */
                {
                    strcat(temp, " ");
                }
                else if (((((char*)mem)[j]) & (char)0x7f) > 32) /* printable char */
                {
                    char t2[2];
                    t2[0] = 0xFF & ((char*)mem)[j];
                    t2[1] = 0;
                    strcat(temp, t2);
                }
                else /* other char */
                {
                    strcat(temp, ".");
                }
            }
            log(c, p, fileName, line, temp );
            *temp = 0;
        }
    }
}

void
logstream::popup( const std::string& msg)
{
    popup_msgs.push_back(msg);
}

std::string
logstream::get_popup()
{
    std::string rv = "";
    if (!popup_msgs.empty())
    {
        rv = popup_msgs.front();
        popup_msgs.erase(popup_msgs.begin());
    }
    return rv;
}

bool
logstream::has_popup()
{
    return (popup_msgs.size() > 0) ? true : false;
}

bool
logstream::would_log( sgDebugClass c, sgDebugPriority p ) const
{
    return d->would_log(c,p);
}

sgDebugClass
logstream::get_log_classes() const
{
    return d->m_logClass;
}

sgDebugPriority
logstream::get_log_priority() const
{
    return d->m_logPriority;
}

void
logstream::set_log_priority( sgDebugPriority p)
{
    d->setLogLevels(d->m_logClass, p);
}

void
logstream::set_log_classes( sgDebugClass c)
{
    d->setLogLevels(c, d->m_logPriority);
}


logstream&
sglog()
{
    // Force initialization of cerr.
    static std::ios_base::Init initializer;

    // http://www.aristeia.com/Papers/DDJ_Jul_Aug_2004_revised.pdf
    // in the absence of portable memory barrier ops in Simgear,
    // let's keep this correct & safe
    SGGuard<SGMutex> g(global_logStreamLock);

    if( !global_logstream )
        global_logstream.reset(new logstream);
    return *(global_logstream.get());
}

void
logstream::logToFile( const SGPath& aPath, sgDebugClass c, sgDebugPriority p )
{
    d->addCallback(new FileLogCallback(aPath, c, p));
}

void logstream::setStartupLoggingEnabled(bool enabled)
{
    d->setStartupLoggingEnabled(enabled);
}

void logstream::requestConsole()
{
#if defined (SG_WINDOWS)
    const bool stderrAlreadyRedirected = d->m_stderr_isRedirectedAlready;
    const bool stdoutAlreadyRedirected = d->m_stdout_isRedirectedAlready;

    /*
     * 2016-09-20(RJH) - Reworked console handling
     * This is part of the reworked console handling for Win32. This is for building as a Win32 GUI Subsystem where no
     * console is allocated on launch. If building as a console app then the startup will ensure that a console is created - but
     * we don't need to handle that.
     * The new handling is quite simple:
     *              1. The constructor will ensure that these streams exists. It will attach to the
     *                 parent command prompt if started from the command prompt, otherwise the
     *                 stdout/stderr will be bound to the NUL device.
     *              2. with --console a window will always appear regardless of where the process was
     *                 started from. Any non redirected streams will be redirected
     *              3. You cannot use --console and either redirected stream.
     *
     * This is called after the Private Log Stream constructor so we need to undo any console that it has attached to.
     */

    if (!stderrAlreadyRedirected && !stdoutAlreadyRedirected) {
        FreeConsole();
        if (AllocConsole()) {
            if (!stdoutAlreadyRedirected)
                freopen("conout$", "w", stdout);

            if (!stderrAlreadyRedirected)
                freopen("conout$", "w", stderr);

            //http://stackoverflow.com/a/25927081
            //Clear the error state for each of the C++ standard stream objects.
            std::wcout.clear();
            std::cout.clear();
            std::wcerr.clear();
            std::cerr.clear();
        }
    } else {
        MessageBox(0, "--console ignored because stdout or stderr redirected with > or 2>", "Simgear Error", MB_OK | MB_ICONERROR);
    }
#endif
}

void
logstream::setTestingMode( bool testMode )
{
    d->m_testMode = testMode;
    if (testMode) d->removeCallbacks();
}


namespace simgear
{

void requestConsole()
{ 
    sglog().requestConsole();
}


void shutdownLogging()
{
    SGGuard<SGMutex> g(global_logStreamLock);
    global_logstream.reset();
}

} // of namespace simgear
