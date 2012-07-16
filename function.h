#pragma once
#include "meta.h"

template<typename T> inline T* addressof(T& r) { return (T*)(&(char&)(const volatile char&)(r)); }

template<typename> struct is_integral_helper : public false_type { };
template<> struct is_integral_helper<bool> : public true_type { };
template<> struct is_integral_helper<char> : public true_type { };
template<> struct is_integral_helper<signed char> : public true_type { };
template<> struct is_integral_helper<unsigned char> : public true_type { };
template<> struct is_integral_helper<short> : public true_type { };
template<> struct is_integral_helper<unsigned short> : public true_type { };
template<> struct is_integral_helper<int> : public true_type { };
template<> struct is_integral_helper<unsigned int> : public true_type { };
template<> struct is_integral_helper<long> : public true_type { };
template<> struct is_integral_helper<unsigned long> : public true_type { };
template<> struct is_integral_helper<long long> : public true_type { };
template<> struct is_integral_helper<unsigned long long> : public true_type { };
template<typename T> struct is_integral :
        public integral_constant<bool, (is_integral_helper<typename remove_const<T>::type>::value)> {};

template<bool, typename T = void> struct enable_if {};
template<typename T> struct enable_if<true, T> { typedef T type; };

template<typename Signature> struct function;

class _Undefined_class;
union _Nocopy_types
{
  void*       _M_object;
  const void* _M_const_object;
  void (*_M_function_pointer)();
  void (_Undefined_class::*_M_member_pointer)();
};

union Any_data
{
  void*       _M_access()       { return &_M_pod_data[0]; }
  const void* _M_access() const { return &_M_pod_data[0]; }

  template<typename _Tp>
    _Tp&
    _M_access()
    { return *static_cast<_Tp*>(_M_access()); }

  template<typename _Tp>
    const _Tp&
    _M_access() const
    { return *static_cast<const _Tp*>(_M_access()); }

  _Nocopy_types _M_unused;
  char _M_pod_data[sizeof(_Nocopy_types)];
};

enum _Manager_operation
{
  __get_functor_ptr=1,
  __clone_functor,
  __destroy_functor
};
// Simple type wrapper that helps avoid annoying const problems
// when casting between void pointers and pointers-to-pointers.
template<typename _Tp>
  struct _Simple_type_wrapper
  {
    _Simple_type_wrapper(_Tp __value) : __value(__value) { }

    _Tp __value;
  };

  template<typename _Tp>
    struct __is_location_invariant
    : integral_constant<bool, (is_pointer<_Tp>::value
                   || is_member_pointer<_Tp>::value)>
    { };

template<typename _Tp>
  struct __is_location_invariant<_Simple_type_wrapper<_Tp> >
  : __is_location_invariant<_Tp>
  { };

/// Base class of all polymorphic function object wrappers.
class Function_base {
public:
  static const size_t _M_max_size = sizeof(_Nocopy_types);
  static const size_t _M_max_align = __alignof__(_Nocopy_types);

  template<typename Functor>
    class _Base_manager
    {
    protected:
  static const bool __stored_locally =
  (__is_location_invariant<Functor>::value
   && sizeof(Functor) <= _M_max_size
   && __alignof__(Functor) <= _M_max_align
   && (_M_max_align % __alignof__(Functor) == 0));

  typedef integral_constant<bool, __stored_locally> _Local_storage;

  // Retrieve a pointer to the function object
  static Functor*
  _M_get_pointer(const Any_data& __source)
  {
    const Functor* __ptr =
      __stored_locally? addressof(__source._M_access<Functor>())
      /* have stored a pointer */ : __source._M_access<Functor*>();
    return const_cast<Functor*>(__ptr);
  }

  // Clone a location-invariant function object that fits within
  // an Any_data structure.
  static void
  _M_clone(Any_data& __dest, const Any_data& __source, true_type)
  {
    new (__dest._M_access()) Functor(__source._M_access<Functor>());
  }

