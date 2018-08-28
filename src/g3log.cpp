/** ==========================================================================
 * 2011 by KjellKod.cc. This is PUBLIC DOMAIN to use at your own risk and comes
 * with no warranties. This code is yours to share, use and modify with no
 * strings attached and no restrictions or obligations.
 *
 * For more information see g3log/LICENSE or refer refer to http://unlicense.org
 * ============================================================================
 *
 * Filename:g3log.cpp  Framework for Logging and Design By Contract
 * Created: 2011 by Kjell Hedström
 *
 * PUBLIC DOMAIN and Not copywrited since it was built on public-domain software and at least in "spirit" influenced
 * from the following sources
 * 1. kjellkod.cc ;)
 * 2. Dr.Dobbs, Petru Marginean:  http://drdobbs.com/article/printableArticle.jhtml?articleId=201804215&dept_url=/cpp/
 * 3. Dr.Dobbs, Michael Schulze: http://drdobbs.com/article/printableArticle.jhtml?articleId=225700666&dept_url=/cpp/
 * 4. Google 'glog': http://google-glog.googlecode.com/svn/trunk/doc/glog.html
 * 5. Various Q&A at StackOverflow
 * ********************************************* */

#include "g3log/g3log.hpp"
#include "g3log/std2_make_unique.hpp"
#include "g3log/logworker.hpp"
#include "g3log/crashhandler.hpp"
#include "g3log/logmessage.hpp"
#include "g3log/loglevels.hpp"
#include "gflags/gflags.h"


#include <mutex>
#include <memory>
#include <iostream>
#include <thread>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <sstream>


DEFINE_bool(logtostderr, false,
                 "log messages go to stderr instead of logfiles");
DEFINE_bool(alsologtostderr, false,
                 "log messages go to stderr in addition to logfiles");

DEFINE_int32(minloglevel, 0, "Messages logged at a lower level than this don't "
                  "actually get logged anywhere");

// To do
/**********   ************/
DEFINE_int32(stderrthreshold,
             G3LOG_ERROR.value,
             "log messages at or above this level are copied to stderr in "
             "addition to logfiles.  This flag obsoletes --alsologtostderr.");

DEFINE_int32(v, 0, "show all log message for level <= v.");
DEFINE_string(log_link, "", "put symbol link to the latest log.");
// To do
/**********   ************/

static const char* DefaultLogDir() {
  const char* env;
  env = getenv("G3LOG_LOG_DIR");
  if (env != NULL && env[0] != '\0') {
    return env;
  }
  env = getenv("TEST_TMPDIR");
  if (env != NULL && env[0] != '\0') {
    return env;
  }
  return "";
}


DEFINE_string(log_dir, DefaultLogDir(),
                   "If specified, logfiles are written into this directory instead "
                   "of the default logging directory.");


namespace {
   std::once_flag g_initialize_flag;
   g3::LogWorker* g_logger_instance = nullptr; // instantiated and OWNED somewhere else (main)
   std::mutex g_logging_init_mutex;

   std::unique_ptr<g3::LogMessage> g_first_unintialized_msg = {nullptr};
   std::once_flag g_set_first_uninitialized_flag;
   std::once_flag g_save_first_unintialized_flag;
   const std::function<void(void)> g_pre_fatal_hook_that_does_nothing = [] { /*does nothing */};
   std::function<void(void)> g_fatal_pre_logging_hook;


   std::atomic<size_t> g_fatal_hook_recursive_counter = {0};
}





namespace g3 {
   // signalhandler and internal clock is only needed to install once
   // for unit testing purposes the initializeLogging might be called
   // several times...
   //                    for all other practical use, it shouldn't!

   void initializeLogging(LogWorker* bgworker) {
      std::call_once(g_initialize_flag, [] {
         installCrashHandler();
      });
      std::lock_guard<std::mutex> lock(g_logging_init_mutex);
      if (internal::isLoggingInitialized() || nullptr == bgworker) {         
         std::ostringstream exitMsg;
         exitMsg << __FILE__ "->" << __FUNCTION__ << ":" << __LINE__ << std::endl;
         exitMsg << "\tFatal exit due to illegal initialization of g3::LogWorker\n";
         exitMsg << "\t(due to multiple initializations? : " << std::boolalpha << internal::isLoggingInitialized();
         exitMsg << ", due to nullptr == bgworker? : " << std::boolalpha << (nullptr == bgworker) << ")";
         std::cerr << exitMsg.str() << std::endl;
         std::exit(EXIT_FAILURE);
      }
      

      // Save the first uninitialized message, if any
      std::call_once(g_save_first_unintialized_flag, [&bgworker] {
         if (g_first_unintialized_msg) {
            bgworker->save(LogMessagePtr {std::move(g_first_unintialized_msg)});
         }
      });

      g_logger_instance = bgworker;
      // by default the pre fatal logging hook does nothing
      // if it WOULD do something it would happen in
      setFatalPreLoggingHook(g_pre_fatal_hook_that_does_nothing);
      // recurvise crash counter re-set to zero
      g_fatal_hook_recursive_counter.store(0);
   }


