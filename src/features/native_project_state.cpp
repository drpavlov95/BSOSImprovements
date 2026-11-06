#include "features/native_project_state.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

#include "core/host_build.h"
#include "core/log.h"

namespace {

struct NativeProfile {
	uintptr_t frameVtableRva;
	uintptr_t setPendingChangesRva;
	uintptr_t setPendingChangesThunkRva;
	std::array<uintptr_t, 2> saveClearCallRvas;
	size_t pendingChangesOffset;
	size_t projectOffset;
	size_t projectFileNameOffset;
};

constexpr NativeProfile kProfile582 = {
	0x199FEC0, 0x0889760, 0x004FCAF,
	{0x087FC91, 0x088209F}, 0x72C, 0x3C0, 0x740
};

constexpr NativeProfile kProfile591 = {
	0x1BEC810, 0x096D440, 0x0056D16,
	{0x0962B01, 0x0964F0F}, 0x844, 0x3C0, 0x7D8
};

const NativeProfile* RunningProfile() {
	switch (HostBuild::CurrentOutfitStudio()) {
		case HostBuild::OutfitStudioBuild::V5_8_2: return &kProfile582;
		case HostBuild::OutfitStudioBuild::V5_9_1: return &kProfile591;
		default: return nullptr;
	}
}

constexpr size_t kStringSizeOffset = 0x10;
constexpr size_t kStringCapacityOffset = 0x18;
constexpr size_t kWideStringSsoCapacity = 7;
constexpr size_t kMaxProjectPathChars = 32767;

uintptr_t g_frameObject = 0;
NativeProjectState::SaveSucceededCallback g_saveSucceeded = nullptr;
const NativeProfile* g_profile = nullptr;

struct CallPatch {
	uintptr_t address = 0;
	std::array<unsigned char, 5> original = {};
	bool installed = false;
};

CallPatch g_saveCallPatches[2];
void* g_saveRelay = nullptr;

DWORD BasicProtection(DWORD protection) {
	return protection & 0xFFu;
}

bool ProtectionReadable(DWORD protection) {
	if (protection & (PAGE_GUARD | PAGE_NOACCESS))
		return false;
	switch (BasicProtection(protection)) {
		case PAGE_READONLY:
		case PAGE_READWRITE:
		case PAGE_WRITECOPY:
		case PAGE_EXECUTE_READ:
		case PAGE_EXECUTE_READWRITE:
		case PAGE_EXECUTE_WRITECOPY:
			return true;
		default:
			return false;
	}
}

bool RangeReadable(uintptr_t address, size_t size) {
	if (!address || !size || address > (std::numeric_limits<uintptr_t>::max)() - size)
		return false;
	const uintptr_t end = address + size;
	for (uintptr_t cursor = address; cursor < end;) {
		MEMORY_BASIC_INFORMATION mbi = {};
		if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &mbi, sizeof(mbi)) ||
			mbi.State != MEM_COMMIT || !ProtectionReadable(mbi.Protect))
			return false;
		const uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
		if (next <= cursor)
			return false;
		cursor = (std::min)(end, next);
	}
	return true;
}

bool ReadPointer(uintptr_t address, uintptr_t& value) {
	if (!RangeReadable(address, sizeof(value)))
		return false;
	std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
	return true;
}

bool RelativeBranchTargets(uintptr_t instruction, unsigned char opcode, uintptr_t target) {
	if (!RangeReadable(instruction, 5) ||
		*reinterpret_cast<const unsigned char*>(instruction) != opcode)
		return false;
	int32_t relative = 0;
	std::memcpy(&relative, reinterpret_cast<const void*>(instruction + 1), sizeof(relative));
	return static_cast<uintptr_t>(static_cast<int64_t>(instruction) + 5 + relative) == target;
}

bool MatchesSetPendingChanges(uintptr_t function, size_t pendingOffset) {
	std::array<unsigned char, 15> expected = {
		0x38, 0x91, 0, 0, 0, 0, 0x74, 0x0B,
		0x88, 0x91, 0, 0, 0, 0, 0xE9
	};
	const uint32_t offset = static_cast<uint32_t>(pendingOffset);
	std::memcpy(expected.data() + 2, &offset, sizeof(offset));
	std::memcpy(expected.data() + 10, &offset, sizeof(offset));
	return RangeReadable(function, expected.size()) &&
		std::memcmp(reinterpret_cast<const void*>(function), expected.data(),
			expected.size()) == 0;
}

