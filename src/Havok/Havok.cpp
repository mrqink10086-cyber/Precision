#include "Havok/Havok.h"

#include "Offsets.h"

RE::hkRagdollConstraintCinfo::hkRagdollConstraintCinfo()
{
	SKSE::stl::emplace_vtable(this);
}

void RE::hkConstraintCinfo::CreateConstraintData()
{
	// CommonLib declares this virtual but does not provide its engine implementation.
	static REL::Relocation<const uintptr_t*> vtable{ RE::VTABLE_hkConstraintCinfo[0] };
	using func_t = void (*)(RE::hkConstraintCinfo*);
	reinterpret_cast<func_t>(vtable.get()[1])(this);
}

RE::hkConstraintCinfo::~hkConstraintCinfo()
{
	hkConstraintCinfo_setConstraintData(this, nullptr);
}

void hkpWorld_removeContactListener(RE::hkpWorld* a_this, RE::hkpContactListener* a_worldListener)
{
	RE::hkArray<RE::hkpContactListener*>& listeners = a_this->contactListeners;

	for (int i = 0; i < listeners.size(); i++) {
		RE::hkpContactListener* listener = listeners[i];
		if (listener == a_worldListener) {
			listeners[i] = nullptr;
			return;
		}
	}
}

bool hkpWorld_hasContactListener(RE::hkpWorld* a_this, RE::hkpContactListener* a_worldListener)
{
	RE::hkArray<RE::hkpContactListener*>& listeners = a_this->contactListeners;

	for (int i = 0; i < listeners.size(); i++) {
		RE::hkpContactListener* listener = listeners[i];
		if (listener == a_worldListener) {
			return true;
		}
	}

	return false;
}

RE::bhkCharProxyController* GetCharProxyController(RE::Actor* a_actor)
{
	auto controller = a_actor->GetCharController();
	if (!controller) {
		return nullptr;
	}

	return skyrim_cast<RE::bhkCharProxyController*>(controller);
}

RE::hkMemoryRouter& hkGetMemoryRouter()
{
	return *(RE::hkMemoryRouter*)(uintptr_t)TlsGetValue(*g_dwTlsIndex);
}

RE::bhkRagdollConstraint* bhkRagdolConstraint_ctor(RE::bhkRagdollConstraint* a_this, [[maybe_unused]] RE::hkConstraintCinfo* a_cInfo)
{
	bhkRefObject_ctor(a_this);
	a_this->cinfo = nullptr;

	SKSE::stl::atomic_ref bhkSerializableCount{ *GetBhkSerializableCount() };
	++bhkSerializableCount;

	SKSE::stl::emplace_vtable(a_this);

	bhkConstraint_Func46(a_this, nullptr);

	SKSE::stl::atomic_ref bhkRagdollConstraintCount{ *GetBhkRagdollConstraintCount() };
	++bhkRagdollConstraintCount;

	return a_this;
}
