/** ==========================================================================
 * 2011 by KjellKod.cc. This is PUBLIC DOMAIN to use at your own risk and comes
 * with no warranties. This code is yours to share, use and modify with no
 * strings attached and no restrictions or obligations.
 *
 * For more information see g3log/LICENSE or refer refer to http://unlicense.org
 * ============================================================================
 *
 * Filename:g3log.hpp  Framework for Logging and Design By Contract
 * Created: 2011 by Kjell Hedström
 *
 * PUBLIC DOMAIN and Not copywrited since it was built on public-domain software and influenced
 * at least in "spirit" from the following sources
 * 1. kjellkod.cc ;)
 * 2. Dr.Dobbs, Petru Marginean:  http://drdobbs.com/article/printableArticle.jhtml?articleId=201804215&dept_url=/caddpp/
 * 3. Dr.Dobbs, Michael Schulze: http://drdobbs.com/article/printableArticle.jhtml?articleId=225700666&dept_url=/cpp/
 * 4. Google 'glog': http://google-glog.googlecode.com/svn/trunk/doc/glog.html
 * 5. Various Q&A at StackOverflow
 * ********************************************* */


#pragma once

#include "g3log/loglevels.hpp"
#include "g3log/logcapture.hpp"
#include "g3log/logmessage.hpp"
#include "g3log/generated_definitions.hpp"

#include <string>
#include <functional>
#include <string.h>

#if !(defined(__PRETTY_FUNCTION__))
#define __PRETTY_FUNCTION__   __FUNCTION__
#endif

// thread_local doesn't exist before VS2013
// it exists on VS2015
#if !(defined(thread_local)) && defined(_MSC_VER) && _MSC_VER < 1900
#define thread_local __declspec(thread)
#endif


/** namespace for LOG() and CHECK() frameworks
 * History lesson:   Why the names 'g3' and 'g3log'?:
 * The framework was made in my own free time as PUBLIC DOMAIN but the
 * first commercial project to use it used 'g3' as an internal denominator for
 * the current project. g3 as in 'generation 2'. I decided to keep the g3 and g3log names
 * to give credit to the people in that project (you know who you are :) and I guess also
 * for 'sentimental' reasons. That a big influence was Google's glog is just a happy
 * coincidence or subconscious choice. Either way g3log became the name for this logger.
 *
 * --- Thanks for a great 2011 and good luck with 'g3' --- KjellKod
 */
namespace g3 {
   class LogWorker;
   struct LogMessage;
   struct FatalMessage;

   /** Should be called at very first startup of the software with \ref g3LogWorker
    *  pointer. Ownership of the \ref g3LogWorker is the responsibility of the caller */
   void initializeLogging(LogWorker *logger);


   /** setFatalPreLoggingHook() provides an optional extra step before the fatalExitHandler is called
    *
    * Set a function-hook before a fatal message will be sent to the logger
    * i.e. this is a great place to put a break point, either in your debugger
    * or programatically to catch LOG(FATAL), CHECK(...) or an OS fatal event (exception or signal)
    * This will be reset to default (does nothing) at initializeLogging(...);
    *
    * Example usage:
    * Windows: g3::setFatalPreLoggingHook([]{__debugbreak();}); // remember #include <intrin.h>
    *         WARNING: '__debugbreak()' when not running in Debug in your Visual Studio IDE will likely
    *                   trigger a recursive crash if used here. It should only be used when debugging
    *                   in your Visual Studio IDE. Recursive crashes are handled but are unnecessary.
    *
    * Linux:   g3::setFatalPreLoggingHook([]{ raise(SIGTRAP); });
    */
   void setFatalPreLoggingHook(std::function<void(void)>  pre_fatal_hook);

