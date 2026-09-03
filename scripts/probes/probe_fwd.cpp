// What a TU pays when it only names an entity id: a component header, a system
// interface, anything that stores an id without touching the registry.
#if __has_include(<ecss/Fwd.h>)
#  include <ecss/Fwd.h>
#else
#  include <ecss/Types.h>
#endif

ecss::EntityId probeFwd(ecss::EntityId id) {
	return id == ecss::INVALID_ID ? ecss::EntityId{ 0 } : id;
}