bool LooksLikeFrameObject(uintptr_t object, uintptr_t vtable, size_t pendingOffset) {
	uintptr_t actualVtable = 0;
	if (!ReadPointer(object, actualVtable) || actualVtable != vtable ||
		!RangeReadable(object + pendingOffset, 1))
		return false;
	const unsigned char pending = *reinterpret_cast<const unsigned char*>(
		object + pendingOffset);
	return pending <= 1;
}

bool ObjectContainsWindow(uintptr_t object, HWND frame) {
	MEMORY_BASIC_INFORMATION mbi = {};
	if (!VirtualQuery(reinterpret_cast<const void*>(object), &mbi, sizeof(mbi)) ||
		mbi.State != MEM_COMMIT || !ProtectionReadable(mbi.Protect))
		return false;
	const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
	const size_t available = static_cast<size_t>(regionEnd - object);
	const size_t bytes = (std::min<size_t>)(available, 0x1000);
	const uintptr_t wanted = reinterpret_cast<uintptr_t>(frame);
	for (size_t offset = 0; offset + sizeof(uintptr_t) <= bytes; offset += sizeof(uintptr_t)) {
		uintptr_t value = 0;
		std::memcpy(&value, reinterpret_cast<const void*>(object + offset), sizeof(value));
		if (value == wanted)
			return true;
	}
	return false;
}

std::vector<uintptr_t> FindFrameObjects(uintptr_t vtable) {
	std::vector<uintptr_t> found;
	std::vector<unsigned char> copy(1024 * 1024);
	SYSTEM_INFO info = {};
	GetSystemInfo(&info);
	uintptr_t cursor = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
	const uintptr_t maximum = reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
	while (cursor < maximum) {
		MEMORY_BASIC_INFORMATION mbi = {};
		if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &mbi, sizeof(mbi)))
			break;
		const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
		const uintptr_t next = base + mbi.RegionSize;
		if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && ProtectionReadable(mbi.Protect)) {
			for (uintptr_t chunk = base; chunk < next;) {
				const size_t wanted = static_cast<size_t>((std::min)(
					static_cast<uintptr_t>(copy.size()), next - chunk));
				SIZE_T read = 0;
				if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(chunk),
					copy.data(), wanted, &read) && read >= sizeof(uintptr_t)) {
					const size_t first = static_cast<size_t>((sizeof(uintptr_t) -
						(chunk & (sizeof(uintptr_t) - 1))) & (sizeof(uintptr_t) - 1));
					for (size_t offset = first; offset + sizeof(uintptr_t) <= read;
						offset += sizeof(uintptr_t)) {
						uintptr_t value = 0;
						std::memcpy(&value, copy.data() + offset, sizeof(value));
						if (value == vtable)
							found.push_back(chunk + offset);
					}
				}
				if (!wanted)
					break;
				chunk += wanted;
			}
		}
		if (next <= cursor)
			break;
		cursor = next;
	}
	return found;
}

uintptr_t ResolveFrameObject(HWND frame, uintptr_t vtable, size_t pendingOffset) {
	if (LooksLikeFrameObject(g_frameObject, vtable, pendingOffset))
		return g_frameObject;

	const std::vector<uintptr_t> candidates = FindFrameObjects(vtable);
	std::vector<uintptr_t> valid;
	for (uintptr_t candidate : candidates)
		if (LooksLikeFrameObject(candidate, vtable, pendingOffset))
			valid.push_back(candidate);

	for (uintptr_t candidate : valid) {
		if (ObjectContainsWindow(candidate, frame)) {
			g_frameObject = candidate;
			LogF("dirty nativo: OutfitStudioFrame localizado pelo HWND");
			return candidate;
		}
	}
	if (valid.size() == 1) {
		g_frameObject = valid.front();
		LogF("dirty nativo: unico OutfitStudioFrame localizado pelo RTTI");
		return g_frameObject;
	}

	LogF("dirty nativo: %zu candidato(s) validos para OutfitStudioFrame", valid.size());
	return 0;
}

void __fastcall SaveSucceededHook(void* frameObject, bool) {
	if (g_saveSucceeded)
		g_saveSucceeded(frameObject);

	HMODULE host = GetModuleHandleW(nullptr);
	if (!host || !g_profile)
		return;
	using SetPendingChanges = void(__fastcall*)(void*, bool);
	reinterpret_cast<SetPendingChanges>(reinterpret_cast<uintptr_t>(host) +
		g_profile->setPendingChangesRva)(frameObject, false);
}

