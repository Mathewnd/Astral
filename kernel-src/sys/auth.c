#include <kernel/auth.h>

static authscope_t scopes[AUTH_SCOPE_COUNT];

void auth_registerlistener(int _scope, authlistener_t listener) {
	authscope_t *scope = &scopes[_scope];
	for (int i = 0; i < AUTH_LISTENER_LIMIT; ++i) {
		if (scope->listeners[i])
			continue;

		scope->listeners[i] = listener;
		break;
	}
}

int auth_check(int _scope, cred_t *cred, int actions, void *arg0, void *arg1, void *arg2) {
	if (cred == NULL || actions == 0)
		return 0;

	authscope_t *scope = &scopes[_scope];

	int allow = 0;
	for (int i = 0; i < AUTH_LISTENER_LIMIT; ++i) {
		authlistener_t listener = scope->listeners[i];
		if (listener == NULL)
			continue;

		int decision = listener(cred, actions, arg0, arg1, arg2);
		if (decision == AUTH_DECISION_DENY)
			return EPERM;

		allow += decision;
	}

	return allow ? 0 : EPERM;
}
