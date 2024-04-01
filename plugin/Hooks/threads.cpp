#include "hooks.h"
#include "DMALibrary/Memory/Memory.h"

namespace Hooks
{
	typedef NTSTATUS (__stdcall*PsLookupThreadByThreadId)(HANDLE ThreadId, /*PETHREAD*/void* Thread);
	typedef NTSTATUS (__stdcall*PsGetContextThread)(/*PETHREAD*/void* Thread, PCONTEXT Context, KPROCESSOR_MODE PreviousMode);
	typedef NTSTATUS (__stdcall*PsSetContextThread)(/*PETHREAD*/void* Thread, PCONTEXT Context, KPROCESSOR_MODE PreviousMode);
	typedef NTSTATUS (__stdcall*PsSuspendThread)(/*PETHREAD*/void* Thread, PULONG PreviousSuspendCount);
	typedef NTSTATUS (__stdcall*PsResumeThread)(/*PETHREAD*/void* Thread, PULONG PreviousSuspendCount);

	template <typename T, typename... Args>
	auto SysCall(uint64_t function, Args&&... args) -> std::enable_if_t<!std::is_void<std::invoke_result_t<T, Args...>>::value, std::invoke_result_t<T, Args...>>
	{
		uintptr_t ntos_shutdown = mem.GetExportTableAddress("NtShutdownSystem", "csrss.exe", "ntoskrnl.exe");
		uint64_t nt_shutdown = (uint64_t)GetProcAddress(LoadLibraryA("ntdll.dll"), "NtShutdownSystem");

		if (!function)
		{
			printf("[!] Failed to get function address\n");
			return { };
		}

		if (ntos_shutdown == 0 || nt_shutdown == 0)
		{
			printf("[!] Failed to get NtShutdownSystem address\n");
			return { };
		}

		BYTE jmp_bytes[14] = {
			0xff, 0x25, 0x00, 0x00, 0x00, 0x00, // jmp [RIP+0x00000000]
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 // RIP value
		};
		*reinterpret_cast<uint64_t*>(jmp_bytes + 6) = function;

		// Save original bytes
		BYTE orig_bytes[sizeof(jmp_bytes)];
		if (!mem.Read(ntos_shutdown, (PBYTE)orig_bytes, sizeof(orig_bytes), 4))
			return { };

		if (!mem.Write(ntos_shutdown, jmp_bytes, sizeof(jmp_bytes), 4))
		{
			printf("[!] Failed to write memory at 0x%p\n", ntos_shutdown);
			return { };
		}

		using ResultType = decltype(std::invoke(reinterpret_cast<T>(nt_shutdown), std::forward<Args>(args)...));
		ResultType buffer { };
		buffer = std::invoke(reinterpret_cast<T>(nt_shutdown), std::forward<Args>(args)...);

		//Restore function
		if (!mem.Write(ntos_shutdown, orig_bytes, sizeof(orig_bytes), 4))
			printf("[!] Failed to write memory at 0x%p\n", ntos_shutdown);

		return buffer;
	}

	NTSTATUS fnPsLookupThreadByThreadId(HANDLE threadId, void* thread)
	{
		auto ptr = mem.GetExportTableAddress("PsLookupThreadByThreadId", "csrss.exe", "ntoskrnl.exe");
		if (ptr > 0)
			return SysCall<PsLookupThreadByThreadId>(ptr, threadId, thread);
		return 1;
	}

	NTSTATUS fnPsGetContextThread(void* thread, PCONTEXT context, KPROCESSOR_MODE previousMode)
	{
		auto ptr = mem.GetExportTableAddress("PsGetContextThread", "csrss.exe", "ntoskrnl.exe");
		if (ptr > 0)
			return SysCall<PsGetContextThread>(ptr, thread, context, previousMode);
		return 1;
	}

	NTSTATUS fnPsSetContextThread(void* thread, PCONTEXT context, KPROCESSOR_MODE previousMode)
	{
		auto ptr = mem.GetExportTableAddress("PsSetContextThread", "csrss.exe", "ntoskrnl.exe");
		if (ptr > 0)
			return SysCall<PsSetContextThread>(ptr, thread, context, previousMode);
		return 1;
	}