bool WriteCode(uintptr_t address, const void* bytes, size_t size) {
	DWORD oldProtection = 0;
	if (!VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE,
		&oldProtection))
		return false;
	std::memcpy(reinterpret_cast<void*>(address), bytes, size);
	FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);
	DWORD ignored = 0;
	VirtualProtect(reinterpret_cast<void*>(address), size, oldProtection, &ignored);
	return true;
}

bool FitsRelativeCall(uintptr_t callAddress, uintptr_t target) {
	const int64_t distance = static_cast<int64_t>(target) -
		static_cast<int64_t>(callAddress + 5);
	return distance >= (std::numeric_limits<int32_t>::min)() &&
		distance <= (std::numeric_limits<int32_t>::max)();
}

void* AllocateRelayNear(uintptr_t address) {
	SYSTEM_INFO info = {};
	GetSystemInfo(&info);
	const uintptr_t granularity = info.dwAllocationGranularity;
	const uintptr_t origin = address & ~(granularity - 1);
	constexpr uintptr_t kMaxDistance = 0x70000000;
	for (uintptr_t distance = granularity; distance < kMaxDistance; distance += granularity) {
		for (int direction : {1, -1}) {
			uintptr_t candidate = origin;
			if (direction > 0) {
				if (origin > (std::numeric_limits<uintptr_t>::max)() - distance)
					continue;
				candidate = origin + distance;
			}
			else {
				if (origin < distance)
					continue;
				candidate = origin - distance;
			}
			void* relay = VirtualAlloc(reinterpret_cast<void*>(candidate), 0x1000,
				MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
			if (relay && FitsRelativeCall(address, reinterpret_cast<uintptr_t>(relay)))
				return relay;
			if (relay)
				VirtualFree(relay, 0, MEM_RELEASE);
		}
	}
	return nullptr;
}

bool InstallSaveObserver() {
	HMODULE host = GetModuleHandleW(nullptr);
	g_profile = RunningProfile();
	if (!host || !g_profile)
		return false;
	const uintptr_t base = reinterpret_cast<uintptr_t>(host);
	const uintptr_t calls[2] = {
		base + g_profile->saveClearCallRvas[0],
		base + g_profile->saveClearCallRvas[1]
	};
	const uintptr_t setPendingThunk = base + g_profile->setPendingChangesThunkRva;
	const uintptr_t setPendingBody = base + g_profile->setPendingChangesRva;
	if (!RelativeBranchTargets(setPendingThunk, 0xE9, setPendingBody) ||
		!MatchesSetPendingChanges(setPendingBody, g_profile->pendingChangesOffset)) {
		LogF("save nativo: assinatura de SetPendingChanges nao confere");
		return false;
	}
	for (size_t i = 0; i < 2; ++i) {
		if (!RelativeBranchTargets(calls[i], 0xE8, setPendingThunk)) {
			LogF("save nativo: assinatura do ponto %zu nao confere", i);
			return false;
		}
	}

	uintptr_t target = reinterpret_cast<uintptr_t>(&SaveSucceededHook);
	if (!FitsRelativeCall(calls[0], target) || !FitsRelativeCall(calls[1], target)) {
		g_saveRelay = AllocateRelayNear(calls[0]);
		if (!g_saveRelay)
			return false;
		unsigned char relayCode[12] = {0x48, 0xB8}; // mov rax, imm64; jmp rax
		std::memcpy(relayCode + 2, &target, sizeof(target));
		relayCode[10] = 0xFF;
		relayCode[11] = 0xE0;
		std::memcpy(g_saveRelay, relayCode, sizeof(relayCode));
		FlushInstructionCache(GetCurrentProcess(), g_saveRelay, sizeof(relayCode));
		target = reinterpret_cast<uintptr_t>(g_saveRelay);
	}

	for (size_t i = 0; i < 2; ++i) {
		CallPatch& patch = g_saveCallPatches[i];
		patch.address = calls[i];
		std::memcpy(patch.original.data(), reinterpret_cast<const void*>(calls[i]), 5);
		unsigned char call[5] = {0xE8};
		const int64_t distance = static_cast<int64_t>(target) -
			static_cast<int64_t>(calls[i] + 5);
		const int32_t relative = static_cast<int32_t>(distance);
		std::memcpy(call + 1, &relative, sizeof(relative));
		if (!WriteCode(calls[i], call, sizeof(call))) {
			for (size_t undo = 0; undo < i; ++undo) {
				WriteCode(g_saveCallPatches[undo].address,
					g_saveCallPatches[undo].original.data(), 5);
				g_saveCallPatches[undo].installed = false;
			}
			return false;
		}
		patch.installed = true;
	}
	LogF("save nativo: observador de sucesso instalado");
	return true;
}

void RemoveSaveObserver() {
	for (CallPatch& patch : g_saveCallPatches) {
		if (patch.installed)
			WriteCode(patch.address, patch.original.data(), patch.original.size());
		patch = CallPatch();
	}
	if (g_saveRelay) {
		VirtualFree(g_saveRelay, 0, MEM_RELEASE);
		g_saveRelay = nullptr;
	}
	g_profile = nullptr;
}

} // namespace

