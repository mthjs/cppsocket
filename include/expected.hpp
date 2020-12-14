#ifndef _CPPSOCKET_EXPECTED
#define _CPPSOCKET_EXPECTED

#include <exception>
#include <stdexcept>
#include <utility>

/**
 * Ungracefully nicked it from Andrei Alexandrescu's talk named "C++ and Beyond
 * 2012: Andrei Alexandrescu - Systematic Error Handling in C++"
 * (https://www.youtube.com/watch?v=kaI4R0Ng4E8).
 *
 * The basic concept behind this is that exceptions still can be used to
 * provide an error case to handle, but without the penalties associated with
 * `throw`
 */
template <typename T>
struct Expected
{
   template <typename E>
   static Expected<T> unexpected(const E& e)
   {
      if (typeid(e) != typeid(E))
         throw std::invalid_argument("Expected::unexpected: slicing detected");
      return Expected(std::make_exception_ptr(e));
   }

   Expected(const T& rhs)
      : __value(rhs)
      , __erred(false)
   {}

   Expected(T&& rhs)
      : __value(std::move(rhs))
      , __erred(false)
   {}

   Expected(const std::exception_ptr& rhs)
      : __exception(rhs)
      , __erred(true)
   {}

   Expected(std::exception_ptr&& rhs)
      : __exception(std::move(rhs))
      , __erred(true)
   {}

   Expected(const Expected& rhs)
      : __erred(rhs.__erred)
   {
      if (__erred) new(&__exception) std::exception_ptr(rhs.__exception);
      else new(&__value) T(rhs.__value);
   }

   Expected(Expected&& rhs)
      : __erred(rhs.__erred)
   {
      if (__erred) new(&__exception) std::exception_ptr(std::move(rhs.__exception));
      else new(&__value) T(std::move(rhs.__value));
   }

   ~Expected()
   {
      using std::exception_ptr;
      if (__erred) __exception.~exception_ptr();
      else __value.~T();
   }

   std::exception_ptr exception() {
      if (!__erred) return nullptr;
      return __exception;
   }

   bool erred() const { return __erred; }

   T& get()
   {
      if (__erred) std::rethrow_exception(__exception);
      return __value;
   }

   const T& get() const
   {
      if (__erred) std::rethrow_exception(__exception);
      return __value;
   }

   void swap(Expected& rhs)
   {
      if (!__erred) {
         if (!rhs.__erred) {
            // this causes std::swap to be a fallback when there is as swap
            // defined for T in the current namespace.
            using std::swap;
            swap(__value, rhs.__value);
         } else {
            // move rhs.__exception in a tmp value due to it being stored in
            // an union.
            auto tmp = std::move(rhs.__exception);
            new(&rhs.__value) T(std::move(__value));
            new(&__exception) std::exception_ptr(tmp);
            std::swap(__erred, rhs.__erred);
         }
      } else {
         if (!rhs.__erred) {
            rhs.wap(*this);
         } else {
            __exception.swap(rhs.swap);
            std::swap(__erred, rhs.__erred);
         }
      }
   }

private:
   Expected() = default;

private:
   bool __erred;
   union {
      T __value;
      std::exception_ptr __exception;
   };
};

#endif
