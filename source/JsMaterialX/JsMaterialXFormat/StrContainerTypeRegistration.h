//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
//

#include <MaterialXFormat/File.h>
#include <emscripten/bind.h>

namespace ems = emscripten;
namespace mx = MaterialX;

template<typename T> struct IsStrContainer : std::false_type {};
template<> struct IsStrContainer<mx::FileSearchPath> : std::true_type {};
template<> struct IsStrContainer<mx::FilePath> : std::true_type {};

using StrContainerIntermediate = std::string;

namespace emscripten
{

namespace internal
{

// Primary specialization for value types (e.g. FilePath, FileSearchPath).
template<typename T>
struct TypeID<T, typename std::enable_if<IsStrContainer<typename std::remove_cv<typename std::remove_reference<T>::type>::type>::value && !std::is_reference<T>::value, void>::type> {
  static constexpr TYPEID get() {
    return TypeID<StrContainerIntermediate>::get();
  }
};

// Explicit specialization for const-ref variants to resolve ambiguity with
// Emscripten's TypeID<T&> partial specialization (Emscripten 4.x+).
template<> struct TypeID<const mx::FilePath&> {
  static constexpr TYPEID get() { return TypeID<StrContainerIntermediate>::get(); }
};
template<> struct TypeID<const mx::FileSearchPath&> {
  static constexpr TYPEID get() { return TypeID<StrContainerIntermediate>::get(); }
};

template<typename T>
struct BindingType<T, typename std::enable_if<IsStrContainer<T>::value, void>::type> {
  typedef typename BindingType<StrContainerIntermediate>::WireType WireType;

  constexpr static WireType toWireType(const T& v) {
    return BindingType<StrContainerIntermediate>::toWireType(v.asString());
  }
  constexpr static T fromWireType(WireType v) {
    return T(BindingType<StrContainerIntermediate>::fromWireType(v));
  }
};

} // namespace internal

} // namespace emscripten
