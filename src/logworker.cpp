/** ==========================================================================
* 2011 by KjellKod.cc. This is PUBLIC DOMAIN to use at your own risk and comes
* with no warranties. This code is yours to share, use and modify with no
* strings attached and no restrictions or obligations.
*
* For more information see g3log/LICENSE or refer refer to http://unlicense.org
* ============================================================================*/

#include "g3log/logworker.hpp"
#include "g3log/logmessage.hpp"
#include "g3log/active.hpp"
#include "g3log/g3log.hpp"
#include "g3log/future.hpp"
#include "g3log/crashhandler.hpp"
#ifdef HAVE_SYS_UTSNAME_H
#include <sys/utsname.h>
#endif

#include <iostream>


static const std::string GetHostName() {
  std::string hostname;
#if defined(HAVE_SYS_UTSNAME_H)
  struct utsname buf;
  if (0 != uname(&buf)) {
    // ensure null termination on failure
    *buf.nodename = '\0';
  }
  hostname = buf.nodename;
#elif defined(OS_WINDOWS)
  char buf[MAX_COMPUTERNAME_LENGTH + 1];
  DWORD len = MAX_COMPUTERNAME_LENGTH + 1;
  if (GetComputerNameA(buf, &len)) {
    hostname = buf;
  } else {
    hostname.clear();
  }
#else
# warning There is no way to retrieve the host name.
  hostname = "(unknown)";
#endif
  return hostname;
}


static const std::string GetProjectShortName(const std::string& argv0) {
  std::string project_short_name;
  const char* slash = strrchr(argv0.c_str(), '/');
#ifdef OS_WINDOWS
  if (!slash)  slash = strrchr(argv0.c_str(), '\\');
#endif
  project_short_name = slash ? slash + 1 : argv0;
  return project_short_name; 
}


static const std::string MyUserName() {
  std::string username;
#if defined(OS_WINDOWS)
  const char* user = getenv("USERNAME");
#else
  const char* user = getenv("USER");
#endif
  if(user != NULL) {
    username = user;
  }else {
    username = "invalid-user";
  }
  return username;
}

static const std::string DefaultLogPrefix(const std::string& argv0) {
  std::string file_stripped_name(GetProjectShortName(argv0));
  std::string hostname = GetHostName();
  std::string uidname = MyUserName();
  file_stripped_name += "." + hostname + "." + uidname;  
  return file_stripped_name;

}

namespace g3 {

   LogWorkerImpl::LogWorkerImpl() : _bg(kjellkod::Active::createActive()) { }

   void LogWorkerImpl::bgSave(g3::LogMessagePtr msgPtr) {
      std::unique_ptr<LogMessage> uniqueMsg(std::move(msgPtr.get()));

      for (auto& sink : _sinks) {
         LogMessage msg(*(uniqueMsg));
         sink->send(LogMessageMover(std::move(msg)));
      }

      if (_sinks.empty()) {
         std::string err_msg {"g3logworker has no sinks. Message: ["};
         err_msg.append(uniqueMsg.get()->toString()).append("]\n");
         std::cerr << err_msg;
      }
   }

   void LogWorkerImpl::bgFatal(FatalMessagePtr msgPtr) {
      // this will be the last message. Only the active logworker can receive a FATAL call so it's
      // safe to shutdown logging now
      g3::internal::shutDownLogging();

      std::string reason = msgPtr.get()->reason();
      const auto level = msgPtr.get()->_level;
      const auto fatal_id = msgPtr.get()->_signal_id;


      std::unique_ptr<LogMessage> uniqueMsg(std::move(msgPtr.get()));
      uniqueMsg->write().append("\nExiting after fatal event  (").append(uniqueMsg->level());


      // Change output in case of a fatal signal (or windows exception)
      std::string exiting = {"Fatal type: "};

      uniqueMsg->write().append("). ").append(exiting).append(" ").append(reason)
      .append("\nLog content flushed sucessfully to sink\n\n");

      std::cerr << uniqueMsg->toString() << std::flush;
      for (auto& sink : _sinks) {
         LogMessage msg(*(uniqueMsg));
         sink->send(LogMessageMover(std::move(msg)));
      }


      // This clear is absolutely necessary
      // All sinks are forced to receive the fatal message above before we continue
      _sinks.clear(); // flush all queues
      internal::exitWithDefaultSignalHandler(level, fatal_id);

      // should never reach this point
      perror("g3log exited after receiving FATAL trigger. Flush message status: ");
   }

   LogWorker::~LogWorker() {
      g3::internal::shutDownLoggingForActiveOnly(this);

      // The sinks WILL automatically be cleared at exit of this destructor
      // However, the waiting below ensures that all messages until this point are taken care of
      // before any internals/LogWorkerImpl of LogWorker starts to be destroyed.
      // i.e. this avoids a race with another thread slipping through the "shutdownLogging" and calling
      // calling ::save or ::fatal through LOG/CHECK with lambda messages and "partly deconstructed LogWorkerImpl"
      //
      //   Any messages put into the queue will be OK due to:
      //  *) If it is before the wait below then they will be executed
      //  *) If it is AFTER the wait below then they will be ignored and NEVER executed
      auto bg_clear_sink_call = [this] { _impl._sinks.clear(); };
      auto token_cleared = g3::spawn_task(bg_clear_sink_call, _impl._bg.get());
      token_cleared.wait();

      // The background worker WILL be automatically cleared at the exit of the destructor
      // However, the explicitly clearing of the background worker (below) makes sure that there can
      // be no thread that manages to add another sink after the call to clear the sinks above.
      //   i.e. this manages the extremely unlikely case of another thread calling
      // addWrappedSink after the sink clear above. Normally adding of sinks should be done in main.cpp
      // and be closely coupled with the existance of the LogWorker. Sharing this adding of sinks to
      // other threads that do not know the state of LogWorker is considered a bug but it is dealt with
      // nonetheless below.
      //
      // If sinks would already have been added after the sink clear above then this reset will deal with it
      // without risking lambda execution with a partially deconstructed LogWorkerImpl
      // Calling g3::spawn_task on a nullptr Active object will not crash but return
      // a future containing an appropriate exception.
      _impl._bg.reset(nullptr);
   }

   void LogWorker::save(LogMessagePtr msg) {
      _impl._bg->send([this, msg] {_impl.bgSave(msg); });
   }

   void LogWorker::fatal(FatalMessagePtr fatal_message) {
      _impl._bg->send([this, fatal_message] {_impl.bgFatal(fatal_message); });
   }

   void LogWorker::addWrappedSink(std::shared_ptr<g3::internal::SinkWrapper> sink) {
      auto bg_addsink_call = [this, sink] {_impl._sinks.push_back(sink);};
      auto token_done = g3::spawn_task(bg_addsink_call, _impl._bg.get());
      token_done.wait();
   }

   std::unique_ptr<LogWorker> LogWorker::createLogWorker() {
      return std::unique_ptr<LogWorker>(new LogWorker);
   }

   std::unique_ptr<FileSinkHandle>LogWorker::addDefaultLogger(const std::string& argv0, const std::string& log_directory, const std::string& default_id) {
      std::string log_prefix = DefaultLogPrefix(argv0);      
      return addSink(std2::make_unique<g3::FileSink>(log_prefix, log_directory, default_id), &FileSink::fileWrite);
   }




} // g3
