#pragma once
#include <memory>
#include <mutex>
#include <iostream>

template <typename T>
class Singleton {
protected:
	Singleton() = default;
	~Singleton() = default;
	Singleton(const Singleton<T>&) = delete;
	Singleton operator=(const Singleton<T>&) = delete;
public:
	static T& GetInstance() {
		static T _instance;
		return _instance;
	}

	void PrintAddress() {
		std::cout << "Singleton address: " << _instance.get(); << "\n";
	}
};