   /** If the @ref setFatalPreLoggingHook is not enough and full fatal exit handling is needed then
    * use "setFatalExithandler".  Please see g3log.cpp and crashhandler_windows.cpp or crashhandler_unix for
    * example of restoring signal and exception handlers, flushing the log and shutting down.
    */
   void setFatalExitHandler(std::function<void(FatalMessagePtr)> fatal_call);


#ifdef G3_DYNAMIC_MAX_MESSAGE_SIZE
  // only_change_at_initialization namespace is for changes to be done only during initialization. More specifically
  // items here would be called prior to calling other parts of g3log
  namespace only_change_at_initialization {
    // Sets the MaxMessageSize to be used when capturing log messages. Currently this value is set to 2KB. Messages
    // Longer than this are bound to 2KB with the string "[...truncated...]" at the end. This function allows
    // this limit to be changed.
    void setMaxMessageSize(size_t max_size);
  }
#endif /* G3_DYNAMIC_MAX_MESSAGE_SIZE */

   // internal namespace is for completely internal or semi-hidden from the g3 namespace due to that it is unlikely
   // that you will use these
   namespace internal {
      /// @returns true if logger is initialized
      bool isLoggingInitialized();

      // Save the created LogMessage to any existing sinks
      void saveMessage(const char *message, const char *file, int line, const char *function, const LEVELS &level,
                       const char *boolean_expression, int fatal_signal, const char *stack_trace);

      // forwards the message to all sinks
      void pushMessageToLogger(LogMessagePtr log_entry);


      // forwards a FATAL message to all sinks,. after which the g3logworker
      // will trigger crashhandler / g3::internal::exitWithDefaultSignalHandler
      //
      // By default the "fatalCall" will forward a FatalMessageptr to this function
      // this behavior can be changed if you set a different fatal handler through
      // "setFatalExitHandler"
      void pushFatalMessageToLogger(FatalMessagePtr message);


      // Saves the created FatalMessage to any existing sinks and exits with
      // the originating fatal signal,. or SIGABRT if it originated from a broken contract.
      // By default forwards to: pushFatalMessageToLogger, see "setFatalExitHandler" to override
      //
      // If you override it then you probably want to call "pushFatalMessageToLogger" after your
      // custom fatal handler is done. This will make sure that the fatal message the pushed
      // to sinks as well as shutting down the process
      void fatalCall(FatalMessagePtr message);

      // Shuts down logging. No object cleanup but further LOG(...) calls will be ignored.
      void shutDownLogging();

      // Shutdown logging, but ONLY if the active logger corresponds to the one currently initialized
      bool shutDownLoggingForActiveOnly(LogWorker *active);

   } // internal
} // g3

#define INTERNAL_LOG_MESSAGE(level) LogCapture(__FILE__, __LINE__, static_cast<const char*>(__PRETTY_FUNCTION__), level)

#define INTERNAL_CONTRACT_MESSAGE(boolean_expression)  \
   LogCapture(__FILE__, __LINE__, __PRETTY_FUNCTION__, g3::internal::CONTRACT, boolean_expression)


// LOG(level) is the API for the stream log
#define LOG(level) if(!g3::logLevel(level)){ } else INTERNAL_LOG_MESSAGE(level).stream()

#define G3LOG_LOG(level) if(!g3::logLevel(level)){ } else INTERNAL_LOG_MESSAGE(level).stream()

//LOG for every n message
#define SOME_KIND_OF_LOG_EVERY_N(level, n, what_to_do)    \
   static int LOG_OCCURRENCES = 0, LOG_OCCURRENCES_MOD_N = 0;  \
   ++LOG_OCCURRENCES;  \
   if (++LOG_OCCURRENCES_MOD_N > n) LOG_OCCURRENCES_MOD_N -= n; \
   if (LOG_OCCURRENCES_MOD_N == 1) INTERNAL_LOG_MESSAGE(level).stream()

#define LOG_EVERY_N(level, n)  \
   SOME_KIND_OF_LOG_EVERY_N(level, (n), what_to_do)

#define G3LOG_LOG_EVERY_N(level, n)  \
   SOME_KIND_OF_LOG_EVERY_N(level, (n), what_to_do)
   