	NTSTATUS fnPsSuspendThread(void* thread, PULONG previousSuspendCount)
	{
		static uintptr_t ptr = 0;
		if (!ptr)
		{
			PVMMDLL_MAP_MODULEENTRY module_info;
			auto result = VMMDLL_Map_GetModuleFromNameW(mem.vHandle, 4, (LPWSTR)L"ntoskrnl.exe", &module_info, VMMDLL_MODULE_FLAG_NORMAL);
			if (result)
			{
				char str[32];
				ZeroMemory(str, 32);
				if (!VMMDLL_PdbLoad(mem.vHandle, 4, module_info->vaBase, str))
				{
					printf("failed to load pdb\n");
					return 1;
				}

				if (!VMMDLL_PdbSymbolAddress(mem.vHandle, str, (LPSTR)"PsSuspendThread", &ptr))
				{
					printf("failed to find PsSuspendThread\n");
					return 1;
				}
				if (ptr > 0)
					return SysCall<PsSuspendThread>(ptr, thread, previousSuspendCount);
			}
		}
		if (ptr > 0)
			return SysCall<PsSuspendThread>(ptr, thread, previousSuspendCount);
		return 1;
	}

	NTSTATUS fnPsResumeThread(void* thread, PULONG previousSuspendCount)
	{
		static uintptr_t ptr = 0;
		if (!ptr)
		{
			PVMMDLL_MAP_MODULEENTRY module_info;
			auto result = VMMDLL_Map_GetModuleFromNameW(mem.vHandle, 4, (LPWSTR)L"ntoskrnl.exe", &module_info, VMMDLL_MODULE_FLAG_NORMAL);
			if (result)
			{
				char str[32];
				ZeroMemory(str, 32);
				if (!VMMDLL_PdbLoad(mem.vHandle, 4, module_info->vaBase, str))
				{
					printf("failed to load pdb\n");
					return 1;
				}

				if (!VMMDLL_PdbSymbolAddress(mem.vHandle, str, (LPSTR)"PsResumeThread", &ptr))
				{
					printf("failed to find PsResumeThread\n");
					return 1;
				}
				if (ptr > 0)
					return SysCall<PsResumeThread>(ptr, thread, previousSuspendCount);
			}
		}
		if (ptr > 0)
			return SysCall<PsResumeThread>(ptr, thread, previousSuspendCount);
		return 1;
	}

	HANDLE hk_open_thread(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwThreadId)
	{
		HANDLE hThread = 0;
		printf("dwThreadId: %d\n", dwThreadId);
		NTSTATUS status = fnPsLookupThreadByThreadId((HANDLE)dwThreadId, &hThread);
		printf("Created Thread: %p\n", hThread);
		if (status == 0)
			return hThread;
		return nullptr;
	}

	BOOL hk_get_thread_context(HANDLE hThread, PCONTEXT pContext)
	{
		return (fnPsGetContextThread(hThread, pContext, UserMode) == 0);
	}

	BOOL hk_set_thread_context(HANDLE hThread, PCONTEXT pContext)
	{
		printf("set_thread_context\n");
		printf("hThread: %p\n", hThread);
		NTSTATUS status = fnPsSetContextThread(hThread, pContext, UserMode);
		printf("status: %d\n", status);
		return (status == 0);
	}

	DWORD hk_suspend_thread(HANDLE hThread)
	{
		ULONG suspendCount = 0;
		NTSTATUS status = fnPsSuspendThread(hThread, &suspendCount);
		if (status == 0)
			return suspendCount;
		return -1;
	}

	DWORD hk_resume_thread(HANDLE hThread)
	{
		ULONG suspendCount = 0;
		NTSTATUS status = fnPsResumeThread(hThread, &suspendCount);
		if (status == 0)
			return suspendCount;

		return -1;
	}

	PVMMDLL_MAP_THREAD thread_info = NULL;
	DWORD current_thread = 0;

	BOOL hk_thread_32_first(HANDLE hSnapshot, LPTHREADENTRY32 lpte)
	{
		thread_info = NULL;
		current_thread = 0;
		if (!VMMDLL_Map_GetThread(mem.vHandle, mem.current_process.PID, &thread_info))
			return false;

		lpte->dwSize = sizeof(LPTHREADENTRY32);
		lpte->th32ThreadID = thread_info->pMap[current_thread].dwTID;
		lpte->th32OwnerProcessID = thread_info->pMap[current_thread].dwPID;
		lpte->tpBasePri = thread_info->pMap[current_thread].bBasePriority;
		lpte->tpDeltaPri = thread_info->pMap[current_thread].bPriority;
		return true;
	}

	BOOL hk_thread_32_next(HANDLE hSnapshot, LPTHREADENTRY32 lpte)
	{
		if (current_thread >= thread_info->cMap)
		{
			current_thread = 0;
			return false;
		}

		lpte->dwSize = sizeof(LPTHREADENTRY32);
		lpte->th32ThreadID = thread_info->pMap[current_thread].dwTID;
		lpte->th32OwnerProcessID = thread_info->pMap[current_thread].dwPID;
		lpte->tpBasePri = thread_info->pMap[current_thread].bBasePriority;
		lpte->tpDeltaPri = thread_info->pMap[current_thread].bPriority;
		current_thread++;
		return true;
	}
}