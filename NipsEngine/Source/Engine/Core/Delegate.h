#pragma once

#include <functional>
#include <vector>

bool IsUObjectAlive(const void* Ptr);

#define DECLARE_DELEGATE(Name, ...) using Name = TDelegate<__VA_ARGS__>

template<typename... Args>
class TDelegate
{
public:
	using HandlerType = std::function<void(Args...)>;

	void Add(const HandlerType& handler)
	{
        Handlers.push_back(handler);
	}

	template <typename T> 
	void AddRaw(T* Instance, void (T::* Func)(Args...))
	{
        Handlers.push_back([Instance, Func](Args... args)
        { 
			(Instance->*Func)(args...);
	    });
	}

	template <typename T>
	void AddDynamic(T* Instance, void (T::* Func)(Args...))
	{
        const void* ObjPtr = static_cast<const void*>(Instance);
        Handlers.push_back([ObjPtr, Instance, Func](Args... args)
        { 
			if (IsUObjectAlive(ObjPtr))
				(Instance->*Func)(args...);
	    });
	}

	void Broadcast(Args... args)
	{
		for (auto& handler : Handlers)
		{
			if (handler)
			{
                handler(args...);
			}
		}
	}

	void Clear()
    {
        Handlers.clear();
    }

	private:
		std::vector<HandlerType> Handlers;
};