// 'Conditional' stream log
#define LOG_IF(level, boolean_expression)  \
   if(true == (boolean_expression))  \
      if(g3::logLevel(level))  INTERNAL_LOG_MESSAGE(level).stream()

#define G3LOG_LOG_IF(level, boolean_expression)  \
   if(true == (boolean_expression))  \
      if(g3::logLevel(level))  INTERNAL_LOG_MESSAGE(level).stream()

//LOG for every n message with conditions
#define SOME_KIND_OF_LOG_IF_EVERY_N(level, boolean_expression, what_to_do)    \
  static int LOG_OCCURRENCES = 0, LOG_OCCURRENCES_MOD_N = 0; \
  ++LOG_OCCURRENCES; \
  if boolean_expression && \
     ((LOG_OCCURRENCES_MOD_N=(LOG_OCCURRENCES_MOD_N + 1) % n) == (1 % n))) \
     INTERNAL_LOG_MESSAGE(level).stream();

#define LOG_IF_EVERY_N(level, boolean_expression, n)      \
  SOME_KIND_OF_LOG_IF_EVERY_N(level, (boolean_expression), (n), what_to_do)

#define G3LOG_LOG_IF_EVERY_N(level, boolean_expression, n)      \
  SOME_KIND_OF_LOG_IF_EVERY_N(level, (boolean_expression), (n), what_to_do)

