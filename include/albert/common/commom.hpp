#pragma once


#include <memory>


namespace albert::common {

template <typename T>
using sp = std::shared_ptr<T>;
template <typename T>
using wp = std::weak_ptr<T>;
template <typename T>
using up = std::unique_ptr<T>;

}
