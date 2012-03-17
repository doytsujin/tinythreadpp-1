/*
Copyright (c) 2012 Jared Duke

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
    claim that you wrote the original software. If you use this software
    in a product, an acknowledgment in the product documentation would be
    appreciated but is not required.

    2. Altered source versions must be plainly marked as such, and must not be
    misrepresented as being the original software.

    3. This notice may not be removed or altered from any source
    distribution.
*/

#ifndef _TINYTHREAD_EXPERIMENTAL_H_
#define _TINYTHREAD_EXPERIMENTAL_H_

/// @file

#include "tinythread.h"
#include "tinythread_t.h"
#include "fast_mutex.h"

#include <memory>
#include <stdexcept>

#define _TTHREAD_EXPERIMENTAL_

#if !defined(_MSC_VER)
#define _TTHREAD_VARIADIC_
#endif

// Macro for disabling assignments of objects.
#ifdef _TTHREAD_CPP0X_PARTIAL_
#define _TTHREAD_DISABLE_ASSIGNMENT(name) \
  name(const name&) = delete; \
  name& operator=(const name&) = delete;
#else
#define _TTHREAD_DISABLE_ASSIGNMENT(name) \
  name(const name&); \
  name& operator=(const name&);
#endif

namespace tthread {

typedef mutex future_mutex;
typedef lock_guard<future_mutex> lock;

template< class >
class packaged_task;

template < class >
class future;

template< class F >
auto async(F f) -> future<decltype(f())>;

///////////////////////////////////////////////////////////////////////////
// async_result

template<class R>
struct result_helper {
  typedef R type;
  template< class T, class F >
  static void store(T& receiver, F& func) {
    receiver.reset( new R( func() ) );
  }
  static R fetch(R* r) { return *r; }
};

template<>
struct result_helper<void> {
  typedef bool type;
  template< class T, class F >
  static void store(T& receiver, F& func) {
    func();
    receiver.reset( new bool(true) );
  }
  static void fetch(...) { }
};

template< class R >
struct async_result {

  std::unique_ptr<typename result_helper<R>::type > mResult;
  bool                       mException;
  mutable future_mutex       mResultLock;
  mutable condition_variable mResultCondition;

  bool ready() const {
    lock guard(mResultLock);
    return mResult || mException;
  }

  template<class> friend class packaged_task;

protected:
  async_result() : mException(false) { }

  _TTHREAD_DISABLE_ASSIGNMENT(async_result);
};

///////////////////////////////////////////////////////////////////////////
// packaged_task

template< class T >
class packaged_task_continuation {
  virtual void operator()(T t) = 0;
};
template< >
class packaged_task_continuation< void > {
  virtual void operator()(void) = 0;
};

#if defined(_TTHREAD_VARIADIC_)

template< class R >
struct result_helper_v {
  template<class T, class F, class... Args>
  static void store(T& receiver, F& func, Args&&... args) {
    receiver.reset( new R( func( std::forward<Args...>(args)... ) ) );
  }
};

template< >
struct result_helper_v<void> {
  template<class T, class F, class... Args>
  static void store(T& receiver, F& func, Args&&... args) {
    func( std::forward<Args...>(args)... );
    receiver.reset( new bool(true) );
  }
};

template< class R, class... Args >
class packaged_task<R(Args...)> : public packaged_task_continuation<void> {
public:
  typedef R result_type;

  ///////////////////////////////////////////////////////////////////////////
  // construction and destruction

  packaged_task() { }
  ~packaged_task() { }

  explicit packaged_task(R(*f)(Args...)) : mFunc( f ) { }
  template <class F>
  explicit packaged_task(const F& f)     : mFunc( f ) { }
  template <class F>
  explicit packaged_task(F&& f)          : mFunc( std::move( f ) ) { }

  ///////////////////////////////////////////////////////////////////////////
  // move support

  packaged_task(packaged_task&& other) {
    *this = std::move(other);
  }

  packaged_task& operator=(packaged_task&& other) {
    swap( std::move(other) );
    return *this;
  }

  void swap(packaged_task&& other) {
    lock guard(mLock);
    std::swap(mFunc,   other.mFunc);
    std::swap(mResult, other.mResult);
  }

  ///////////////////////////////////////////////////////////////////////////
  // result retrieval

  operator bool() const {
    lock guard(mLock);
    return !!mFunc;
  }

  future<R> get_future() {
    lock guard(mLock);
    if (!mResult)
      mResult.reset( new async_result<R>() );
    return future<R>(mResult);
  }

  ///////////////////////////////////////////////////////////////////////////
  // execution

  void operator()(void*) { (*this)(); }
  void operator()(Args&&...);

  void reset() {
    lock guard(mLock);
    mResult.reset( );
  }

private:
  _TTHREAD_DISABLE_ASSIGNMENT(packaged_task);

  mutable future_mutex mLock;
  std::function<R(Args...)> mFunc;
  std::shared_ptr< async_result<R> > mResult;
};


///////////////////////////////////////////////////////////////////////////

template< class R, class... Args >
void tthread::packaged_task<R(Args...)>::operator()(Args&&... args)
{
  if (!(*this))
    return;

  std::shared_ptr< async_result<R> > result;
  {
    lock guard(mLock);
    if (!mResult)
      mResult.reset( new async_result<R>() );
    result = mResult;
  }

  lock guardResult(result->mResultLock);

  if(!result->mResult)
    result_helper_v<R>::store(result->mResult, mFunc, std::forward<Args...>(args)... );

  result->mResultCondition.notify_all();
}

#else

template< class R >
class packaged_task<R(void)> : public packaged_task_continuation<void> {
public:
  typedef R result_type;

