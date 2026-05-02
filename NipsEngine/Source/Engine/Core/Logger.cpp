#include "Core/Logger.h"

#include <Windows.h>
#include <cstdarg>
#include <cstdio>

namespace
{
	FLogger::FLogMessage GLogMessage = nullptr;
}

void FLogger::SetMessage(FLogMessage InSink)
{
	GLogMessage = InSink;
}

void FLogger::ClearMessage(FLogMessage InSink)
{
	if (InSink == nullptr || GLogMessage == InSink)
	{
		GLogMessage = nullptr;
	}
}

void FLogger::Log(const char* Format, ...)
{
	char Buffer[2048];

	va_list Args;
	va_start(Args, Format);
	vsnprintf(Buffer, sizeof(Buffer), Format, Args);
	va_end(Args);

	if (GLogMessage)
	{
		GLogMessage(Buffer);
		return;
	}

	OutputDebugStringA(Buffer);
	OutputDebugStringA("\n");
}