  // Clone a function object that is not location-invariant or
  // that cannot fit into an Any_data structure.
  static void
  _M_clone(Any_data& __dest, const Any_data& __source, false_type)
  {
    __dest._M_access<Functor*>() =
      new Functor(*__source._M_access<Functor*>());
  }

  // Destroying a location-invariant object may still require
  // destruction.
  static void
  _M_destroy(Any_data& __victim, true_type)
  {
    __victim._M_access<Functor>().~Functor();
  }

  // Destroying an object located on the heap.
  static void
  _M_destroy(Any_data& __victim, false_type)
  {
    delete __victim._M_access<Functor*>();
  }

    public:
  static bool
  _M_manager(Any_data& __dest, const Any_data& __source,
         _Manager_operation __op)
  {
    switch (__op)
      {
      case __get_functor_ptr:
        __dest._M_access<Functor*>() = _M_get_pointer(__source);
        break;

      case __clone_functor:
        _M_clone(__dest, __source, _Local_storage());
        break;

      case __destroy_functor:
        _M_destroy(__dest, _Local_storage());
        break;
      }
    return false;
  }

  static void
  _M_init_functor(Any_data& __functor, Functor&& __f)
  { _M_init_functor(__functor, move(__f), _Local_storage()); }

  template<typename _Signature>
    static bool
    _M_not_empty_function(const function<_Signature>& __f)
    { return static_cast<bool>(__f); }

  template<typename _Tp>
    static bool
    _M_not_empty_function(const _Tp*& __fp)
    { return __fp; }

  template<typename _Class, typename _Tp>
    static bool
    _M_not_empty_function(_Tp _Class::* const& __mp)
    { return __mp; }

  template<typename _Tp>
    static bool
    _M_not_empty_function(const _Tp&)
    { return true; }

    private:
  static void
  _M_init_functor(Any_data& __functor, Functor&& __f, true_type)
  { new (__functor._M_access()) Functor(move(__f)); }

  static void
  _M_init_functor(Any_data& __functor, Functor&& __f, false_type)
  { __functor._M_access<Functor*>() = new Functor(move(__f)); }
    };

  template<typename Functor>
    class _Ref_manager : public _Base_manager<Functor*>
    {
  typedef Function_base::_Base_manager<Functor*> _Base;

  public:
  static bool
  _M_manager(Any_data& __dest, const Any_data& __source,
         _Manager_operation __op)
  {
    switch (__op)
      {
      case __get_functor_ptr:
        __dest._M_access<Functor*>() = *_Base::_M_get_pointer(__source);
        return is_const<Functor>::value;
        break;

      default:
        _Base::_M_manager(__dest, __source, __op);
      }
    return false;
  }
    };

  Function_base() : _M_manager(0) { }

  ~Function_base()
  {
    if (_M_manager)
  _M_manager(_M_functor, _M_functor, __destroy_functor);
  }


  bool _M_empty() const { return !_M_manager; }

  typedef bool (*_Manager_type)(Any_data&, const Any_data&,
                _Manager_operation);

  Any_data     _M_functor;
  _Manager_type _M_manager;
};

template<typename _Signature, typename Functor>
  class _Function_handler;

template<typename R, typename Functor, typename... Args>
  class _Function_handler<R(Args...), Functor>
  : public Function_base::_Base_manager<Functor>
  {
    typedef Function_base::_Base_manager<Functor> _Base;

  public:
    static R
    _M_invoke(const Any_data& __functor, Args... __args)
    {
  return (*_Base::_M_get_pointer(__functor))(
      forward<Args>(__args)...);
    }
  };

template<typename Functor, typename... Args>
  class _Function_handler<void(Args...), Functor>
  : public Function_base::_Base_manager<Functor>
  {
    typedef Function_base::_Base_manager<Functor> _Base;

   public:
    static void
    _M_invoke(const Any_data& __functor, Args... __args)
    {
  (*_Base::_M_get_pointer(__functor))(
      forward<Args>(__args)...);
    }
  };
