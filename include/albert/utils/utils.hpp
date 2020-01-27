#pragma once

#include <cstddef>

#include <string>

namespace albert::dht::utils {
std::string hexdump(const void *ptr, size_t length, bool verbose);

template <typename T>
T host_to_network(T input);

template <typename T>
T network_to_host(T input);

template <>
uint32_t host_to_network<uint32_t>(uint32_t input);

template <>
uint16_t host_to_network<uint16_t>(uint16_t input);
}