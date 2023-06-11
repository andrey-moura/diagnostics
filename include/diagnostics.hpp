#pragma once

#include <chrono>
#include <functional>
#include <type_traits>

namespace uva
{
	namespace diagnostics
	{
		template<typename functor, typename... Args>
		auto measure_function(const functor& function, Args&&... args) {
			if constexpr (std::is_same<void, decltype(function(std::forward<Args>(args)...))>::value) {
				auto start = std::chrono::high_resolution_clock::now();
				function(std::forward<Args>(args)...);
				auto end = std::chrono::high_resolution_clock::now();
				return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
			}
			else {
				auto start = std::chrono::high_resolution_clock::now();
				auto ret = function(std::forward<Args>(args)...);
				auto end = std::chrono::high_resolution_clock::now();
				return std::pair{ std::move(ret), std::chrono::duration_cast<std::chrono::nanoseconds>(end - start) };
			}
		}
	};
};