   /**
   *  default does nothing, @ref ::g_pre_fatal_hook_that_does_nothing
   *  It will be called just before sending the fatal message, @ref pushFatalmessageToLogger
   *  It will be reset to do nothing in ::initializeLogging(...)
   *     so please call this function, if you ever need to, after initializeLogging(...)
   */
   void setFatalPreLoggingHook(std::function<void(void)>  pre_fatal_hook) {
      static std::mutex m;
      std::lock_guard<std::mutex> lock(m);
      g_fatal_pre_logging_hook = pre_fatal_hook;
   }




   // By default this function pointer goes to \ref pushFatalMessageToLogger;
   std::function<void(FatalMessagePtr) > g_fatal_to_g3logworker_function_ptr = internal::pushFatalMessageToLogger;

   /** REPLACE fatalCallToLogger for fatalCallForUnitTest
    * This function switches the function pointer so that only
    * 'unitTest' mock-fatal calls are made.
    * */
   void setFatalExitHandler(std::function<void(FatalMessagePtr) > fatal_call) {
      g_fatal_to_g3logworker_function_ptr = fatal_call;
   }


   namespace internal {

      bool isLoggingInitialized() {
         return g_logger_instance != nullptr;
      }

      /**
       * Shutdown the logging by making the pointer to the background logger to nullptr. The object is not deleted
       * that is the responsibility of its owner. *
       */
      void shutDownLogging() {
         std::lock_guard<std::mutex> lock(g_logging_init_mutex);
         g_logger_instance = nullptr;

      }

      /** Same as the Shutdown above but called by the destructor of the LogWorker, thus ensuring that no further
       *  LOG(...) calls can happen to  a non-existing LogWorker.
       *  @param active MUST BE the LogWorker initialized for logging. If it is not then this call is just ignored
       *         and the logging continues to be active.
       * @return true if the correct worker was given,. and shutDownLogging was called
       */
      bool shutDownLoggingForActiveOnly(LogWorker* active) {
         if (isLoggingInitialized() && nullptr != active && (active != g_logger_instance)) {
            LOG(G3LOG_WARNING) << "\n\t\tAttempted to shut down logging, but the ID of the Logger is not the one that is active."
                         << "\n\t\tHaving multiple instances of the g3::LogWorker is likely a BUG"
                         << "\n\t\tEither way, this call to shutDownLogging was ignored"
                         << "\n\t\tTry g3::internal::shutDownLogging() instead";
            return false;
         }
         shutDownLogging();
         return true;
      }




      /** explicits copy of all input. This is makes it possibly to use g3log across dynamically loaded libraries
      * i.e. (dlopen + dlsym)  */
      void saveMessage(const char* entry, const char* file, int line, const char* function, const LEVELS& level,
                       const char* boolean_expression, int fatal_signal, const char* stack_trace) {

         if(level.value < FLAGS_minloglevel) {           
           return;
         }
         LEVELS msgLevel {level};
         LogMessagePtr message {std2::make_unique<LogMessage>(file, line, function, msgLevel)};
         message.get()->write().append(entry);
         message.get()->setExpression(boolean_expression);


         if (internal::wasFatal(level)) {
            auto fatalhook = g_fatal_pre_logging_hook;
            // In case the fatal_pre logging actually will cause a crash in its turn
            // let's not do recursive crashing!
            setFatalPreLoggingHook(g_pre_fatal_hook_that_does_nothing);
            ++g_fatal_hook_recursive_counter; // thread safe counter
            // "benign" race here. If two threads crashes, with recursive crashes
            // then it's possible that the "other" fatal stack trace will be shown
            // that's OK since it was anyhow the first crash detected
            static const std::string first_stack_trace = stack_trace;
            fatalhook();
            message.get()->write().append(stack_trace);

            if (g_fatal_hook_recursive_counter.load() > 1) {
               message.get()->write()
               .append("\n\n\nWARNING\n"
                       "A recursive crash detected. It is likely the hook set with 'setFatalPreLoggingHook(...)' is responsible\n\n")
               .append("---First crash stacktrace: ").append(first_stack_trace).append("\n---End of first stacktrace\n");
            }
            FatalMessagePtr fatal_message { std2::make_unique<FatalMessage>(*(message._move_only.get()), fatal_signal) };
            // At destruction, flushes fatal message to g3LogWorker
            // either we will stay here until the background worker has received the fatal
            // message, flushed the crash message to the sinks and exits with the same fatal signal
            //..... OR it's in unit-test mode then we throw a std::runtime_error (and never hit sleep)
            fatalCall(fatal_message);
         } else {
            pushMessageToLogger(message);
         }
      }