template<typename _Class, typename _Member, typename R,
     typename... Args>
  class _Function_handler<R(Args...), _Member _Class::*>
  : public _Function_handler<void(Args...), _Member _Class::*>
  {
    typedef _Function_handler<void(Args...), _Member _Class::*>
  _Base;

   public:
    static R
    _M_invoke(const Any_data& __functor, Args... __args)
    {
  return mem_fn(_Base::_M_get_pointer(__functor)->__value)(
      forward<Args>(__args)...);
    }
  };

template<typename _Class, typename _Member, typename... Args>
  class _Function_handler<void(Args...), _Member _Class::*>
  : public Function_base::_Base_manager<
       _Simple_type_wrapper< _Member _Class::* > >
  {
    typedef _Member _Class::* Functor;
    typedef _Simple_type_wrapper<Functor> _Wrapper;
    typedef Function_base::_Base_manager<_Wrapper> _Base;

   public:
    static bool
    _M_manager(Any_data& __dest, const Any_data& __source,
       _Manager_operation __op)
    {
  switch (__op)
    {
    case __get_functor_ptr:
      __dest._M_access<Functor*>() =
        &_Base::_M_get_pointer(__source)->__value;
      break;

    default:
      _Base::_M_manager(__dest, __source, __op);
    }
  return false;
    }

    static void
    _M_invoke(const Any_data& __functor, Args... __args)
    {
  mem_fn(_Base::_M_get_pointer(__functor)->__value)(
      forward<Args>(__args)...);
    }
  };

