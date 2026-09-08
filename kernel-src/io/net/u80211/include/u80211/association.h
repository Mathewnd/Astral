#ifndef U80211_ASSOCIATION_H
#define U80211_ASSOCIATION_H

#include <u80211/packet.h>
#include <u80211/u80211.h>

void u80211_association_process_authentication(u80211_device_t *device, u80211_auth_data_t *auth_data); // called from interrupt context
void u80211_association_process_response(u80211_device_t *device, u80211_association_response_data_t *association_data); // called from interrupt context
void u80211_association_process_deauthentication(u80211_device_t *device, u80211_deauthentication_data_t *deauthentication_data); // called from interrupt context
void u80211_association_process_disassociation(u80211_device_t *device, u80211_disassociation_data_t *disassociation_data); // called from interrupt context
bool u80211_association_is_duplicate(u80211_device_t *device, const u80211_header_description_t *header, int expected_state); // called from interrupt context

#endif