      /**
       * save the message to the logger. In case of called before the logger is instantiated
       * the first message will be saved. Any following subsequent unitnialized log calls
       * will be ignored.
       *
       * The first initialized log entry will also save the first uninitialized log message, if any
       * @param log_entry to save to logger
       */
      void pushMessageToLogger(LogMessagePtr incoming) { // todo rename to Push SavedMessage To Worker
         // Uninitialized messages are ignored but does not CHECK/crash the logger
         if (!internal::isLoggingInitialized()) {
            std::call_once(g_set_first_uninitialized_flag, [&] {
               g_first_unintialized_msg = incoming.release();
               std::string err = {"LOGGER NOT INITIALIZED:\n\t\t"};
               err.append(g_first_unintialized_msg->message());
               std::string& str = g_first_unintialized_msg->write();
               str.clear();
               str.append(err); // replace content
               std::cerr << str << std::endl;
            });
            return;
         }

         // logger is initialized
         g_logger_instance->save(incoming);
      }

      /** Fatal call saved to logger. This will trigger SIGABRT or other fatal signal
       * to exit the program. After saving the fatal message the calling thread
       * will sleep forever (i.e. until the background thread catches up, saves the fatal
       * message and kills the software with the fatal signal.
       */
      void pushFatalMessageToLogger(FatalMessagePtr message) {
         if (!isLoggingInitialized()) {
            std::ostringstream error;
            error << "FATAL CALL but logger is NOT initialized\n"
                  << "CAUSE: " << message.get()->reason()
                  << "\nMessage: \n" << message.get()->toString() << std::flush;
            std::cerr << error.str() << std::flush;
            internal::exitWithDefaultSignalHandler(message.get()->_level, message.get()->_signal_id);
         }
         g_logger_instance->fatal(message);
         while (shouldBlockForFatalHandling()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
         }
      }

      /** The default, initial, handling to send a 'fatal' event to g3logworker
       *  the caller will stay here, eternally, until the software is aborted
       * ... in the case of unit testing it is the given "Mock" fatalCall that will
       * define the behaviour.
       */
      void fatalCall(FatalMessagePtr message) {
         g_fatal_to_g3logworker_function_ptr(FatalMessagePtr {std::move(message)});
      }


   } // internal
} // g3



namespace g3Base {

CheckOpMessageBuilder::CheckOpMessageBuilder(const char *exprtext)
    : stream_(new std::ostringstream) {
  *stream_ << exprtext << " (";
}

CheckOpMessageBuilder::~CheckOpMessageBuilder() {
  delete stream_;
}

std::ostream* CheckOpMessageBuilder::ForVar2() {
  *stream_ << " vs. ";
  return stream_;
}

std::string* CheckOpMessageBuilder::NewString() {
  *stream_ << ")";
  return new std::string(stream_->str());
}

} // namespace g3Base


namespace g3Internal {
template <>
void MakeCheckOpValueString(std::ostream* os, const char& v) {
  if (v >= 32 && v <= 126) {
    (*os) << "'" << v << "'";
  } else {
    (*os) << "char value " << (short)v;
  }
}

template <>
void MakeCheckOpValueString(std::ostream* os, const signed char& v) {
  if (v >= 32 && v <= 126) {
    (*os) << "'" << v << "'";
  } else {
    (*os) << "signed char value " << (short)v;
  }
}

template <>
void MakeCheckOpValueString(std::ostream* os, const unsigned char& v) {
  if (v >= 32 && v <= 126) {
    (*os) << "'" << v << "'";
  } else {
    (*os) << "unsigned char value " << (unsigned short)v;
  }
}

} //g3Internal end

// Helper functions for string comparisons.
#define DEFINE_CHECK_STROP_IMPL(name, func, expected)                   \
  std::string* Check##func##expected##Impl(const char* s1, const char* s2,   \
                                      const char* names) {              \
    bool equal = s1 == s2 || (s1 && s2 && !func(s1, s2));               \
    if (equal == expected) return NULL;                                 \
    else {                                                              \
      std::ostringstream ss;                                                 \
      if (!s1) s1 = "";                                                 \
      if (!s2) s2 = "";                                                 \
      ss << #name " failed: " << names << " (" << s1 << " vs. " << s2 << ")"; \
      return new std::string(ss.str());                                      \
    }                                                                   \
  }
DEFINE_CHECK_STROP_IMPL(CHECK_STREQ, strcmp, true)
DEFINE_CHECK_STROP_IMPL(CHECK_STRNE, strcmp, false)
DEFINE_CHECK_STROP_IMPL(CHECK_STRCASEEQ, strcasecmp, true)
DEFINE_CHECK_STROP_IMPL(CHECK_STRCASENE, strcasecmp, false)
#undef DEFINE_CHECK_STROP_IMPL