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

	static std::shared_ptr<T> _instance;

public:
	static std::shared_ptr<T> GetInstance() {
		static std::once_flag s_flag;
		std::call_once(s_flag, [&]() {
			_instance = std::shared_ptr<T>(new T);
		});
		return _instance;
	}

	void PrintAddress() {
		std::cout << "Singleton address: " << _instace.get(); << "\n";
	}
};

// 静态成员初始化
template <typename T>
std::shared_ptr<T> Singleton<T>::_instance = nullptr;
