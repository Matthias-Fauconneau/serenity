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

template<typename Signature> class function;

class _Undefined_class;
union _Nocopy_types {
  void*       _M_object;
  const void* _M_const_object;
  void (*_M_function_pointer)();
  void (_Undefined_class::*_M_member_pointer)();
};

union Any_data {
  void*       _M_access()       { return &_M_pod_data[0]; }
  const void* _M_access() const { return &_M_pod_data[0]; }

  template<typename _Tp> _Tp& _M_access() { return *static_cast<_Tp*>(_M_access()); }
  template<typename _Tp> const _Tp& _M_access() const { return *static_cast<const _Tp*>(_M_access()); }

  _Nocopy_types _M_unused;
  char _M_pod_data[sizeof(_Nocopy_types)];
};

enum _Manager_operation { get_functor_ptr=1, clone_functor, destroy_functor };
// Simple type wrapper that helps avoid annoying const problems
// when casting between void pointers and pointers-to-pointers.
template<typename _Tp> struct _Simple_type_wrapper { _Simple_type_wrapper(_Tp value) : value(value) {} _Tp value; };
template<typename _Tp> struct is_location_invariant : integral_constant<bool, (is_pointer<_Tp>::value || is_member_pointer<_Tp>::value)>{};
template<typename _Tp> struct is_location_invariant<_Simple_type_wrapper<_Tp> > : is_location_invariant<_Tp> {};

/// Base class of all polymorphic function object wrappers.
class Function_base {
public:
  static const uint _M_max_size = sizeof(_Nocopy_types);
  static const uint _M_max_align = alignof(_Nocopy_types);

  template<typename Functor> class _Base_manager {
  protected:
      static const bool stored_locally =
              is_location_invariant<Functor>::value
              && sizeof(Functor) <= _M_max_size
              && __alignof(Functor) <= _M_max_align
              && (_M_max_align % __alignof__(Functor) == 0);

      typedef integral_constant<bool, stored_locally> _Local_storage;

      // Retrieve a pointer to the function object
      static Functor* _M_get_pointer(const Any_data& source) {
          const Functor* ptr = stored_locally? addressof(source._M_access<Functor>()) : source._M_access<Functor*>();
          return const_cast<Functor*>(ptr);
      }

      // Destroying a location-invariant object may still require destruction.
      static void _M_destroy(Any_data& victim, true_type) { victim._M_access<Functor>().~Functor(); }
      // Destroying an object located on the heap.
      static void _M_destroy(Any_data& victim, false_type) { delete victim._M_access<Functor*>(); }

  public:
      static bool _M_manager(Any_data& dest, const Any_data& source, _Manager_operation op) {
          if(op==get_functor_ptr) dest._M_access<Functor*>() = _M_get_pointer(source);
          else if(op==destroy_functor) _M_destroy(dest, _Local_storage());
          else error("");
          return false;
      }

      static void _M_init_functor(Any_data& functor, Functor&& f) { _M_init_functor(functor, move(f), _Local_storage()); }

      template<typename _Signature> static bool _M_not_empty_function(const function<_Signature>& f) { return static_cast<bool>(f); }
      template<typename _Tp> static bool _M_not_empty_function(const _Tp*& fp) { return fp; }
      template<typename _Class, typename _Tp> static bool _M_not_empty_function(_Tp _Class::* const& mp) { return mp; }
      template<typename _Tp> static bool _M_not_empty_function(const _Tp&) { return true; }

  private:
      static void _M_init_functor(Any_data& functor, Functor&& f, true_type) { new (functor._M_access()) Functor(move(f)); }
      static void _M_init_functor(Any_data& unused functor, Functor&& unused f, false_type) { error(""); }
      //functor._M_access<Functor*>() = alloc<Functor>(move(f));
};

template<typename Functor> class _Ref_manager : public _Base_manager<Functor*> {
    typedef Function_base::_Base_manager<Functor*> _Base;
public:
    static bool _M_manager(Any_data& dest, const Any_data& source, _Manager_operation op) {
        if(op==get_functor_ptr) {
            dest._M_access<Functor*>() = *_Base::_M_get_pointer(source);
            return is_const<Functor>::value;
        } else _Base::_M_manager(dest, source, op);
        return false;
    }
};

Function_base() : _M_manager(0) { }
~Function_base() { if (_M_manager) _M_manager(_M_functor, _M_functor, destroy_functor); }

bool _M_empty() const { return !_M_manager; }

typedef bool (*_Manager_type)(Any_data&, const Any_data&, _Manager_operation);

Any_data     _M_functor;
_Manager_type _M_manager;
};

