#ifndef U80211_REGULATORY_DATABASE_H
#define U80211_REGULATORY_DATABASE_H

// TODO: this will have safe defaults for transmission on all countries.
// eventually a proper "database" should be implemented to map a 2-character
// country code into channel rules.

#define U80211_CHANNEL_RULES_DISABLED 1 // no receive/transmission.
#define U80211_CHANNEL_RULES_PASSIVE 2 // only transmit when a passive scan returns an AP in that channel.
typedef struct {
	int flags;
	int max_dbm;
} u80211_channel_rules_t;

u80211_channel_rules_t u80211_get_channel_rules(int channel);

#endif
