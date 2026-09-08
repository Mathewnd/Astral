#include <u80211/regulatory_database.h>
#include <u80211/status.h>

#define CHANNEL_COUNT 14
static u80211_channel_rules_t channels[CHANNEL_COUNT] = {
	{0, 20}, // ch1
	{0, 20}, // ch2
	{0, 20}, // ch3
	{0, 20}, // ch4
	{0, 20}, // ch5
	{0, 20}, // ch6
	{0, 20}, // ch7
	{0, 20}, // ch8
	{0, 20}, // ch9
	{0, 20}, // ch10
	{0, 20}, // ch11
	{U80211_CHANNEL_RULES_DISABLED, 0}, // ch12
	{U80211_CHANNEL_RULES_DISABLED, 0}, // ch13
	{U80211_CHANNEL_RULES_DISABLED, 0}, // ch14
};

u80211_channel_rules_t u80211_get_channel_rules(int channel) {
	if (channel > CHANNEL_COUNT) {
		const u80211_channel_rules_t disabled_channel = {U80211_CHANNEL_RULES_DISABLED, 0};
		return disabled_channel;
	}

	return channels[channel - 1];
}