template<typename _Signature, typename Functor> struct _Function_handler;
template<typename R, typename Functor, typename... Args>
struct _Function_handler<R(Args...), Functor> : Function_base::_Base_manager<Functor> {
    typedef Function_base::_Base_manager<Functor> _Base;
    static R _M_invoke(const Any_data& functor, Args... args) { return (*_Base::_M_get_pointer(functor))(forward<Args>(args)...); }
};

template<typename Functor, typename... Args>
struct _Function_handler<void(Args...), Functor> : Function_base::_Base_manager<Functor> {
    typedef Function_base::_Base_manager<Functor> _Base;
    static void _M_invoke(const Any_data& functor, Args... args) { (*_Base::_M_get_pointer(functor))(forward<Args>(args)...); }
};
template<typename _Class, typename _Member, typename R, typename... Args>
struct _Function_handler<R(Args...), _Member _Class::*> : _Function_handler<void(Args...), _Member _Class::*> {
    typedef _Function_handler<void(Args...), _Member _Class::*> _Base;

    static R _M_invoke(const Any_data& functor, Args... args) {
        return mem_fn(_Base::_M_get_pointer(functor)->value)(forward<Args>(args)...);
    }
};

template<typename _Class, typename _Member, typename... Args>
struct _Function_handler<void(Args...), _Member _Class::*> : Function_base::_Base_manager<_Simple_type_wrapper< _Member _Class::* > > {
    typedef _Member _Class::* Functor;
    typedef _Simple_type_wrapper<Functor> _Wrapper;
    typedef Function_base::_Base_manager<_Wrapper> _Base;

    static bool _M_manager(Any_data& dest, const Any_data& source, _Manager_operation op) {
        if(op==get_functor_ptr) dest._M_access<Functor*>() = &_Base::_M_get_pointer(source)->value;
        else _Base::_M_manager(dest, source, op);
        return false;
    }

    static void _M_invoke(const Any_data& functor, Args... args) {
        mem_fn(_Base::_M_get_pointer(functor)->value)(forward<Args>(args)...);
    }
};

template<typename R, typename... Args> class function<R(Args...)> : Function_base {
    typedef R _Signature_type(Args...);
    struct _Useless {};
    public:
    typedef R result_type;

    function(const function& x) : Function_base() {
      if (static_cast<bool>(x)) {
          _M_invoker = x._M_invoker;
          _M_manager = x._M_manager;
          x._M_manager(_M_functor, x._M_functor, clone_functor);
      }
    }
    template<typename Functor> function(Functor f, typename enable_if<!is_integral<Functor>::value, _Useless>::type = _Useless()) {
        typedef _Function_handler<_Signature_type, Functor> _My_handler;

        if (_My_handler::_M_not_empty_function(f)) {
            _M_invoker = &_My_handler::_M_invoke;
            _M_manager = &_My_handler::_M_manager;
            _My_handler::_M_init_functor(_M_functor, move(f));
        }
    }

    explicit operator bool() const noexcept { return !_M_empty(); }
    R operator()(Args... args) const {
        assert_(!_M_empty());
        return _M_invoker(_M_functor, forward<Args>(args)...);
    }

  private:
    typedef R (*_Invoker_type)(const Any_data&, Args...);
    _Invoker_type _M_invoker;
};