template<typename R, typename... Args> struct function<R(Args...)> : private Function_base {
private:
    typedef R _Signature_type(Args...);

    struct _Useless { };

  public:
    typedef R result_type;

    // [3.7.2.1] construct/copy/destroy

    /**
     *  @brief Default construct creates an empty function call wrapper.
     *  @post @c !(bool)*this
     */
    function() noexcept
    : Function_base() { }

    /**
     *  @brief %Function copy constructor.
     *  @param __x A %function object with identical call signature.
     *  @post @c bool(*this) == bool(__x)
     *
     *  The newly-created %function contains a copy of the target of @a
     *  __x (if it has one).
     */
    function(const function& __x);

    /**
     *  @brief %Function move constructor.
     *  @param __x A %function object rvalue with identical call signature.
     *
     *  The newly-created %function contains the target of @a __x
     *  (if it has one).
     */
    function(function&& __x) : Function_base()
    {
  __x.swap(*this);
    }

    // TODO: needs allocator_arg_t

    /**
     *  @brief Builds a %function that targets a copy of the incoming
     *  function object.
     *  @param __f A %function object that is callable with parameters of
     *  type @c T1, @c T2, ..., @c TN and returns a value convertible
     *  to @c Res.
     *
     *  The newly-created %function object will target a copy of
     *  @a __f. If @a __f is @c reference_wrapper<F>, then this function
     *  object will contain a reference to the function object @c
     *  __f.get(). If @a __f is a NULL function pointer or NULL
     *  pointer-to-member, the newly-created object will be empty.
     *
     *  If @a __f is a non-NULL function pointer or an object of type @c
     *  reference_wrapper<F>, this function will not throw.
     */
    template<typename Functor>
  function(Functor __f,
       typename enable_if<
             !is_integral<Functor>::value, _Useless>::type
         = _Useless());

    /**
     *  @brief %Function assignment operator.
     *  @param __x A %function with identical call signature.
     *  @post @c (bool)*this == (bool)x
     *  @returns @c *this
     *
     *  The target of @a __x is copied to @c *this. If @a __x has no
     *  target, then @c *this will be empty.
     *
     *  If @a __x targets a function pointer or a reference to a function
     *  object, then this operation will not throw an %exception.
     */
    function&
    operator=(const function& __x)
    {
  function(__x).swap(*this);
  return *this;
    }

    /**
     *  @brief %Function move-assignment operator.
     *  @param __x A %function rvalue with identical call signature.
     *  @returns @c *this
     *
     *  The target of @a __x is moved to @c *this. If @a __x has no
     *  target, then @c *this will be empty.
     *
     *  If @a __x targets a function pointer or a reference to a function
     *  object, then this operation will not throw an %exception.
     */
    function&
    operator=(function&& __x)
    {
  function(move(__x)).swap(*this);
  return *this;
    }

    /**
     *  @brief %Function assignment to a new target.
     *  @param __f A %function object that is callable with parameters of
     *  type @c T1, @c T2, ..., @c TN and returns a value convertible
     *  to @c Res.
     *  @return @c *this
     *
     *  This  %function object wrapper will target a copy of @a
     *  __f. If @a __f is @c reference_wrapper<F>, then this function
     *  object will contain a reference to the function object @c
     *  __f.get(). If @a __f is a NULL function pointer or NULL
     *  pointer-to-member, @c this object will be empty.
     *
     *  If @a __f is a non-NULL function pointer or an object of type @c
     *  reference_wrapper<F>, this function will not throw.
     */
    template<typename Functor>
  typename enable_if<!is_integral<Functor>::value, function&>::type
  operator=(Functor&& __f)
  {
    function(forward<Functor>(__f)).swap(*this);
    return *this;
  }

    // [3.7.2.2] function modifiers

    /**
     *  @brief Swap the targets of two %function objects.
     *  @param __x A %function with identical call signature.
     *
     *  Swap the targets of @c this function object and @a __f. This
     *  function will not throw an %exception.
     */
    void swap(function& __x)
    {
  ::swap(_M_functor, __x._M_functor);
  ::swap(_M_manager, __x._M_manager);
  ::swap(_M_invoker, __x._M_invoker);
    }

    // TODO: needs allocator_arg_t
    /*
    template<typename Functor, typename _Alloc>
  void
  assign(Functor&& __f, const _Alloc& __a)
  {
    function(allocator_arg, __a,
         forward<Functor>(__f)).swap(*this);
  }
    */

    // [3.7.2.3] function capacity

    /**
     *  @brief Determine if the %function wrapper has a target.
     *
     *  @return @c true when this %function object contains a target,
     *  or @c false when it is empty.
     *
     *  This function will not throw an %exception.
     */
    explicit operator bool() const noexcept
    { return !_M_empty(); }

    // [3.7.2.4] function invocation

    /**
     *  @brief Invokes the function targeted by @c *this.
     *  @returns the result of the target.
     *  @throws bad_function_call when @c !(bool)*this
     *
     *  The function call operator invokes the target function object
     *  stored by @c this.
     */
    R operator()(Args... __args) const;

  private:
    typedef R (*_Invoker_type)(const Any_data&, Args...);
    _Invoker_type _M_invoker;
};

// Out-of-line member definitions.
template<typename R, typename... Args>
  function<R(Args...)>::
  function(const function& __x)
  : Function_base()
  {
    if (static_cast<bool>(__x))
  {
    _M_invoker = __x._M_invoker;
    _M_manager = __x._M_manager;
    __x._M_manager(_M_functor, __x._M_functor, __clone_functor);
  }
  }

template<typename R, typename... Args>
  template<typename Functor>
    function<R(Args...)>::
    function(Functor __f,
         typename enable_if<
          !is_integral<Functor>::value, _Useless>::type)
    : Function_base()
    {
  typedef _Function_handler<_Signature_type, Functor> _My_handler;

  if (_My_handler::_M_not_empty_function(__f))
    {
      _M_invoker = &_My_handler::_M_invoke;
      _M_manager = &_My_handler::_M_manager;
      _My_handler::_M_init_functor(_M_functor, move(__f));
    }
    }

template<typename R, typename... Args>
  R
  function<R(Args...)>::
  operator()(Args... __args) const
  {
    assert_(_M_empty());
    return _M_invoker(_M_functor, forward<Args>(__args)...);
  }