namespace NativeProjectState {

bool MarkPending(HWND frame) {
	const NativeProfile* profile = RunningProfile();
	if (!frame || !IsWindow(frame) || !profile)
		return false;
	HMODULE host = GetModuleHandleW(nullptr);
	if (!host)
		return false;
	const uintptr_t base = reinterpret_cast<uintptr_t>(host);
	const uintptr_t function = base + profile->setPendingChangesRva;
	if (!MatchesSetPendingChanges(function, profile->pendingChangesOffset)) {
		LogF("dirty nativo: assinatura de SetPendingChanges nao confere");
		return false;
	}

	const uintptr_t object = ResolveFrameObject(frame, base + profile->frameVtableRva,
		profile->pendingChangesOffset);
	if (!object)
		return false;

	using SetPendingChanges = void(__fastcall*)(void*, bool);
	reinterpret_cast<SetPendingChanges>(function)(reinterpret_cast<void*>(object), true);
	const bool marked = *reinterpret_cast<const unsigned char*>(
		object + profile->pendingChangesOffset) == 1;
	LogF("dirty nativo: projeto %s", marked ? "marcado" : "NAO marcado");
	return marked;
}

bool CurrentProjectFile(HWND frame, std::filesystem::path& path) {
	path.clear();
	const NativeProfile* profile = RunningProfile();
	if (!frame || !IsWindow(frame) || !profile)
		return false;
	HMODULE host = GetModuleHandleW(nullptr);
	if (!host)
		return false;

	const uintptr_t base = reinterpret_cast<uintptr_t>(host);
	const uintptr_t object = ResolveFrameObject(frame, base + profile->frameVtableRva,
		profile->pendingChangesOffset);
	if (!object)
		return false;

	uintptr_t project = 0;
	if (!ReadPointer(object + profile->projectOffset, project) || !project)
		return false;

	// Nesta build wxString usa a mesma representacao de 32 bytes vista em
	// SaveProject(): buffer/pointer em +0, tamanho em +0x10, capacidade em
	// +0x18 e SSO para ate sete wchar_t. SaveProject testa exatamente
	// [project+0x750] antes de passar [project+0x740] a OutfitProject::Save.
	const uintptr_t stringObject = project + profile->projectFileNameOffset;
	size_t size = 0;
	size_t capacity = 0;
	if (!RangeReadable(stringObject, 0x20))
		return false;
	std::memcpy(&size, reinterpret_cast<const void*>(stringObject + kStringSizeOffset),
		sizeof(size));
	std::memcpy(&capacity,
		reinterpret_cast<const void*>(stringObject + kStringCapacityOffset),
		sizeof(capacity));
	if (!size || size > kMaxProjectPathChars || capacity < size)
		return false;

	uintptr_t data = stringObject;
	if (capacity > kWideStringSsoCapacity && !ReadPointer(stringObject, data))
		return false;
	if (!RangeReadable(data, (size + 1) * sizeof(wchar_t)))
		return false;
	const wchar_t* text = reinterpret_cast<const wchar_t*>(data);
	if (text[size] != L'\0')
		return false;

	path = std::filesystem::path(std::wstring(text, size));
	LogF("save nativo: projeto atual %ls", path.c_str());
	return !path.empty();
}

bool ObserveSuccessfulSaves(SaveSucceededCallback callback) {
	if (!callback) {
		g_saveSucceeded = nullptr;
		RemoveSaveObserver();
		return true;
	}
	if (g_saveCallPatches[0].installed && g_saveCallPatches[1].installed) {
		g_saveSucceeded = callback;
		return true;
	}
	if (!InstallSaveObserver()) {
		RemoveSaveObserver();
		return false;
	}
	g_saveSucceeded = callback;
	return true;
}

} // namespace NativeProjectState