  ///////////////////////////////////////////////////////////////////////////
  // construction and destruction

  packaged_task() { }
  ~packaged_task() { }

  explicit packaged_task(R(*f)())    : mFunc( f ) { }
  template <class F>
  explicit packaged_task(const F& f) : mFunc( f ) { }
  template <class F>
  explicit packaged_task(F&& f)      : mFunc( std::move( f ) ) { }

  ///////////////////////////////////////////////////////////////////////////
  // move support

  packaged_task(packaged_task&& other) {
    *this = std::move(other);
  }

  packaged_task& operator=(packaged_task&& other) {
    swap( std::move(other) );
    return *this;
  }

  void swap(packaged_task&& other)
  {
    lock guard(mLock);
    std::swap(mFunc,   other.mFunc);
    std::swap(mResult, other.mResult);
  }

  ///////////////////////////////////////////////////////////////////////////
  // result retrieval

  operator bool() const {
    lock guard(mLock);
    return !!mFunc;
  }

  future<R> get_future() {
    lock guard(mLock);
    if (!mResult)
      mResult.reset( new async_result<R>() );
    return future<R>(mResult);
  }

  ///////////////////////////////////////////////////////////////////////////
  // execution

  void operator()(void*) { (*this)(); }
  void operator()();

  void reset() {
    lock guard(mLock);
    mResult.reset( );
  }

private:
  _TTHREAD_DISABLE_ASSIGNMENT(packaged_task);

  mutable future_mutex mLock;
  std::function<R()>   mFunc;
  std::shared_ptr< async_result<R> > mResult;
};

///////////////////////////////////////////////////////////////////////////

template< class R >
void tthread::packaged_task<R()>::operator()()
{
  if (!(*this))
    return;

  std::shared_ptr< async_result<R> > result;
  {
    lock guard(mLock);
    if (!mResult)
      mResult.reset( new async_result<R>() );
    result = mResult;
  }

  lock guardResult(result->mResultLock);

  if(!result->mResult)
    result_helper<R>::store(result->mResult, mFunc);

  result->mResultCondition.notify_all();
}

#endif

///////////////////////////////////////////////////////////////////////////
/// Future class.

template< class R >
class future {
public:

  ~future() { }
  future(future<R>&& f) : mResult( f.mResult ) { }
  future& operator=(future&& other) { std::swap(mResult, other.mResult); }

  bool valid() const     { return mResult; }
  bool is_ready() const  { return valid() && mResult->ready(); }
  bool has_value() const { return is_ready(); }

  R    get();
  void wait();

  template< class F >
  auto then( const F& f ) -> future<decltype(f())> {

    std::shared_ptr< async_result<R> > pResult = mResult;

    if (!valid())
      throw std::exception("invalid future");

    // TODO: Create a continuation
    //lock guard(pResult->mResultLock);
    //if (is_ready())
    return async(f, get());
  }

  template< class > friend class packaged_task;

protected:
  future() { }
  future(std::shared_ptr< async_result<R> >& result) : mResult(result) { }

  _TTHREAD_DISABLE_ASSIGNMENT(future)

  std::shared_ptr< async_result<R> > mResult;
};

///////////////////////////////////////////////////////////////////////////

template< class R >
R tthread::future<R>::get()
{
  std::shared_ptr< async_result<R> > pResult = mResult;
  if (!pResult)
    throw std::runtime_error("invalid future");

  wait();

  const async_result<R>& result = *pResult;

  if(result.mException || !result.mResult)
    throw std::runtime_error("invalid future");

  return result_helper<R>::fetch(result.mResult.get());
}

template< class R >
void tthread::future<R>::wait()
{
  std::shared_ptr< async_result<R> > pResult = mResult;
  if (!pResult)
    return;

  const async_result<R>& result = *pResult;

  lock guard(result.mResultLock);
  while (!result.mResult && !result.mException)
  {
    result.mResultCondition.wait(result.mResultLock);
  }
}

///////////////////////////////////////////////////////////////////////////

template< class F >
auto async(F f) -> future<decltype(f())>
{
  typedef decltype(f())                result_type;
  typedef packaged_task<result_type()> task_type;
  typedef future<result_type>          future_type;

  task_type task(std::move(f));
  auto future = task.get_future();
  threadt thread(std::move(task));
  thread.detach();
  return future;
}

template< class F, class T >
auto async(F f, T t) -> future<decltype(f(t))> {
  return async(std::bind(f, t));
}

template< class F, class T, class U >
auto async(F f, T t, U u) -> future<decltype(f(t,u))> {
  return async(std::bind(f, t, u));
}

template< class F, class T, class U, class V >
auto async(F f, T t, U u, V v) -> future<decltype(f(t,u,v))> {
  return async(std::bind(f, t, u, v));
}

template< class F, class T, class U, class V, class W >
auto async(F f, T t, U u, V v, W w) -> future<decltype(f(t,u,v,w))> {
  return async(std::bind(f, t, u, v));
}

}

#undef _TTHREAD_DISABLE_ASSIGNMENT

#endif // _TINYTHREAD_EXPERIMENTAL_H_