// 'Design By Contract' stream API. For Broken Contracts:
//         unit testing: it will throw std::runtime_error when a contract breaks
//         I.R.L : it will exit the application by using fatal signal SIGABRT
#define CHECK(boolean_expression)        \
   if (false == (boolean_expression))  INTERNAL_CONTRACT_MESSAGE(#boolean_expression).stream()


/** For details please see this
 * REFERENCE: http://www.cppreference.com/wiki/io/c/printf_format
 * \verbatim
 *
  There are different %-codes for different variable types, as well as options to
    limit the length of the variables and whatnot.
    Code Format
    %[flags][width][.precision][length]specifier
 SPECIFIERS
 ----------
 %c character
 %d signed integers
 %i signed integers
 %e scientific notation, with a lowercase “e”
 %E scientific notation, with a uppercase “E”
 %f floating point
 %g use %e or %f, whichever is shorter
 %G use %E or %f, whichever is shorter
 %o octal
 %s a string of characters
 %u unsigned integer
 %x unsigned hexadecimal, with lowercase letters
 %X unsigned hexadecimal, with uppercase letters
 %p a pointer
 %n the argument shall be a pointer to an integer into which is placed the number of characters written so far

For flags, width, precision etc please see the above references.
EXAMPLES:
{
   LOGF(INFO, "Characters: %c %c \n", 'a', 65);
   LOGF(INFO, "Decimals: %d %ld\n", 1977, 650000L);      // printing long
   LOGF(INFO, "Preceding with blanks: %10d \n", 1977);
   LOGF(INFO, "Preceding with zeros: %010d \n", 1977);
   LOGF(INFO, "Some different radixes: %d %x %o %#x %#o \n", 100, 100, 100, 100, 100);
   LOGF(INFO, "floats: %4.2f %+.0e %E \n", 3.1416, 3.1416, 3.1416);
   LOGF(INFO, "Width trick: %*d \n", 5, 10);
   LOGF(INFO, "%s \n", "A string");
   return 0;
}
And here is possible output
:      Characters: a A
:      Decimals: 1977 650000
:      Preceding with blanks:       1977
:      Preceding with zeros: 0000001977
:      Some different radixes: 100 64 144 0x64 0144
:      floats: 3.14 +3e+000 3.141600E+000
:      Width trick:    10
:      A string  \endverbatim */
#define LOGF(level, printf_like_message, ...)                 \
   if(!g3::logLevel(level)){ } else INTERNAL_LOG_MESSAGE(level).capturef(printf_like_message, ##__VA_ARGS__)

// Conditional log printf syntax
#define LOGF_IF(level,boolean_expression, printf_like_message, ...) \
   if(true == (boolean_expression))                                     \
      if(g3::logLevel(level))  INTERNAL_LOG_MESSAGE(level).capturef(printf_like_message, ##__VA_ARGS__)

// Design By Contract, printf-like API syntax with variadic input parameters.
// Throws std::runtime_eror if contract breaks
#define CHECKF(boolean_expression, printf_like_message, ...)    \
   if (false == (boolean_expression))  INTERNAL_CONTRACT_MESSAGE(#boolean_expression).capturef(printf_like_message, ##__VA_ARGS__)

// Backwards compatible. The same as CHECKF. 
// Design By Contract, printf-like API syntax with variadic input parameters.
// Throws std::runtime_eror if contract breaks
#define CHECK_F(boolean_expression, printf_like_message, ...)    \
   if (false == (boolean_expression))  INTERNAL_CONTRACT_MESSAGE(#boolean_expression).capturef(printf_like_message, ##__VA_ARGS__)

// Add CHECKs features for us

#if defined(_MSC_VER)
#define G3LOG_MSVC_PUSH_DISABLE_WARNING(n) __pragma(warning(push)) \
                                     __pragma(warning(disable:n))
#define G3LOG_MSVC_POP_WARNING() __pragma(warning(pop))
#else
#define G3LOG_MSVC_PUSH_DISABLE_WARNING(n)
#define G3LOG_MSVC_POP_WARNING()
#endif


#ifndef G3LOG_DLL_DECL
# if defined(_WIN32) && !defined(__CYGWIN__)
#   define G3LOG_DLL_DECL  __declspec(dllimport)
# else
#   define G3LOG_DLL_DECL
# endif
#endif

#ifndef G3LOG_PREDICT_BRANCH_NOT_TAKEN
#if !defined(_WIN32)
#define G3LOG_PREDICT_BRANCH_NOT_TAKEN(x) (__builtin_expect(x, 0))
#else
#define G3LOG_PREDICT_BRANCH_NOT_TAKEN(x) x
#endif
#endif

#ifndef G3LOG_PREDICT_FALSE
#if !defined(_WIN32)
#define G3LOG_PREDICT_FALSE(x) (__builtin_expect(x, 0))
#else
#define G3LOG_PREDICT_FALSE(x) x
#endif
#endif

#ifndef G3LOG_PREDICT_TRUE
#if !defined(_WIN32)
#define G3LOG_PREDICT_TRUE(x) (__builtin_expect(!!(x), 1))
#else
#define G3LOG_PREDICT_TRUE(x) x
#endif
#endif


#if defined(NDEBUG) && !defined(DCHECK_ALWAYS_ON)
#define DCHECK_IS_ON() 0
#else
#define DCHECK_IS_ON() 1
#endif

template <typename T>
inline void MakeCheckOpValueString(std::ostream* os, const T& v) {
  (*os) << v;
}

// Overrides for char types provide readable values for unprintable
// characters.
template <> G3LOG_DLL_DECL
void MakeCheckOpValueString(std::ostream* os, const char& v);
template <> G3LOG_DLL_DECL
void MakeCheckOpValueString(std::ostream* os, const signed char& v);
template <> G3LOG_DLL_DECL
void MakeCheckOpValueString(std::ostream* os, const unsigned char& v);

// Build the error message string. Specify no inlining for code size.
template <typename T1, typename T2>
std::string* MakeCheckOpString(const T1& v1, const T2& v2, const char* exprtext)
    __attribute__((noinline));

namespace base {


// A helper class for formatting "expr (V1 vs. V2)" in a CHECK_XX
// statement.  See MakeCheckOpString for sample usage.  Other
// approaches were considered: use of a template method (e.g.,
// base::BuildCheckOpString(exprtext, base::Print<T1>, &v1,
// base::Print<T2>, &v2), however this approach has complications
// related to volatile arguments and function-pointer arguments).
class G3LOG_DLL_DECL CheckOpMessageBuilder {
 public:
  // Inserts "exprtext" and " (" to the stream.
  explicit CheckOpMessageBuilder(const char *exprtext);
  // Deletes "stream_".
  ~CheckOpMessageBuilder();
  // For inserting the first variable.
  std::ostream* ForVar1() { return stream_; }
  // For inserting the second variable (adds an intermediate " vs. ").
  std::ostream* ForVar2();
  // Get the result (inserts the closing ")").
  std::string* NewString();

 private:
  std::ostringstream *stream_;
};

} // namespace base


struct CheckOpString {
  CheckOpString(std::string* str) : str_(str) { }
  // No destructor: if str_ is non-NULL, we're about to LOG(FATAL),
  // so there's no point in cleaning up str_.
  operator bool() const {
    return G3LOG_PREDICT_BRANCH_NOT_TAKEN(str_ != NULL);
  }
  std::string* str_;
};

// Function is overloaded for integral types to allow static const
// integrals declared in classes and not defined to be used as arguments to
// CHECK* macros. It's not encouraged though.
template <class T>
inline const T&       GetReferenceableValue(const T&           t) { return t; }
inline char           GetReferenceableValue(char               t) { return t; }
inline unsigned char  GetReferenceableValue(unsigned char      t) { return t; }
inline signed char    GetReferenceableValue(signed char        t) { return t; }
inline short          GetReferenceableValue(short              t) { return t; }
inline unsigned short GetReferenceableValue(unsigned short     t) { return t; }
inline int            GetReferenceableValue(int                t) { return t; }
inline unsigned int   GetReferenceableValue(unsigned int       t) { return t; }
inline long           GetReferenceableValue(long               t) { return t; }
inline unsigned long  GetReferenceableValue(unsigned long      t) { return t; }
inline long long      GetReferenceableValue(long long          t) { return t; }
inline unsigned long long GetReferenceableValue(unsigned long long t) {
  return t;
}

template <typename T1, typename T2>
std::string* MakeCheckOpString(const T1& v1, const T2& v2, const char* exprtext) {
  base::CheckOpMessageBuilder comb(exprtext);
  MakeCheckOpValueString(comb.ForVar1(), v1);
  MakeCheckOpValueString(comb.ForVar2(), v2);
  return comb.NewString();
}

#define DEFINE_CHECK_OP_IMPL(name, op) \
  template <typename T1, typename T2> \
  inline std::string* name##Impl(const T1& v1, const T2& v2,    \
                            const char* exprtext) { \
    if (G3LOG_PREDICT_TRUE(v1 op v2)) return NULL; \
    else return MakeCheckOpString(v1, v2, exprtext); \
  } \
  inline std::string* name##Impl(int v1, int v2, const char* exprtext) { \
    return name##Impl<int, int>(v1, v2, exprtext); \
  }

// We use the full name Check_EQ, Check_NE, etc. in case the file including
// base/logging.h provides its own #defines for the simpler names EQ, NE, etc.
// This happens if, for example, those are used as token names in a
// yacc grammar.
DEFINE_CHECK_OP_IMPL(Check_EQ, ==)  // Compilation error with CHECK_EQ(NULL, x)?
DEFINE_CHECK_OP_IMPL(Check_NE, !=)  // Use CHECK(x == NULL) instead.
DEFINE_CHECK_OP_IMPL(Check_LE, <=)
DEFINE_CHECK_OP_IMPL(Check_LT, < )
DEFINE_CHECK_OP_IMPL(Check_GE, >=)
DEFINE_CHECK_OP_IMPL(Check_GT, > )

#undef DEFINE_CHECK_OP_IMPL

#if defined(STATIC_ANALYSIS)
// Only for static analysis tool to know that it is equivalent to assert
#define CHECK_OP_LOG(name, op, val1, val2, log) CHECK((val1) op (val2))
#elif DCHECK_IS_ON()
// In debug mode, avoid constructing CheckOpStrings if possible,
// to reduce the overhead of CHECK statments by 2x.
// Real DCHECK-heavy tests have seen 1.5x speedups.

// The meaning of "string" might be different between now and
// when this macro gets invoked (e.g., if someone is experimenting
// with other string implementations that get defined after this
// file is included).  Save the current meaning now and use it
// in the macro.
typedef std::string _Check_string;
#define CHECK_OP_LOG(name, op, val1, val2, log)                         \
  while (_Check_string* _result =                                       \
         Check##name##Impl(                                             \
             GetReferenceableValue(val1),                               \
             GetReferenceableValue(val2),                               \
             #val1 " " #op " " #val2))                                  \
    log(__FILE__, __LINE__,                                             \
        static_cast<const char*>(__PRETTY_FUNCTION__),                  \
        CheckOpString(_result)).stream()
#else
// In optimized mode, use CheckOpString to hint to compiler that
// the while condition is unlikely.
#define CHECK_OP_LOG(name, op, val1, val2, log)                         \
  while (CheckOpString _result =                                        \
         Check##name##Impl(                                             \
             GetReferenceableValue(val1),                               \
             GetReferenceableValue(val2),                               \
             #val1 " " #op " " #val2))                                  \                          
    log(__FILE__, __LINE__,                                             \
        static_cast<const char*>(__PRETTY_FUNCTION__),                  \
     _result).stream()
#endif  // STATIC_ANALYSIS, DCHECK_IS_ON()


#define CHECK_OP(name, op, val1, val2)    \
   CHECK_OP_LOG(name, op, val1, val2, LogCapture)   

#define CHECK_EQ(val1, val2)  CHECK_OP(_EQ, ==, val1, val2)
#define CHECK_NE(val1, val2)  CHECK_OP(_NE, !=, val1, val2)
#define CHECK_LE(val1, val2)  CHECK_OP(_LE, <=, val1, val2)
#define CHECK_GE(val1, val2)  CHECK_OP(_GE, >=, val1, val2)
#define CHECK_LT(val1, val2)  CHECK_OP(_LT, <, val1, val2)
#define CHECK_GT(val1, val2)  CHECK_OP(_GT, >, val1, val2)

// Check that the input is non NULL.  This very useful in constructor
// initializer lists.

#define CHECK_NOTNULL(val) \
  CheckNotNull(__FILE__, __LINE__, "'" #val "' Must be non NULL", (val))

// Check if it's compiled in C++11 mode.
//
// GXX_EXPERIMENTAL_CXX0X is defined by gcc and clang up to at least
// gcc-4.7 and clang-3.1 (2011-12-13).  __cplusplus was defined to 1
// in gcc before 4.7 (Crosstool 16) and clang before 3.1, but is
// defined according to the language version in effect thereafter.
// Microsoft Visual Studio 14 (2015) sets __cplusplus==199711 despite
// reasonably good C++11 support, so we set LANG_CXX for it and
// newer versions (_MSC_VER >= 1900).
#if (defined(__GXX_EXPERIMENTAL_CXX0X__) || __cplusplus >= 201103L || \
     (defined(_MSC_VER) && _MSC_VER >= 1900))
// Helper for CHECK_NOTNULL().
//
// In C++11, all cases can be handled by a single function. Since the value
// category of the argument is preserved (also for rvalue references),
// member initializer lists like the one below will compile correctly:
//
//   Foo()
//     : x_(CHECK_NOTNULL(MethodReturningUniquePtr())) {}
template <typename T>
T CheckNotNull(const char* file, int line, const char* names, T&& t) {
 if (t == nullptr) {
   LogCapture(__FILE__, __LINE__, static_cast<const char*>(__PRETTY_FUNCTION__), new std::string(names));
 }
 return std::forward<T>(t);
}

#else

// A small helper for CHECK_NOTNULL().
template <typename T>
T* CheckNotNull(const char *file, int line, const char *names, T* t) {
  if (t == NULL) {
    LogCapture(__FILE__, __LINE__, static_cast<const char*>(__PRETTY_FUNCTION__), new std::string(names));
  }
  return t;
}
#endif

// Helper functions for string comparisons.
// To avoid bloat, the definitions are in logging.cc.
#define DECLARE_CHECK_STROP_IMPL(func, expected) \
  G3LOG_DLL_DECL std::string* Check##func##expected##Impl( \
      const char* s1, const char* s2, const char* names);
DECLARE_CHECK_STROP_IMPL(strcmp, true)
DECLARE_CHECK_STROP_IMPL(strcmp, false)
DECLARE_CHECK_STROP_IMPL(strcasecmp, true)
DECLARE_CHECK_STROP_IMPL(strcasecmp, false)
#undef DECLARE_CHECK_STROP_IMPL

// Helper macro for string comparisons.
// Don't use this macro directly in your code, use CHECK_STREQ et al below.
#define CHECK_STROP(func, op, expected, s1, s2) \
  while (CheckOpString _result = \
         Check##func##expected##Impl((s1), (s2), \
                                     #s1 " " #op " " #s2)) \
    LOG(FATAL) << *_result.str_


// String (char*) equality/inequality checks.
// CASE versions are case-insensitive.
//
// Note that "s1" and "s2" may be temporary strings which are destroyed
// by the compiler at the end of the current "full expression"
// (e.g. CHECK_STREQ(Foo().c_str(), Bar().c_str())).

#define CHECK_STREQ(s1, s2) CHECK_STROP(strcmp, ==, true, s1, s2)
#define CHECK_STRNE(s1, s2) CHECK_STROP(strcmp, !=, false, s1, s2)
#define CHECK_STRCASEEQ(s1, s2) CHECK_STROP(strcasecmp, ==, true, s1, s2)
#define CHECK_STRCASENE(s1, s2) CHECK_STROP(strcasecmp, !=, false, s1, s2)

#define CHECK_INDEX(I,A) CHECK(I < (sizeof(A)/sizeof(A[0])))
#define CHECK_BOUND(B,A) CHECK(B <= (sizeof(A)/sizeof(A[0])))

#define CHECK_DOUBLE_EQ(val1, val2)              \
  do {                                           \
    CHECK_LE((val1), (val2)+0.000000000000001L); \
    CHECK_GE((val1), (val2)-0.000000000000001L); \
  } while (0)

#define CHECK_NEAR(val1, val2, margin)           \
  do {                                           \
    CHECK_LE((val1), (val2)+(margin));           \
    CHECK_GE((val1), (val2)-(margin));           \
} while (0)

// Plus some debug-logging macros that get compiled to nothing for production

#if DCHECK_IS_ON()

#define DLOG(level) LOG(level)
#define DLOG_IF(level, boolean_expression) LOG_IF(level, boolean_expression)
#define DLOG_EVERY_N(level, n) LOG_EVERY_N(level, n)
#define DLOG_IF_EVERY_N(level, boolean_expression, n) \
  LOG_IF_EVERY_N(level, boolean_expression, n)
#define DLOG_ASSERT(boolean_expression) LOG_ASSERT(boolean_expression)

// debug-only checking.  executed if DCHECK_IS_ON().
#define DCHECK(boolean_expression) CHECK(boolean_expression)
#define DCHECK_EQ(val1, val2) CHECK_EQ(val1, val2)
#define DCHECK_NE(val1, val2) CHECK_NE(val1, val2)
#define DCHECK_LE(val1, val2) CHECK_LE(val1, val2)
#define DCHECK_LT(val1, val2) CHECK_LT(val1, val2)
#define DCHECK_GE(val1, val2) CHECK_GE(val1, val2)
#define DCHECK_GT(val1, val2) CHECK_GT(val1, val2)
#define DCHECK_NOTNULL(val) CHECK_NOTNULL(val)
#define DCHECK_STREQ(str1, str2) CHECK_STREQ(str1, str2)
#define DCHECK_STRCASEEQ(str1, str2) CHECK_STRCASEEQ(str1, str2)
#define DCHECK_STRNE(str1, str2) CHECK_STRNE(str1, str2)
#define DCHECK_STRCASENE(str1, str2) CHECK_STRCASENE(str1, str2)

#else  // !DCHECK_IS_ON()

#define DLOG(level) \
  true ? (void) 0 : @ac_google_namespace@::LogMessageVoidify() & LOG(level)

#define DLOG_IF(level, boolean_expression) \
  (true || !(boolean_expression)) ? (void) 0 : @ac_google_namespace@::LogMessageVoidify() & LOG(level)

#define DLOG_EVERY_N(level, n) \
  true ? (void) 0 : @ac_google_namespace@::LogMessageVoidify() & LOG(level)

#define DLOG_IF_EVERY_N(level, boolean_expression, n) \
  (true || !(boolean_expression))? (void) 0 : @ac_google_namespace@::LogMessageVoidify() & LOG(level)

#define DLOG_ASSERT(boolean_expression) \
  true ? (void) 0 : LOG_ASSERT(boolean_expression)

// MSVC warning C4127: conditional expression is constant
#define DCHECK(boolean_expression) \
  G3LOG_MSVC_PUSH_DISABLE_WARNING(4127) \
  while (false) \
    G3LOG_MSVC_POP_WARNING() CHECK(boolean_expression)

#define DCHECK_EQ(val1, val2) \
  G3LOG_MSVC_PUSH_DISABLE_WARNING(4127) \
  while (false) \
    G3LOG_MSVC_POP_WARNING() CHECK_EQ(val1, val2)

#define DCHECK_NE(val1, val2) \
  G3LOG_MSVC_PUSH_DISABLE_WARNING(4127) \
  while (false) \
    G3LOG_MSVC_POP_WARNING() CHECK_NE(val1, val2)

#define DCHECK_LE(val1, val2) \
  G3LOG_MSVC_PUSH_DISABLE_WARNING(4127) \
  while (false) \
    G3LOG_MSVC_POP_WARNING() CHECK_LE(val1, val2)

#define DCHECK_LT(val1, val2) \
  G3LOG_MSVC_PUSH_DISABLE_WARNING(4127) \
  while (false) \
    G3LOG_MSVC_POP_WARNING() CHECK_LT(val1, val2)

#define DCHECK_GE(val1, val2) \
  G3LOG_MSVC_PUSH_DISABLE_WARNING(4127) \
  while (false) \
    G3LOG_MSVC_POP_WARNING() CHECK_GE(val1, val2)

#define DCHECK_GT(val1, val2) \
  G3LOG_MSVC_PUSH_DISABLE_WARNING(4127) \
  while (false) \
    G3LOG_MSVC_POP_WARNING() CHECK_GT(val1, val2)

// You may see warnings in release mode if you don't use the return
// value of DCHECK_NOTNULL. Please just use DCHECK for such cases.
#define DCHECK_NOTNULL(val) (val)

#define DCHECK_STREQ(str1, str2) \
  G3LOG_MSVC_PUSH_DISABLE_WARNING(4127) \
  while (false) \
    G3LOG_MSVC_POP_WARNING() CHECK_STREQ(str1, str2)

#define DCHECK_STRCASEEQ(str1, str2) \
  G3LOG_MSVC_PUSH_DISABLE_WARNING(4127) \
  while (false) \
    G3LOG_MSVC_POP_WARNING() CHECK_STRCASEEQ(str1, str2)

#define DCHECK_STRNE(str1, str2) \
  G3LOG_MSVC_PUSH_DISABLE_WARNING(4127) \
  while (false) \
    G3LOG_MSVC_POP_WARNING() CHECK_STRNE(str1, str2)

#define DCHECK_STRCASENE(str1, str2) \
  G3LOG_MSVC_PUSH_DISABLE_WARNING(4127) \
  while (false) \
    G3LOG_MSVC_POP_WARNING() CHECK_STRCASENE(str1, str2)

#endif  // DCHECK_IS_ON()


// add some macros to avoid name conflicts

#define G3LOG_INFO INFO
#define G3LOG_WARNING WARNING
#define G3LOG_ERROR ERROR
#define G3LOG_FATAL FATAL
